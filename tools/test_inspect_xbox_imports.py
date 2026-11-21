# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
# Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
# The Original Code is AnyXboxSeries.
# The Original Developer is the Initial Developer.
# The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
#

import unittest

from inspect_xbox_imports import Import, parse_exports


class ExportTableTests(unittest.TestCase):
    def test_named_forwarded_and_ordinal_only_exports(self):
        exports = parse_exports("""
File Type: DLL

    ordinal hint RVA      name

          1    0 00001350 D3D12CreateRootSignatureDeserializer
          2    1          ProbeNamed (forwarded to KERNEL32.Sleep)
         19               [NONAME] (forwarded to KERNEL32.GetTickCount)
         23      00001900 [NONAME]

  Summary
""")
        self.assertIs(exports.forwarder(
            Import("game.exe", "sample.dll", "normal", name="D3D12CreateRootSignatureDeserializer", hint=19)
        ), False)
        self.assertIs(exports.forwarder(
            Import("game.exe", "sample.dll", "delay-load", name="ProbeNamed", hint=99)
        ), True)
        self.assertIs(exports.forwarder(Import("game.exe", "sample.dll", "normal", ordinal=2)), True)
        self.assertIs(exports.forwarder(Import("game.exe", "sample.dll", "normal", ordinal=19)), True)
        self.assertIs(exports.forwarder(Import("game.exe", "sample.dll", "normal", ordinal=23)), False)
        self.assertIsNone(exports.forwarder(Import("game.exe", "sample.dll", "normal", ordinal=0)))
        self.assertIsNone(exports.forwarder(
            Import("game.exe", "sample.dll", "normal", name="probenamed", hint=1)
        ))

    def test_executable_cannot_be_compared_as_a_shim(self):
        with self.assertRaisesRegex(ValueError, "not a PE DLL"):
            parse_exports("File Type: EXECUTABLE IMAGE\n  Summary\n")


if __name__ == "__main__":
    unittest.main()
