# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
# Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
# The Original Code is AnyXboxSeries.
# The Original Developer is the Initial Developer.
# The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
#

"""Exercise the public shader tools end to end with an original RDNA 2 fixture."""

import subprocess
import sys
import tempfile
from pathlib import Path

from shader_container import inspect_shader, read_shader
from test_prepare_shader import CODE, CODE_OFFSET, FSSM, xdat_shader


def main() -> int:
    probe, dxc, *options = sys.argv[1:]
    tool = Path(__file__).with_name("prepare_shader.py")
    with tempfile.TemporaryDirectory(prefix="wsx shader ") as folder:
        source = Path(folder)
        container = source / "fixture.dxbc"
        fssm = source / "fixture.fssm"
        hlsl = source / "fixture.hlsl"
        dxil = source / "fixture.dxil"
        container.write_bytes(xdat_shader())
        subprocess.run([sys.executable, tool, "unpack", container, fssm], check=True)
        if fssm.read_bytes() != FSSM:
            raise RuntimeError("unexpected unpacked bytes")
        subprocess.run([sys.executable, tool, "lift", fssm, hlsl, "--code-offset",
                        str(CODE_OFFSET), "--code-size", str(len(CODE))], check=True)

        for i, (s0, s1, expected) in enumerate(((7, 5, 38), (0xFFFFFFFF, 2, 33))):
            command = [probe, "--dxc-dir", dxc, "--compute", hlsl,
                       "--inputs", str(s0), str(s1), "--expect", str(expected), *options]
            if i == 0:
                command += ["--dxil", dxil]
            status = subprocess.run(command).returncode
            if status != 0:
                return status
        program = next(p for p in inspect_shader(read_shader(dxil))["parts"] if p["tag"] == "DXIL")
        if not program["has_dxil_bitcode"] or program["shader_model"] != "6.6":
            raise RuntimeError("expected desktop DXIL shader model 6.6")
    return 0


if __name__ == "__main__":
    sys.exit(main())
