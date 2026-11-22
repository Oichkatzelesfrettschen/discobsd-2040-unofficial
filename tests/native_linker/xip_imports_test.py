# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Check final-link segment boundaries and every compact slot in one native caller."""

import json
import struct
import subprocess
import tempfile
from pathlib import Path

from xip_imports import HEADER, RECORD, ROOT, TOOLS, link, require


def main():
    groups = json.loads(RECORD.read_text())["slot_groups"]
    names = [name for group in groups for name in group]
    source = [".syntax unified", ".thumb", ".text", ".global _start", ".thumb_func", "_start:"]
    source.extend("bl " + name for name in names)
    source.extend(
        [
            "bx lr",
            ".data",
            ".word _etext",
            ".word _edata",
            ".word _end",
            ".word owner",
            ".comm owner,12",
        ]
    )
    source.extend(".word " + name for name in names)
    work = Path(tempfile.mkdtemp(prefix="native-slot-boundaries-", dir=ROOT / ".build-logs"))
    (work / "caller.s").write_text("\n".join(source) + "\n")
    subprocess.run([TOOLS / "as", "caller.s", "-o", "caller.o"], cwd=work, check=True)
    image = link([work / "caller.o"]).read_bytes()
    magic, text, data, bss, rt, rd, _, entry = HEADER.unpack_from(image)
    base = 0x20000000
    require(
        (magic, data, bss, rt, rd, entry) == (0o407, 16 + 4 * len(names), 12, 0, 0, base + 1),
        "native header differs",
    )
    end_text = base + text
    end_data = end_text + data
    expected = [end_text, end_data, end_data + bss, end_data]
    expected.extend(0x10020001 + 8 * i for i, group in enumerate(groups) for _ in group)
    actual = struct.unpack_from(f"<{len(expected)}I", image, HEADER.size + text)
    require(list(actual) == expected, "native boundary or function pointer differs")
    print(f"PASS native common, segment ends and {len(names)} callable imports")


if __name__ == "__main__":
    main()
