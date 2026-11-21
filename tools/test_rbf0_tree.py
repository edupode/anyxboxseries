# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License");
# you may not use this file except in compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/CPAL-1.0. Software distributed under the License is provided on an "AS IS"
# basis, without warranty. See the License for the specific language governing rights and limitations.
# The Original Code is AnyXboxSeries. The Initial Developer is edupode. All portions written by edupode are
# Copyright (c) 2026 edupode. All Rights Reserved.

from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import unittest
import zlib

from rbf0_tree import IncompleteResource, InvalidResource, parse_rbf0, scan_archive


def new_record(index, name, kind, payload=b""):
    encoded = name.encode("utf-8")
    return bytes((index, kind)) + struct.pack("<H", len(encoded)) + encoded + payload


def wide_resource():
    return (
        b"RBF0\xfb\xff\x01\x00"
        + new_record(0, "Root", 0, struct.pack("<HHH", 0, 0, 0))
        + new_record(1, "Item", 0, struct.pack("<HHH", 0, 0, 1))
        + new_record(2, "id", 0x60, struct.pack("<H", 2) + b"ok")
        + b"\xff\xff"
        + b"\xfd\xff" + struct.pack("<I", 6) + b"hello\0"
        + b"\xff\xff"
    )


class Rbf0ParserTests(unittest.TestCase):
    def test_wide_tree_counts_attributes_text_and_search_hits(self):
        result = parse_rbf0(wide_resource(), match="HELLO")
        self.assertEqual(result.root_name, "Root")
        self.assertEqual(result.value_width, 8)
        self.assertEqual(result.elements, 2)
        self.assertEqual(result.values, 1)
        self.assertEqual(result.attribute_values, 1)
        self.assertEqual(result.text_records, 1)
        self.assertEqual(result.max_depth, 2)
        self.assertEqual(result.matches, ("hello\0",))
        self.assertEqual(result.consumed, len(wide_resource()))

    def test_narrow_resource_uses_four_byte_values(self):
        payload = (
            b"RBF0"
            + new_record(0, "Root", 0, struct.pack("<HHH", 0, 0, 1))
            + new_record(1, "count", 0x10, struct.pack("<I", 37))
            + b"\xff\xff"
        )
        result = parse_rbf0(payload)
        self.assertEqual(result.value_width, 4)
        self.assertEqual(result.value_types["integer"], 1)
        self.assertEqual(result.attribute_values, 1)

    def test_incomplete_resource_is_distinct_from_invalid_bytes(self):
        with self.assertRaises(IncompleteResource):
            parse_rbf0(wide_resource()[:-2])
        with self.assertRaises(InvalidResource):
            parse_rbf0(wide_resource() + b"\0")

    def test_unknown_record_type_fails_closed(self):
        payload = (
            b"RBF0"
            + new_record(0, "Root", 0, struct.pack("<HHH", 0, 0, 0))
            + new_record(1, "unknown", 0x7F)
        )
        with self.assertRaisesRegex(InvalidResource, "unknown record type"):
            parse_rbf0(payload)

    def test_archive_scan_joins_split_rbf0_zlib_streams(self):
        resource = wide_resource()
        split = len(resource) // 2
        archive = b"9FPR" + struct.pack("<III", 64, 48, 1) + bytes(48)
        archive += zlib.compress(resource[:split])
        archive += zlib.compress(resource[split:])
        archive += zlib.compress(b"u" * 100_000)
        with TemporaryDirectory() as directory:
            path = Path(directory) / "sample.rpf"
            path.write_bytes(archive)
            result = scan_archive(path, match="hello")
            selected = scan_archive(path, match="", offset=64)

        self.assertEqual(result.zlib_streams, 3)
        self.assertEqual(len(result.resources), 1)
        self.assertEqual(result.resources[0].offset, 64)
        self.assertEqual(result.resources[0].chunks, 2)
        self.assertEqual(result.resources[0].parsed.root_name, "Root")
        self.assertEqual(result.resources[0].parsed.matches, ("hello\0",))
        self.assertEqual(result.errors, ())
        self.assertEqual(selected.zlib_streams, 2)
        self.assertEqual(len(selected.resources), 1)
        self.assertIn("Root", selected.resources[0].parsed.matches)
        self.assertIn("hello\0", selected.resources[0].parsed.matches)
        self.assertEqual(selected.errors, ())


if __name__ == "__main__":
    unittest.main()
