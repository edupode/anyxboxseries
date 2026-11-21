# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
# Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
# The Original Code is AnyXboxSeries.
# The Original Developer is the Initial Developer.
# The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
#

"""Inspect shader containers, unpack FSSM and emit HLSL for scalar RDNA 2 probes."""

import argparse
import json
import sys
from pathlib import Path

from rdna2_scalar import lift_scalar_compute
from shader_container import inspect_shader, read_shader, unpack_fssm


def code_bytes(data: bytes, offset: int | None, size: int | None) -> bytes:
    if data.startswith(b"FSSM"):
        if offset is None or size is None:
            raise ValueError("FSSM requires an explicit --code-offset and --code-size")
        if offset < 4 or size <= 0 or offset % 4 or size % 4 or offset + size > len(data):
            raise ValueError("FSSM code range must be DWORD aligned and contained in the blob")
        return data[offset:offset + size]
    if offset is not None or size is not None:
        raise ValueError("code ranges are used with FSSM input only")
    return data


def integer(text: str) -> int:
    return int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    for name, help_text in (
        ("inspect", "List DXBC parts and identify desktop DXIL and Xbox metadata"),
        ("unpack", "Extract an FSSM blob from a standalone XBCH or DXBC shader"),
        ("lift", "Translate a standalone RDNA 2 snippet or an explicit FSSM code range"),
    ):
        action = actions.add_parser(name, help=help_text)
        action.add_argument("input", type=Path)
        if name != "inspect":
            action.add_argument("output", type=Path, help="new file to write")
        if name == "lift":
            action.add_argument("--code-offset", type=integer)
            action.add_argument("--code-size", type=integer)
            action.add_argument("--output-sgpr", type=integer, default=0)

    args = parser.parse_args()
    try:
        data = read_shader(args.input)
        if args.action == "inspect":
            print(json.dumps(inspect_shader(data), indent=2))
        elif args.action == "unpack":
            blob = unpack_fssm(data)
            with args.output.open("xb") as stream:
                stream.write(blob)
        else:
            hlsl = lift_scalar_compute(code_bytes(data, args.code_offset, args.code_size),
                                       args.output_sgpr)
            with args.output.open("x", encoding="utf-8") as stream:
                stream.write(hlsl)
    except (ValueError, OSError) as exc:
        print(f"shader preparation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
