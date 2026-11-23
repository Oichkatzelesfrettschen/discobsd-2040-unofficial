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

"""Cross-link a C caller with the compact provider and its process-local objects."""

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from xip_imports import RECORD, ROOT, TOOLS, object_layout, require

sys.path.insert(0, str(ROOT / "tests/libc_xip_slots"))
from envelope_imports import import_source  # noqa: E402
from relink_envelope import build_imports  # noqa: E402


def compile_caller(work, source):
    compiler = [
        "arm-none-eabi-gcc",
        "-Os",
        "-mcpu=cortex-m0plus",
        "-mthumb",
        "-mfloat-abi=soft",
        "-ffixed-r9",
        "-ffreestanding",
        "-fno-builtin",
        "-fshort-enums",
        "-std=gnu17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-nostdinc",
        "-I" + str(ROOT / "distrib/obj/destdir.rp2040/usr/include"),
    ]
    startup = ROOT / "lib/startup-arm"
    for path, obj in (
        (source, "app.o"),
        (startup / "crt0.c", "crt.o"),
        (startup / "crt0-xip.S", "entry.o"),
    ):
        flags = [] if path == source else ["-DDISCOBSD_SHARED_LIBC"]
        subprocess.run([*compiler, *flags, "-c", path, "-o", obj], cwd=work, check=True)
    subprocess.run(
        [
            "arm-none-eabi-ld",
            "--fatal-warnings",
            "-z",
            "noexecstack",
            "-r",
            "entry.o",
            "crt.o",
            "app.o",
            "-o",
            "caller.o",
        ],
        cwd=work,
        check=True,
    )


def linker_fragment(record, image, references):
    layout = object_layout(image, record)
    slots = max(
        (i + 1 for i, group in enumerate(record["slot_groups"]) if references.intersection(group)),
        default=0,
    )
    digest = " ".join(f"BYTE({byte})" for byte in hashlib.sha256(image).digest())
    state = record["state"]
    aliases = []
    for name, (address, _, writable) in layout.items():
        value = f"__xip_libc_state + {address}" if writable else hex(address)
        aliases.append(f"{name} = {value};")
    return (
        f"""PHDRS {{ requirement 0x64787271 FLAGS(4); }}
SECTIONS {{
  .discobsd.xreq 0 (INFO) : {{
    LONG(0x51455258) LONG(1) LONG(56)
    {digest}
    LONG({slots}) LONG(__xip_libc_state) LONG({state})
  }} :requirement
}}
INSERT AFTER .ARM.attributes;
SECTIONS {{
  .xstate (NOLOAD) : ALIGN(8) {{ __xip_libc_state = .; . += {state}; }} :data
}}
INSERT AFTER .bss;
__xip_libc_start = 0x10020000;
ASSERT(_end == __xip_libc_state + {state}, "shared state must end at the break")
"""
        + "\n".join(aliases)
        + "\n"
    )


def cross_link(work, source, slot):
    record = json.loads(RECORD.read_text())
    record.update(json.loads(RECORD.with_name("compact-provider.json").read_text()))
    image = slot.read_bytes()
    build_imports(work, import_source(image, record), record)
    compile_caller(work, source)
    defined = subprocess.check_output(
        ["arm-none-eabi-nm", "-gj", "--defined-only", "caller.o"], cwd=work, text=True
    ).splitlines()
    reserved = {name for group in record["slot_groups"] for name in group}
    reserved.update(
        [*record["objects"], "environ", "__progname", "__xip_libc_state", "__xip_libc_start"]
    )
    require(not reserved.intersection(defined), "caller defines a reserved provider symbol")
    refs = subprocess.check_output(
        ["arm-none-eabi-nm", "-uj", "caller.o"], cwd=work, text=True
    ).splitlines()
    (work / "shared.ld").write_text(linker_fragment(record, image, set(refs)))
    subprocess.run(
        [
            "arm-none-eabi-ld",
            "--fatal-warnings",
            "-z",
            "noexecstack",
            "-T",
            "shared.ld",
            "-T",
            ROOT / "lib/elf32-arm.ld",
            "--just-symbols=imports.elf",
            "caller.o",
            "-o",
            "app.elf",
        ],
        cwd=work,
        check=True,
    )
    subprocess.run([TOOLS / "elf2aout", "app.elf", "app.aout"], cwd=work, check=True)
    subprocess.run([TOOLS / "hsaout", "-c", "app.aout", "app.hsx"], cwd=work, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("slot_elf", type=Path)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="cross-xip-", dir=ROOT / ".build-logs"))
    cross_link(work, args.source.resolve(strict=True), args.slot_elf.resolve(strict=True))
    print(work)
