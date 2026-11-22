# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
"""Browse the readable data area of an RPF9 archive, one range at a time."""
import argparse
from contextlib import closing
from dataclasses import dataclass
import mmap
from pathlib import Path
import re
import struct
import zlib

from inspect_ui_resources import resource_header
from rbf0_tree import IncompleteResource, MAX_INFLATED, READ_CHUNK, _iter_zlib_streams, parse_rbf0

PAGE_BYTES = 8 * 1024 * 1024
PNG = b"\x89PNG\r\n\x1a\n"
BINK = re.compile(rb"(?:BIK[bdfghik]|KB2[abdfghijkn])")
SIGNATURES = re.compile(rb"\x78(?=[\x01\x5e\x9c\xda])|" + re.escape(PNG) + b"|" + BINK.pattern)


@dataclass(frozen=True)
class Entry:
    offset: int
    kind: str
    storage: str
    detail: str
    size: int


def header(path):
    with path.open("rb") as file:
        data = file.read(8)
        size = file.seek(0, 2)
    if len(data) != 8 or data[:4] != b"9FPR":
        raise ValueError("Expected an RPF9 archive (9FPR).")
    start = int.from_bytes(data[4:], "little")
    if not 8 <= start <= size:
        raise ValueError("Data area starts outside the archive.")
    return start, size


def media(data, available, require_complete):
    if data.startswith(PNG) and len(data) >= 33:
        length, tag, width, height = struct.unpack_from(">I4sII", data, 8)
        crc = struct.unpack_from(">I", data, 29)[0]
        if length == 13 and tag == b"IHDR" and width and height and zlib.crc32(data[12:29]) == crc:
            return "PNG", f"{width} × {height} · IHDR · {data[24]} bits"
    if BINK.match(data) and len(data) >= 44:
        size, frames, largest, _, width, height, fps, divisor = struct.unpack_from("<8I", data, 4)
        if frames and largest and largest <= size and 0 < width <= 16384 and 0 < height <= 16384 and divisor and 1 <= fps/divisor <= 1000 and 44 + 4 * frames <= size + 8 and (not require_complete or size + 8 <= available):
            return "Bink", f"{data[:4].decode()} · {width} × {height} · {frames} frames · {fps}/{divisor} fps · {size+8} bytes"
    return None


def inflate(mm, offset):
    decoder = zlib.decompressobj()
    result = bytearray()
    pos = offset
    while pos < len(mm):
        chunk = mm[pos:pos+READ_CHUNK]
        try:
            result.extend(decoder.decompress(chunk, MAX_INFLATED+1-len(result)))
        except zlib.error:
            return None
        if len(result) > MAX_INFLATED or decoder.unconsumed_tail:
            return None
        pos += len(chunk)
        if decoder.eof:
            return result, pos - offset - len(decoder.unused_data)
    return None


def classify(payload):
    kind = resource_header(payload)
    if kind:
        return kind.split()[0], kind
    detected = media(payload, len(payload), False)
    if detected:
        return detected
    return "zlib", f"{len(payload):,} inflated bytes"


def read_page(path, offset):
    start, size = header(path)
    if offset is None:
        offset = start
    if not start <= offset <= size:
        raise ValueError(f"Choose an offset between 0x{start:X} and 0x{size:X}.")
    end = min(size, offset + PAGE_BYTES)
    entries = []
    with path.open("rb") as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as mm:
        pos = offset
        while pos < end:
            match = SIGNATURES.search(mm, pos, min(size, end+7))
            if not match or match.start() >= end:
                break
            at = match.start()
            pos = at + 1
            if mm[at] == 0x78:
                decoded = inflate(mm, at)
                if decoded is None:
                    continue
                payload, stored = decoded
                kind, detail = classify(payload)
                entries.append(Entry(at, kind, "zlib", detail, stored))
                pos = at + stored
            else:
                detected = media(mm[at:at+44], size-at, True)
                if detected:
                    kind, detail = detected
                    stored = struct.unpack_from("<I", mm, at+4)[0]+8 if kind == "Bink" else 0
                    entries.append(Entry(at, kind, "raw", detail, stored))
                    if stored:
                        pos = at + stored
    return entries, end, size


def inspect(path, offset):
    start, size = header(path)
    if not start <= offset < size:
        raise ValueError("Selected offset is outside the data area.")
    with path.open("rb") as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as mm:
        if mm[offset:offset+1] == b"\x78":
            decoded = inflate(mm, offset)
            if decoded is None:
                raise ValueError("No complete zlib stream at the selected offset.")
            payload, stored = decoded
            kind, detail = classify(payload)
            text = f"{detail}\nStored {stored:,} bytes · inflated {len(payload):,} bytes"
        else:
            payload = mm[offset:offset+48]
            detected = media(payload, size-offset, True)
            if not detected:
                raise ValueError("No recognized stream at the selected offset.")
            kind, text = detected
    info = [f"{path.name} · 0x{offset:08X}", text]
    if kind == "RBF0":
        # The existing parser also recognizes RBF0 records spanning zlib boundaries.
        with closing(_iter_zlib_streams(path, offset)) as streams:
            next(streams)
            for _ in range(128):
                try:
                    parsed = parse_rbf0(bytes(payload), match="")
                    break
                except IncompleteResource:
                    piece = next(streams, None)
                    if not piece or piece[1].startswith(b"RBF0") or len(payload) + len(piece[1]) > MAX_INFLATED:
                        raise ValueError("RBF0 continuation unavailable or exceeds the reader limit.")
                    payload.extend(piece[1])
            else:
                raise ValueError("RBF0 continuation limit exceeded.")
        info.append(f"{parsed.elements:,} elements · {parsed.values:,} values · {parsed.text_records:,} text records")
        items = dict.fromkeys(s.rstrip("\0") for s in parsed.matches if s.rstrip("\0").isprintable())
        info.extend(["", f"Readable names and text ({len(items)})", *list(items)[:500]])
        if len(items) > 500:
            info.append("Showing the first 500 unique values.")
    else:
        info.extend(["", "Header bytes", bytes(payload[:48]).hex(" ")])
    return "\n".join(info)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", nargs="?", type=Path, help="RPF9 file or package directory")
    parser.add_argument("--offset", type=lambda n:int(n, 0), help="start at this absolute file offset")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--list", action="store_true", help="list readable streams within the next 8 MiB")
    mode.add_argument("--detail", action="store_true", help="inspect the stream at --offset")
    args = parser.parse_args()
    if args.list or args.detail:
        if not args.archive or not args.archive.is_file() or (args.detail and args.offset is None):
            parser.error("Select an RPF9 file and supply --offset for --detail.")
        try:
            if args.detail:
                print(inspect(args.archive,args.offset))
            else:
                entries,end,size = read_page(args.archive,args.offset)
                for entry in entries:
                    print(f"0x{entry.offset:08X}  {entry.storage:4}  {entry.detail}")
                print(f"{len(entries)} streams or headers · range end 0x{end:X} / 0x{size:X}")
        except (OSError, ValueError) as e:
            parser.exit(1, str(e)+"\n")
    else:
        from importlib.util import module_from_spec, spec_from_file_location
        ui_path = Path(__file__).resolve().parents[1]/"viewer"/"rpf"/"window.py"
        spec = spec_from_file_location("rpf_window",ui_path)
        ui = module_from_spec(spec)
        spec.loader.exec_module(ui)
        ui.open_explorer(args.archive,args.offset,header,read_page,inspect,PAGE_BYTES)


if __name__ == "__main__":
    main()
