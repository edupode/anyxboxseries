# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
# Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
# The Original Code is AnyXboxSeries.
# The Original Developer is the Initial Developer.
# The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
#

"""Lift a bounded RDNA 2 scalar program to a D3D12 compute probe."""

import struct


MAX_WORDS = 1024
LAST_SGPR = 105
END_PROGRAM = 0xBF810000
MASK32 = 0xFFFFFFFF
COMPARE = ("==", "!=", ">", ">=", "<", "<=")

# The operands are snapshotted before the destination is changed, including
# when one of them reads the previous SCC or aliases the destination SGPR.
BINARY = {
    0: ("{a} + {b}", "{value} < {a}"),
    1: ("{a} - {b}", "{a} < {b}"),
    2: ("{a} + {b}", "((~({a} ^ {b}) & ({a} ^ {value})) >> 31) != 0u"),
    3: ("{a} - {b}", "((({a} ^ {b}) & ({a} ^ {value})) >> 31) != 0u"),
    4: ("{a} + {b} + uint({scc})", "({value} < {a}) || ({scc} && {value} == {a})"),
    5: ("{a} - {b} - uint({scc})", "({a} < {b}) || ({scc} && {a} == {b})"),
    10: ("{scc} ? {a} : {b}", None),
    14: ("{a} & {b}", "{value} != 0u"),
    16: ("{a} | {b}", "{value} != 0u"),
    18: ("{a} ^ {b}", "{value} != 0u"),
    30: ("{a} << ({b} & 31u)", "{value} != 0u"),
    32: ("{a} >> ({b} & 31u)", "{value} != 0u"),
    34: ("asuint(asint({a}) >> ({b} & 31u))", "{value} != 0u"),
    38: ("{a} * {b}", None),
}


def _u32(value: int) -> str:
    return f"0x{value & MASK32:08x}u"


def lift_scalar_compute(code: bytes, output_sgpr: int) -> str:
    if not 0 <= output_sgpr <= LAST_SGPR:
        raise ValueError("output must be s0 through s105")
    if not code or len(code) % 4 or len(code) > 4 * MAX_WORDS:
        raise ValueError(f"RDNA 2 code must contain 1 to {MAX_WORDS} whole DWORDs")

    words = [word for (word,) in struct.iter_unpack("<I", code)]
    lines = [
        "cbuffer InitialRegisters : register(b0) { uint initialS0; uint initialS1; };",
        "RWByteAddressBuffer result : register(u0);",
        "",
        "[numthreads(1, 1, 1)]",
        "void main() {",
    ]
    registers = {0: "initialS0", 1: "initialS1"}
    scc = None
    pc = 0
    while pc < len(words):
        index = pc
        word = words[pc]
        pc += 1
        prefix = word & 0xFF800000
        op = word >> 16 & 0x7F
        where = f"byte 0x{index * 4:x} (0x{word:08x})"
        value = f"v{index}"

        if word == END_PROGRAM:
            if pc != len(words):
                raise ValueError(f"{where}: trailing words after s_endpgm")
            if output_sgpr not in registers:
                raise ValueError(f"{where}: s{output_sgpr} is undefined")
            lines += [f"    result.Store2(0, uint2({registers[output_sgpr]}, 0x57535831u));", "}"]
            return "\n".join(lines) + "\n"
        if prefix == 0xBF800000 and op == 0 and (word & 0xFFFF) <= 15:
            continue  # s_nop has no observable effect in this probe.

        def read_source(source: int) -> str:
            if 0 <= source <= LAST_SGPR:
                if source in registers:
                    return registers[source]
                raise ValueError(f"{where}: read of undefined s{source}")
            if 128 <= source <= 192:
                return _u32(source - 128)
            if 193 <= source <= 208:
                return _u32(192 - source)
            if source == 253:
                if scc is None:
                    raise ValueError(f"{where}: read of undefined SCC")
                return f"uint({scc})"
            if source == 255:
                return _u32(literal)
            raise ValueError(f"{where}: unsupported scalar operand {source}")

        # These formats are subsets of the high bits used by SOP2/SOPK.
        if prefix == 0xBE800000:
            instruction = word >> 8 & 0xFF
            sources = (word & 0xFF,)
            unary = {3: "{a}", 7: "~{a}"}
            if instruction not in unary:
                raise ValueError(f"{where}: unsupported SOP1 opcode {instruction}")
            expression = unary[instruction]
            flag = "{value} != 0u" if instruction == 7 else None
        elif prefix == 0xBF000000 and op < 12:
            sources = (word & 0xFF, word >> 8 & 0xFF)
            comparison = COMPARE[op % 6]
            flag = ("asint({a}) " + comparison + " asint({b})" if op < 6
                    else "{a} " + comparison + " {b}")
            expression = None
        elif word >> 28 == 0xB and (word >> 23 & 31) == 0:
            sources = ()
            immediate = word & 0xFFFF
            expression = _u32(immediate - 0x10000 if immediate & 0x8000 else immediate)
            flag = None
        elif word >> 30 == 2 and prefix < 0xBE800000:
            instruction = word >> 23 & 0x7F
            if instruction not in BINARY:
                raise ValueError(f"{where}: unsupported SOP2 opcode {instruction}")
            sources = (word & 0xFF, word >> 8 & 0xFF)
            expression, flag = BINARY[instruction]
        else:
            raise ValueError(f"{where}: unsupported RDNA 2 instruction")

        literal = 0
        if 255 in sources:
            if pc == len(words):
                raise ValueError(f"{where}: missing literal DWORD")
            literal = words[pc]
            pc += 1

        operands = [read_source(source) for source in sources]
        if scc is None and any(part and "{scc}" in part for part in (expression, flag)):
            raise ValueError(f"{where}: read of undefined SCC")
        slots = dict(zip(("a", "b"), operands), scc=scc, value=value)
        if expression is not None:
            if op > LAST_SGPR:
                raise ValueError(f"{where}: unsupported destination {op}")
            lines.append(f"    uint {value} = {expression.format(**slots)};")
            registers[op] = value
        if flag is not None:
            scc = f"cc{index}"
            lines.append(f"    bool {scc} = {flag.format(**slots)};")
    raise ValueError("RDNA 2 code is missing its final s_endpgm")
