# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
# Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
# The Original Code is AnyXboxSeries.
# The Original Developer is the Initial Developer.
# The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
#

import struct
import unittest
import zlib

from prepare_shader import code_bytes
from rdna2_scalar import lift_scalar_compute
from shader_container import inspect_shader, unpack_fssm


def words(*values: int) -> bytes:
    return struct.pack("<" + "I" * len(values), *values)


CODE = words(
    0x80020100,  # s_add_u32 s2, s0, s1
    0x8503A9A1,  # s_cselect_b32 s3, 33, 41
    0x89000302,  # s_xor_b32 s0, s2, s3
    0xBE8403FF, 0xBF810000,  # s_mov_b32 s4, literal (same bits as the terminator)
    0x90049F04,  # s_lshr_b32 s4, s4, 31
    0x80000400,  # s_add_u32 s0, s0, s4
    0xBF810000,  # s_endpgm
)
CODE_OFFSET = 16
FSSM = b"FSSM" + bytes(CODE_OFFSET - 4) + CODE


def container(*chunks: tuple[bytes, bytes]) -> bytes:
    offset = 32 + 4 * len(chunks)
    directory = bytearray()
    block = bytearray()
    for tag, payload in chunks:
        directory += words(offset + len(block))
        block += tag + words(len(payload)) + payload
    return b"DXBC" + bytes(16) + words(1, offset + len(block), len(chunks)) + directory + block


def xbch(content: bytes = FSSM, size: int | None = None) -> bytes:
    payload = zlib.compress(content)
    return b"XBCH" + words(0, len(payload), 0, len(content) if size is None else size, 0) + payload


def xdat_shader(payload: bytes | None = None) -> bytes:
    stub = words(0x50066, 6) + b"DXIL" + words(0x106, 16, 0)
    if payload is None:
        block = xbch()
        payload = b"XDAT" + words(1, 24, 0x0D, 48, len(block)) + block
    return container((b"DXIL", stub + payload), (b"ACCC", b"test"))


class ShaderContainerTests(unittest.TestCase):
    def test_xdat_xbch_fssm_and_metadata(self):
        data = xdat_shader()
        dxil, metadata = inspect_shader(data)["parts"]
        self.assertEqual(dxil["shader_model"], "6.6")
        self.assertFalse(dxil["has_dxil_bitcode"])
        self.assertTrue(dxil["xbch"])
        self.assertTrue(metadata["xbox_metadata"])
        self.assertEqual(unpack_fssm(data), FSSM)
        self.assertEqual(unpack_fssm(xbch()), FSSM)

    def test_desktop_bitcode_is_not_treated_as_xdat(self):
        bitcode = b"BC\xc0\xdeXDAT"
        payload = words(0x50066, 8) + b"DXIL" + words(0x106, 16, len(bitcode)) + bitcode
        part = inspect_shader(container((b"DXIL", payload)))["parts"][0]
        self.assertTrue(part["has_dxil_bitcode"])
        self.assertNotIn("xdat", part)

    def test_untrusted_sizes_are_rejected(self):
        data = bytearray(xdat_shader())
        struct.pack_into("<I", data, 28, 0xFFFFFFFF)
        with self.assertRaisesRegex(ValueError, "directory"):
            inspect_shader(bytes(data))
        for invalid in (xbch(size=32), xbch()[:-1], xbch() + b"trailing", xbch(b"bad!")):
            with self.subTest(invalid=invalid[:24]), self.assertRaises(ValueError):
                unpack_fssm(invalid)
        overflow = b"XDAT" + words(1, 24, 0x0D, 48, 0xFFFFFFFF) + xbch()
        with self.assertRaisesRegex(ValueError, "XDAT tag"):
            unpack_fssm(xdat_shader(overflow))

    def test_overlapping_dxbc_parts_are_rejected(self):
        data = bytearray(xdat_shader())
        struct.pack_into("<I", data, 36, struct.unpack_from("<I", data, 32)[0])
        with self.assertRaisesRegex(ValueError, "overlapping DXBC"):
            inspect_shader(bytes(data))


class Rdna2Tests(unittest.TestCase):
    def test_lift_exposes_dynamic_registers(self):
        source = lift_scalar_compute(code_bytes(FSSM, CODE_OFFSET, len(CODE)), 0)
        self.assertIn("uint v0 = initialS0 + initialS1;", source)
        self.assertIn("bool cc0 = v0 < initialS0;", source)
        self.assertIn("uint v1 = cc0 ? 0x00000021u : 0x00000029u;", source)
        self.assertIn("uint v3 = 0xbf810000u;", source)
        self.assertIn("result.Store2(0, uint2(v6, 0x57535831u));", source)

    def test_fssm_requires_a_valid_explicit_code_range(self):
        for offset, size in ((None, None), (None, 4), (0, len(CODE)), (5, 4), (16, 4096)):
            with self.subTest(offset=offset, size=size), self.assertRaises(ValueError):
                code_bytes(FSSM, offset, size)

    def test_rejects_unsupported_or_incomplete_programs(self):
        for code, reason in (
            (words(0xBF820000, 0xBF810000), "unsupported"),  # s_branch
            (words(0xBE8003FF), "missing literal"),
            (words(0xBE80037E, 0xBF810000), "unsupported scalar operand"),
            (words(0xBE800302, 0xBF810000), "undefined s2"),
            (words(0x85000100, 0xBF810000), "undefined SCC"),
            (words(0xBF810000, 0xBF810000), "trailing words"),
            (words(0xBE8003FF, 0xBF810000), "missing its final"),
        ):
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                lift_scalar_compute(code, 0)


if __name__ == "__main__":
    unittest.main()
