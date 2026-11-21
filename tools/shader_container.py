# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
# Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
# The Original Code is AnyXboxSeries.
# The Original Developer is the Initial Developer.
# The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
#

"""Read standalone DXBC containers and the documented Xbox XDAT/XBCH layout."""

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


MAX_SHADER_BYTES = 64 * 1024 * 1024
MAX_PARTS = 4096
XBOX_PARTS = {b"XBOX", b"XS2M", b"ACCC", b"FSSM", b"XBCH"}


def read_shader(path: Path) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(MAX_SHADER_BYTES + 1)
    if len(data) > MAX_SHADER_BYTES:
        raise ValueError("shader exceeds the 64 MiB file limit")
    return data


@dataclass(frozen=True)
class Part:
    tag: bytes
    offset: int
    payload: memoryview


def parts(data: bytes) -> list[Part]:
    if len(data) < 32 or data[:4] != b"DXBC":
        raise ValueError("expected a standalone DXBC container")
    size, count = struct.unpack_from("<II", data, 24)
    if size != len(data) or count > MAX_PARTS or 32 + 4 * count > size:
        raise ValueError("invalid DXBC size or part directory")
    first_part = 32 + 4 * count
    view = memoryview(data)
    result = []
    ranges = []
    for (offset,) in struct.iter_unpack("<I", view[32:first_part]):
        if not first_part <= offset <= size - 8:
            raise ValueError(f"DXBC part header outside payload at 0x{offset:x}")
        tag, length = struct.unpack_from("<4sI", view, offset)
        end = offset + 8 + length
        if end > size:
            raise ValueError(f"DXBC part at 0x{offset:x} exceeds container size")
        result.append(Part(tag, offset, view[offset + 8:end]))
        ranges.append((offset, end))
    _check_overlap(ranges, "DXBC")
    return result


def _check_overlap(ranges: list[tuple[int, int]], directory: str) -> None:
    previous_end = 0
    for start, end in sorted(ranges):
        if start < previous_end:
            raise ValueError(f"overlapping {directory} entries at 0x{start:x}")
        previous_end = end


def _bitcode(payload: memoryview) -> tuple[int, int] | None:
    if len(payload) < 24 or payload[8:12] != b"DXIL":
        return None
    words = struct.unpack_from("<I", payload, 4)[0]
    relative_offset, size = struct.unpack_from("<II", payload, 16)
    start = 8 + relative_offset
    if (start < 24 or size < 4 or start + size > words * 4
            or words * 4 > len(payload) or payload[start:start + 4] != b"BC\xc0\xde"):
        return None
    return start, size


def _xdat(payload: memoryview) -> dict[int, memoryview]:
    if len(payload) < 24 or payload[8:12] not in (b"DXIL", b"NOBC") or _bitcode(payload):
        return {}

    # The Xbox program stub precedes the directory. Look only at aligned
    # positions near the stub, never inside a valid desktop bitcode program.
    markers = [pos for pos in range(24, min(256, len(payload) - 3), 4)
               if payload[pos:pos + 4] == b"XDAT"]
    if not markers:
        return {}
    if len(markers) != 1:
        raise ValueError("ambiguous XDAT directory")
    offset = markers[0]
    if offset + 12 > len(payload):
        raise ValueError("truncated XDAT directory header")
    count = struct.unpack_from("<I", payload, offset + 4)[0]
    table_end = offset + 12 + 12 * count
    if not 1 <= count <= MAX_PARTS or table_end > len(payload):
        raise ValueError("invalid XDAT directory size")

    entries = {}
    ranges = []
    for tag, start, length in struct.iter_unpack("<III", payload[offset + 12:table_end]):
        end = start + length
        if start < table_end or end > len(payload) or tag in entries:
            raise ValueError(f"invalid or repeated XDAT tag 0x{tag:x}")
        entries[tag] = payload[start:end]
        if length:
            ranges.append((start, end))
    _check_overlap(ranges, "XDAT")
    return entries


def _fourcc(tag: bytes) -> str:
    return tag.decode("ascii", errors="backslashreplace")


def inspect_shader(data: bytes) -> dict:
    if data[:4] in (b"XBCH", b"FSSM"):
        return {"format": _fourcc(data[:4]), "size": len(data)}

    listed = []
    for part in parts(data):
        description = {"tag": _fourcc(part.tag), "part_offset": part.offset,
                       "size": len(part.payload)}
        if part.tag in XBOX_PARTS:
            description["xbox_metadata"] = True
        if part.tag == b"DXIL":
            payload = part.payload
            bitcode = _bitcode(payload)
            entries = _xdat(payload)
            if len(payload) >= 24 and payload[8:12] in (b"DXIL", b"NOBC"):
                program = struct.unpack_from("<I", payload)[0]
                description["shader_kind"] = program >> 16
                description["shader_model"] = f"{(program >> 4) & 15}.{program & 15}"
            description["has_dxil_bitcode"] = bitcode is not None
            if bitcode:
                start, size = bitcode
                description["bitcode_size"] = size
                description["xbox_symbol_marker"] = b"@xbox." in payload[start:start + size].tobytes()
            if entries:
                description["xdat"] = {f"0x{tag:x}": len(value) for tag, value in entries.items()}
                description["xbch"] = entries.get(0x0D, b"")[:4] == b"XBCH"
        listed.append(description)
    return {"format": "DXBC", "size": len(data), "parts": listed}


def unpack_fssm(data: bytes) -> bytes:
    if data[:4] == b"XBCH":
        payload = memoryview(data)
    else:
        candidates = []
        for part in parts(data):
            if part.tag == b"DXIL":
                section = _xdat(part.payload).get(0x0D)
                if section is not None:
                    candidates.append(section)
        if len(candidates) != 1:
            raise ValueError("expected exactly one XDAT 0x0d entry")
        payload = candidates[0]

    if len(payload) < 24 or payload[:4] != b"XBCH":
        raise ValueError("invalid XBCH header")
    size = struct.unpack_from("<I", payload, 16)[0]
    if not 4 <= size <= MAX_SHADER_BYTES:
        raise ValueError("invalid XBCH decompressed size")
    try:
        decoder = zlib.decompressobj()
        result = decoder.decompress(payload[24:], size + 1)
    except zlib.error as exc:
        raise ValueError(f"invalid XBCH zlib stream ({exc})") from exc
    if (len(result) != size or not decoder.eof or decoder.unconsumed_tail
            or decoder.unused_data):
        raise ValueError("XBCH stream does not match its declared size")
    if not result.startswith(b"FSSM"):
        raise ValueError("XBCH did not contain an FSSM blob")
    return result
