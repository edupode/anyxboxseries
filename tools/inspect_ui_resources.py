# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License");
# you may not use this file except in compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/CPAL-1.0. Software distributed under the License is provided on an "AS IS"
# basis, without warranty. See the License for the specific language governing rights and limitations.
# The Original Code is AnyXboxSeries. The Initial Developer is edupode. All portions written by edupode are
# Copyright (c) 2026 edupode. All Rights Reserved.

"""Locate readable UI record headers in an RPF9 archive. Writes no game data."""

import argparse
from collections import Counter
import mmap
from pathlib import Path
import re
from typing import Iterator
import zlib


# A header match alone is not evidence of a valid stream.
ZLIB_HEADER = re.compile(rb"\x78(?=[\x01\x5e\x9c\xda])")
MAX_STORED = 64 * 1024 + 1024
MAX_INFLATED = 64 * 1024
SCHEMA_NAME = re.compile(rb"[A-Za-z_][A-Za-z0-9_]*")


def resource_header(payload: bytes) -> str | None:
    """Classify a stream start. A resource may continue into later streams."""
    if payload.startswith(b"RBF0"):
        # Format reference: Leonida, docs/rbf0.md at aae4d99.
        start = 8 if payload[4:8] == b"\xfb\xff\x01\x00" else 4
        if len(payload) < start + 10 or payload[start:start + 2] != b"\0\0":
            return None
        name_length = int.from_bytes(payload[start + 2:start + 4], "little")
        end = start + 4 + name_length
        name = payload[start + 4:end]
        if end + 6 <= len(payload) and SCHEMA_NAME.fullmatch(name):
            return "RBF0 " + name.decode("ascii")
    elif len(payload) >= 16 and payload.startswith(b"PSIN"):
        # PSIN v22 is metadata; seeing its header does not locate a script.
        section = int.from_bytes(payload[4:8], "big")
        if 16 <= section <= len(payload) - 8 and payload[8:12] == b"\x16\0\0\0":
            return "PSIN v22 header candidate"
    return None


def scan_archive(path: Path) -> Iterator[tuple[int, str]]:
    with path.open("rb") as file:
        if file.read(4) != b"9FPR":
            raise ValueError("expected RPF9 magic (9FPR)")
        with mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as archive:
            end_of_stream = 4
            for match in ZLIB_HEADER.finditer(archive, end_of_stream):
                offset = match.start()
                if offset < end_of_stream:
                    continue
                decoder = zlib.decompressobj()
                source = memoryview(archive)[offset:offset + MAX_STORED]
                try:
                    payload = decoder.decompress(source, MAX_INFLATED + 1)
                except zlib.error:
                    source.release()
                    continue
                if not decoder.eof or len(payload) > MAX_INFLATED:
                    source.release()
                    continue
                consumed = len(source) - len(decoder.unused_data)
                source.release()
                if consumed <= 0:
                    continue
                end_of_stream = offset + consumed
                kind = resource_header(payload)
                if kind is not None:
                    yield offset, kind


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", nargs="+", type=Path)
    parser.add_argument("--offsets", action="store_true", help="list each header's archive offset")
    args = parser.parse_args()
    for path in args.archives:
        print(path.name)
        counts: Counter[str] = Counter()
        try:
            for offset, kind in scan_archive(path):
                counts[kind] += 1
                if args.offsets:
                    print(f"  0x{offset:08X}  {kind}")
        except (OSError, ValueError) as error:
            parser.exit(1, f"{path.name}: {error}\n")
        for kind, count in sorted(counts.items()):
            print(f"  {count:>5}  {kind}")
        print(f"  {counts.total():>5}  recognized stream starts")


if __name__ == "__main__":
    main()
