# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License");
# you may not use this file except in compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/CPAL-1.0. Software distributed under the License is provided on an "AS IS"
# basis, without warranty. See the License for the specific language governing rights and limitations.
# The Original Code is AnyXboxSeries. The Initial Developer is edupode. All portions written by edupode are
# Copyright (c) 2026 edupode. All Rights Reserved.

"""Read-only RPF9 zlib walker and clean-room RBF0 structure counter."""

import argparse
from collections import Counter
from dataclasses import dataclass
import mmap
from pathlib import Path
import re
import struct
from typing import Callable, Iterator
import zlib


RPF9_MAGIC = b"9FPR"
RBF0_MAGIC = b"RBF0"
RBF0_PROLOGUE = b"\xfb\xff\x01\x00"
ZLIB_HEADER = re.compile(rb"\x78(?=[\x01\x5e\x9c\xda])")
MAX_INFLATED = 16 * 1024 * 1024
READ_CHUNK = 16 * 1024


class IncompleteResource(Exception):
    """The current zlib chunk ends before the RBF0 record stream is complete."""


class InvalidResource(ValueError):
    """The RBF0 record stream violates the observed format grammar."""


@dataclass(frozen=True)
class ParsedRbf0:
    root_name: str
    value_width: int
    consumed: int
    elements: int
    values: int
    attribute_values: int
    text_records: int
    value_types: dict[str, int]
    max_depth: int
    matches: tuple[str, ...]


@dataclass(frozen=True)
class Rbf0Resource:
    offset: int
    chunks: int
    parsed: ParsedRbf0


@dataclass(frozen=True)
class ArchiveScan:
    zlib_streams: int
    resources: tuple[Rbf0Resource, ...]
    errors: tuple[tuple[int, str], ...]


@dataclass
class _Frame:
    attributes_remaining: int


class _Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    def take(self, size: int) -> bytes:
        if size < 0:
            raise InvalidResource("negative field length")
        end = self.position + size
        if end > len(self.data):
            raise IncompleteResource(f"need {size} bytes at offset 0x{self.position:X}")
        value = self.data[self.position:end]
        self.position = end
        return value

    def u16(self) -> int:
        return struct.unpack("<H", self.take(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]


def _decode_utf8(value: bytes, *, strict: bool) -> str:
    try:
        return value.decode("utf-8", errors="strict" if strict else "replace")
    except UnicodeDecodeError as error:
        raise InvalidResource(f"invalid UTF-8 at offset 0x{error.start:X}") from error


def parse_rbf0(data: bytes, match: str | None = None) -> ParsedRbf0:
    """Parse one exact RBF0 record stream; unknown types and trailing bytes fail closed."""
    reader = _Reader(data)
    if reader.take(4) != RBF0_MAGIC:
        raise InvalidResource("expected RBF0 magic")
    if data[4:8] == RBF0_PROLOGUE:
        reader.position = 8
        value_width = 8
    else:
        value_width = 4

    descriptors: list[str] = []
    stack: list[_Frame] = []
    root_name: str | None = None
    elements = 0
    values = 0
    attribute_values = 0
    text_records = 0
    value_types: Counter[str] = Counter()
    max_depth = 0
    matches: list[str] = []
    needle = match.casefold() if match is not None else None

    def keep_match(value: str) -> None:
        if needle is not None and needle in value.casefold():
            matches.append(value)

    while True:
        index, kind = reader.take(2)
        if (index, kind) == (0xFF, 0xFF):
            if not stack:
                raise InvalidResource("close record without an open element")
            if stack[-1].attributes_remaining:
                raise InvalidResource("element closed before all declared attributes")
            stack.pop()
            if not stack:
                if root_name is None:
                    raise InvalidResource("resource closed without a root element")
                if reader.position != len(data):
                    raise InvalidResource(f"trailing bytes after root at offset 0x{reader.position:X}")
                return ParsedRbf0(
                    root_name=root_name,
                    value_width=value_width,
                    consumed=reader.position,
                    elements=elements,
                    values=values,
                    attribute_values=attribute_values,
                    text_records=text_records,
                    value_types=dict(value_types),
                    max_depth=max_depth,
                    matches=tuple(matches),
                )
            continue

        if (index, kind) == (0xFD, 0xFF):
            if not stack:
                raise InvalidResource("text record outside an open element")
            if stack[-1].attributes_remaining:
                raise InvalidResource("text record before all declared attributes")
            length = reader.u32()
            text = _decode_utf8(reader.take(length), strict=False)
            stack[-1].attributes_remaining = 0
            text_records += 1
            keep_match(text)
            continue

        if index > len(descriptors):
            raise InvalidResource(f"descriptor index {index} exceeds table size {len(descriptors)}")
        if index == len(descriptors):
            name_length = reader.u16()
            name = _decode_utf8(reader.take(name_length), strict=True)
            if not name:
                raise InvalidResource("empty descriptor name")
            descriptors.append(name)
            keep_match(name)
        else:
            name = descriptors[index]

        if kind == 0x00:
            word0, word1, declared_attributes = struct.unpack("<HHH", reader.take(6))
            del word0, word1  # Their values are reserved/unknown in this corpus.
            if stack and stack[-1].attributes_remaining:
                raise InvalidResource("child element before all declared attributes")
            if root_name is None:
                if stack:
                    raise InvalidResource("root element nested under another element")
                root_name = name
            elif not stack:
                raise InvalidResource("multiple root elements")
            elements += 1
            max_depth = max(max_depth, len(stack) + 1)
            stack.append(_Frame(declared_attributes))
            continue

        if not stack:
            raise InvalidResource("value record outside an open element")

        if kind == 0x10:
            reader.take(value_width)
            value_type = "integer"
        elif kind in (0x20, 0x30):
            value_type = "boolean"
        elif kind == 0x40:
            reader.take(value_width)
            value_type = "float"
        elif kind == 0x50:
            reader.take(value_width * 3)
            value_type = "vector3"
        elif kind == 0x60:
            length = reader.u16()
            value = _decode_utf8(reader.take(length), strict=True)
            keep_match(value)
            value_type = "string"
        else:
            raise InvalidResource(f"unknown record type 0x{kind:02X}")

        values += 1
        value_types[value_type] += 1
        if stack[-1].attributes_remaining:
            stack[-1].attributes_remaining -= 1
            attribute_values += 1


def _iter_zlib_streams(path: Path, start_offset: int | None = None) -> Iterator[tuple[int, bytes]]:
    with path.open("rb") as source:
        header = source.read(8)
        if len(header) < 8 or header[:4] != RPF9_MAGIC:
            raise ValueError("expected RPF9 magic (9FPR) and an 8-byte header")
        data_start = struct.unpack_from("<I", header, 4)[0]
        size = path.stat().st_size
        if data_start < 8 or data_start > size:
            raise ValueError(f"RPF9 data start 0x{data_start:X} is outside the archive")
        if start_offset is not None and not data_start <= start_offset < size:
            raise ValueError("selected offset is outside the RPF9 data area")
        with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as archive:
            cursor = start_offset if start_offset is not None else data_start
            while cursor < size:
                match = ZLIB_HEADER.search(archive, cursor)
                if match is None:
                    return
                offset = match.start()
                decoder = zlib.decompressobj()
                payload = bytearray()
                position = offset
                consumed = 0
                valid = True
                while position < size:
                    end = min(position + READ_CHUNK, size)
                    chunk = archive[position:end]
                    try:
                        part = decoder.decompress(chunk, MAX_INFLATED + 1 - len(payload))
                    except zlib.error:
                        valid = False
                        break
                    payload.extend(part)
                    if len(payload) > MAX_INFLATED or decoder.unconsumed_tail:
                        valid = False
                        break
                    if decoder.eof:
                        consumed = position - offset + len(chunk) - len(decoder.unused_data)
                        break
                    position = end
                if not valid or not decoder.eof or consumed <= 0:
                    cursor = offset + 1
                    continue
                cursor = offset + consumed
                yield offset, bytes(payload)


def scan_archive(path: Path, match: str | None = None, offset: int | None = None) -> ArchiveScan:
    """Scan the RPF9 data area, joining adjacent zlib chunks that form one RBF0 resource."""
    zlib_streams = 0
    resources: list[Rbf0Resource] = []
    errors: list[tuple[int, str]] = []
    pending: bytearray | None = None
    pending_offset = 0
    pending_chunks = 0

    def finish_pending() -> None:
        nonlocal pending, pending_chunks
        if pending is not None:
            errors.append((pending_offset, "RBF0 resource remained incomplete before the next root"))
            pending = None
            pending_chunks = 0

    found_offset = False
    for stream_offset, payload in _iter_zlib_streams(path, start_offset=offset):
        zlib_streams += 1
        if offset is not None and not found_offset:
            if stream_offset != offset:
                errors.append((offset, "selected offset is not a zlib stream start"))
                break
            found_offset = True
            if not payload.startswith(RBF0_MAGIC):
                errors.append((offset, "selected zlib stream does not start with RBF0"))
                break
        if payload.startswith(RBF0_MAGIC):
            finish_pending()
            pending = bytearray(payload)
            pending_offset = stream_offset
            pending_chunks = 1
        elif pending is not None:
            pending.extend(payload)
            pending_chunks += 1
        else:
            continue

        try:
            parsed = parse_rbf0(bytes(pending), match=match)
        except IncompleteResource:
            continue
        except InvalidResource as error:
            errors.append((pending_offset, str(error)))
            pending = None
            pending_chunks = 0
        else:
            resources.append(Rbf0Resource(pending_offset, pending_chunks, parsed))
            pending = None
            pending_chunks = 0
            if offset is not None:
                break

    finish_pending()
    if offset is not None and not found_offset and not errors:
        errors.append((offset, "selected offset did not contain a zlib stream"))
    return ArchiveScan(zlib_streams, tuple(resources), tuple(errors))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="RPF9 archive (scans only its readable data area)")
    search = parser.add_mutually_exclusive_group()
    search.add_argument("--find", help="show RBF0 text, string, or descriptor values containing this text")
    search.add_argument("--dump-strings", action="store_true",
                        help="show all descriptor, text, and string values in one resource; requires --offset")
    parser.add_argument("--offset", type=lambda value: int(value, 0),
                        help="parse one RBF0 resource by its archive offset (for example, 0x11D3C000)")
    args = parser.parse_args()
    if args.dump_strings and args.offset is None:
        parser.error("--dump-strings requires --offset to keep output limited to one resource")
    try:
        needle = "" if args.dump_strings else args.find
        result = scan_archive(args.archive, match=needle, offset=args.offset)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{args.archive.name}: {error}\n")

    roots = Counter(resource.parsed.root_name for resource in result.resources)
    elements = sum(resource.parsed.elements for resource in result.resources)
    values = sum(resource.parsed.values for resource in result.resources)
    texts = sum(resource.parsed.text_records for resource in result.resources)
    print(f"{args.archive.name}: {result.zlib_streams} zlib streams, "
          f"{len(result.resources)} complete RBF0 resources, {len(result.errors)} parse errors")
    print(f"  {elements} elements, {values} values, {texts} text records")
    for name, count in sorted(roots.items()):
        print(f"  {count:>5}  {name}")
    for offset, error in result.errors:
        print(f"  ERROR 0x{offset:08X}: {error}")
    if args.find is not None or args.dump_strings:
        for resource in result.resources:
            for value in resource.parsed.matches:
                print(f"  0x{resource.offset:08X} {resource.parsed.root_name}: {value!r}")


if __name__ == "__main__":
    main()
