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

import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from xip_imports import HEADER, RECORD, ROOT, TOOLS, link, require


def call_targets(code, slots):
    """Follow each Thumb BL and the literal load in its aligned veneer."""
    destinations = {}
    for index, slot in enumerate(slots):
        pc = 4 * index
        high, low = struct.unpack_from("<HH", code, pc)
        require(high & 0xF800 == 0xF000 and low & 0xF800 == 0xF800, "expected Thumb BL")
        displacement = ((high & 0x7FF) << 12) | ((low & 0x7FF) << 1)
        if displacement & 0x400000:
            displacement -= 0x800000
        veneer = pc + 4 + displacement
        require(veneer % 4 == 0 and 0 <= veneer <= len(code) - 12, "invalid veneer extent")
        require(
            code[veneer : veneer + 8] == bytes.fromhex("18b4014b019308bd"),
            "veneer instructions differ",
        )
        (target,) = struct.unpack_from("<I", code, veneer + 8)
        require(target == slot, "veneer slot differs")
        require(destinations.setdefault(slot, veneer) == veneer, "alias veneers differ")
    return destinations


def reject_destination(code, slots):
    try:
        call_targets(code, slots)
    except ValueError as error:
        require(str(error) == "veneer slot differs", "wrong destination control diagnostic")
    else:
        raise ValueError("accepted wrong veneer destination")


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
    slots = expected[4:]
    code = image[HEADER.size : HEADER.size + text]
    destinations = call_targets(code, slots)
    first, second = list(destinations)[:2]
    changed = bytearray(code)
    struct.pack_into("<I", changed, destinations[first] + 8, second)
    reject_destination(changed, slots)
    changed = bytearray(code)
    displacement = destinations[second] - 4
    struct.pack_into(
        "<HH",
        changed,
        0,
        0xF000 | (displacement >> 12 & 0x7FF),
        0xF800 | (displacement >> 1 & 0x7FF),
    )
    reject_destination(changed, slots)
    print(f"PASS native common, segment ends and {len(names)} callable imports")


def state_replay(path):
    record = json.loads(RECORD.read_text())
    names = [*record["objects"], "environ", "__progname"]
    listing = subprocess.check_output(["readelf", "-sW", path], text=True)
    values = {
        fields[-1]: int(fields[1], 16)
        for line in listing.splitlines()
        if len(fields := line.split()) == 8 and fields[0][:-1].isdigit()
    }
    work = Path(tempfile.mkdtemp(prefix="native-state-replay-", dir=ROOT / ".build-logs"))
    padding = set()
    for extra in (0, 4):
        source = [
            ".syntax unified",
            ".thumb",
            ".text",
            ".global _start",
            ".thumb_func",
            "_start:",
            "bx lr",
            ".data",
        ]
        source.extend(".word " + name for name in [*names, "_end", "owner", "local_bss"])
        source.extend([".bss", "local_bss:", f".space {4 + extra}", ".comm owner,5"])
        slot_index = 1 if extra else len(record["slot_groups"]) - 1
        function = record["slot_groups"][slot_index][0]
        source.extend([".data", ".word " + function])
        (work / "app.s").write_text("\n".join(source) + "\n")
        subprocess.run([TOOLS / "as", "app.s", "-o", "app.o"], cwd=work, check=True)
        image = link([work / "app.o"], path).read_bytes()
        magic, text, data, bss, *_ = HEADER.unpack_from(image)
        require(magic == 0o407 | (2 << 26), "shared image flag differs")
        wire = struct.Struct("<4sII32sIII")
        signature, version, size, digest, slots, state, state_size = wire.unpack_from(image, 32)
        require(
            (signature, version, size, digest.hex(), slots)
            == (b"XREQ", 1, 56, hashlib.sha256(path.read_bytes()).hexdigest(), slot_index + 1),
            "shared image identity or pointer-only requirement differs",
        )
        app_bss = 0x20000000 + text + data
        # The final native linker assigns common storage before input BSS.
        after_app = app_bss + 8 + 4 + extra
        base = (after_app + 7) & ~7
        padding.add(base - after_app)
        require((state, state_size) == (base, values["__libc_state_size"]), "state record differs")
        end = base + values["__libc_state_size"]
        require(app_bss + bss == end, "heap boundary differs")
        expected = [values[n] + (0 if n == "_ctype_" else base) for n in names]
        expected.extend([end, app_bss, app_bss + 8])
        expected.append(values["slot_0"] + 8 * (slots - 1))
        pointers = struct.unpack_from(f"<{len(expected)}I", image, HEADER.size + size + text)
        require(list(pointers) == expected, "linked state pointers differ")
    require(padding == {0, 4}, "state padding coverage differs")
    (work / "conflict.s").write_text(".bss\n.global environ\nenviron:\n.space 4\n")
    subprocess.run([TOOLS / "as", "conflict.s", "-o", "conflict.o"], cwd=work, check=True)
    bad = work / "wrong.elf"
    bad.write_bytes(path.read_bytes() + bytes(1))
    for inputs, elf, expected_error in [
        (["app.o", "conflict.o"], path, "application defines a shared state symbol"),
        (["app.o"], bad, "state image digest differs"),
    ]:
        try:
            link([work / name for name in inputs], elf)
        except ValueError as error:
            require(str(error) == expected_error, "wrong state control diagnostic")
        else:
            raise ValueError("accepted " + expected_error)
    print(f"PASS {len(names)} object pointers, both state alignments and two rejection controls")


def provider_replay(path, package):
    from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, UC_PROT_EXEC, UC_PROT_READ, Uc
    from unicorn.arm_const import (
        UC_ARM_REG_LR,
        UC_ARM_REG_PC,
        UC_ARM_REG_R0,
        UC_ARM_REG_SP,
        UC_CPU_ARM_CORTEX_M0,
    )

    provider = package.read_bytes()
    record = json.loads(RECORD.with_name("compact-provider.json").read_text())
    require(
        hashlib.sha256(provider).hexdigest() == record["package_sha256"], "package digest differs"
    )
    tag, major, minor, header, slots, width, state, entry, identity = struct.unpack_from(
        "<4s7I32s", provider
    )
    require(
        (tag, major, minor, header, width) == (b"XLIB", 1, 0, 64, 8)
        and header + slots * width <= entry < len(provider) <= 49152
        and identity == hashlib.sha256(path.read_bytes()).digest(),
        "package identity or extent differs",
    )
    work = Path(tempfile.mkdtemp(prefix="native-provider-", dir=ROOT / ".build-logs"))
    (work / "start.s").write_text(""".syntax unified
.thumb
.text
.global _start
.thumb_func
_start:
    push {r4,lr}
    ldr r0,=__xip_libc_state
    ldr r3,=0x10020000
    ldr r1,[r3,#28]
    adds r3,r3,r1
    adds r3,#1
    blx r3
    mov r9,r0
    movs r0,#0
    bl sbrk
    ldr r1,=_end
    cmp r0,r1
    bne wrong
    bl rand
    pop {r4,pc}
wrong:
    movs r0,#0
    pop {r4,pc}
    .pool
""")
    subprocess.run([TOOLS / "as", "start.s", "-o", "start.o"], cwd=work, check=True)
    image = link([work / "start.o"], path)
    packed = work / "shared.hsx"
    expanded = work / "expanded.aout"
    subprocess.run([TOOLS / "hsaout", "-c", image, packed], check=True)
    subprocess.run([TOOLS / "hsaout", "-d", packed, expanded], check=True)
    ram = 0x20000000
    for executable in (image, expanded):
        content = executable.read_bytes()
        _, text, data, bss, _, _, _, start = HEADER.unpack_from(content)
        sig, version, size, digest, prefix, base, extent = struct.unpack_from(
            "<4sII32sIII", content, HEADER.size
        )
        require(
            (sig, version, size, digest, extent) == (b"XREQ", 1, 56, identity, state)
            and 0 < prefix <= slots
            and base + extent == ram + text + data + bss,
            "caller requirement differs",
        )
        machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        machine.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
        machine.mem_map(0x10020000, 49152)
        machine.mem_write(0x10020000, provider)
        machine.mem_protect(0x10020000, 49152, UC_PROT_READ | UC_PROT_EXEC)
        machine.mem_map(ram, 0x30000)
        machine.mem_write(ram, content[HEADER.size + size : HEADER.size + size + text + data])
        machine.mem_write(base, bytes([0xA5]) * extent)
        machine.reg_write(UC_ARM_REG_SP, ram + 0x23000)
        machine.reg_write(UC_ARM_REG_LR, ram + 0x24001)
        machine.emu_start(start, ram + 0x24000, count=100000)
        require(
            machine.reg_read(UC_ARM_REG_PC) == ram + 0x24000
            and machine.reg_read(UC_ARM_REG_R0) == 16838,
            "provider startup, break or rand differs",
        )
    print(f"PASS raw and expanded native callers, {len(provider)} provider bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-image", type=Path)
    parser.add_argument("--provider", type=Path)
    args = parser.parse_args()
    if args.provider and not args.state_image:
        parser.error("--provider requires --state-image")
    main()
    if args.state_image:
        state_replay(args.state_image)
    if args.provider:
        provider_replay(args.state_image, args.provider)
