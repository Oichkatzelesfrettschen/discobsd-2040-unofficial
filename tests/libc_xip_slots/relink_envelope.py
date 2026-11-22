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

"""Relink the compact diagnostic image from the qualified PIC replay and startup fixture."""

import argparse
import hashlib
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile
from envelope_imports import RECORD, import_source
from state_entry import initializer
from symbols import functions, require, separate

PROVIDER = RECORD.with_name("compact-provider.json")
PROVIDER_HEADER = struct.Struct("<4s7I32s")
ALIASES = {"flsbuf": ("fclose", "fflush"), "doprnt_float": ("__doprnt_cvt",)}


def bind_members(work, archive):
    bound = work / "bound.a"
    bound.write_bytes(archive.read_bytes())
    for stem, names in ALIASES.items():
        member = stem + ".o"
        raw = work / (stem + "-input.o")
        raw.write_bytes(subprocess.check_output(["arm-none-eabi-ar", "p", archive, member]))
        separate(raw, work / member, [(n,) for n in names])
        subprocess.run(["arm-none-eabi-ar", "r", bound, member], cwd=work, check=True)
    return bound


def pack_provider(work, source, record):
    with (work / "slot.elf").open("rb") as stream:
        elf = ELFFile(stream)
        flash, state = [seg for seg in elf.iter_segments() if seg["p_type"] == "PT_LOAD"]
        data = bytearray(flash.data())
        require(
            data[: PROVIDER_HEADER.size] == bytes(PROVIDER_HEADER.size)
            and flash["p_vaddr"] == flash["p_paddr"] == 0x10020000
            and state["p_paddr"] == 0x10020000 + len(data),
            "provider prefix or template differs",
        )
        data.extend(state.data())
    entry = len(data)
    require(entry % 4 == 0, "provider entry alignment differs")
    (work / "entry.s").write_text(initializer(source, record))
    subprocess.run(["arm-none-eabi-as", "entry.s", "-o", "entry.o"], cwd=work, check=True)
    subprocess.run(
        [
            "arm-none-eabi-objcopy",
            "-O",
            "binary",
            "-j",
            ".text.xip_state_entry",
            "entry.o",
            "entry.bin",
        ],
        cwd=work,
        check=True,
    )
    data.extend((work / "entry.bin").read_bytes())
    data[: PROVIDER_HEADER.size] = PROVIDER_HEADER.pack(
        b"XLIB",
        1,
        0,
        PROVIDER_HEADER.size,
        len(record["slot_groups"]),
        record["slot_width_bytes"],
        record["state"],
        entry,
        bytes.fromhex(record["slot_elf_sha256"]),
    )
    require(len(data) <= 49152, "provider exceeds the flash reservation")
    require(
        hashlib.sha256(data).hexdigest() == record["package_sha256"],
        "provider package digest differs",
    )
    (work / "provider.bin").write_bytes(data)


def build_imports(work, imports, record):
    (work / "imports.S").write_text(imports + "\n")
    subprocess.run(
        ["arm-none-eabi-as", "-mcpu=cortex-m0plus", "imports.S", "-o", "imports.o"],
        cwd=work,
        check=True,
    )
    subprocess.run(
        [
            "arm-none-eabi-ld",
            "--fatal-warnings",
            f"-Ttext={0x10020000 + record.get('slot_table_offset', 0):#x}",
            "-e",
            "rand",
            "imports.o",
            "-o",
            "imports-full.elf",
        ],
        cwd=work,
        check=True,
    )
    names = {
        name: 0x10020001 + record.get("slot_table_offset", 0) + 8 * index
        for index, group in enumerate(record["slot_groups"])
        for name in group
    }
    subprocess.run(
        [
            "arm-none-eabi-objcopy",
            "--strip-all",
            *("--keep-symbol=" + n for n in names),
            "imports-full.elf",
            "imports.elf",
        ],
        cwd=work,
        check=True,
    )
    with (work / "imports.elf").open("rb") as stream:
        elf = ELFFile(stream)
        imports = functions(elf, names, 8)
        require(
            all(imports[name]["st_value"] == address for name, address in names.items()),
            "import address differs",
        )
        actual = {
            symbol.name
            for symbol in elf.get_section_by_name(".symtab").iter_symbols()
            if symbol.name
        }
        require(actual == set(names), "import symbol set differs")


def relink(archive, fixture, provider=False):
    record = json.loads(RECORD.read_text())
    if provider:
        record.update(json.loads(PROVIDER.read_text()))
    require(
        hashlib.sha256(archive.read_bytes()).hexdigest() == record["pic_archive_sha256"],
        "PIC archive digest differs",
    )
    script_bytes = (fixture / "library.ld").read_bytes()
    startup = (fixture / "startup.o").read_bytes()
    require(
        hashlib.sha256(script_bytes).hexdigest()
        == "3cfdf53ca8449c510568a4b0775773fdecab2ac51cadd2da368349c09db44371",
        "linker template digest differs",
    )
    require(
        hashlib.sha256(startup).hexdigest()
        == "ba1c821fcf941ca96476629f3f26ec7eb32d73096e9589d05ab3ca1634a7ce20",
        "startup fixture digest differs",
    )
    script = script_bytes.decode()
    require(script.count("*(.text .text.*)") == 1, "text section template differs")
    assembly = [".syntax unified", ".cpu cortex-m0plus", ".thumb"]
    entries = []
    bindings = record.get("implementation_owners", {})
    roots = [*record["roots"], *bindings.values()]
    if provider:
        require(
            record["slot_table_offset"] == PROVIDER_HEADER.size, "provider header width differs"
        )
        entries.append(f". += {PROVIDER_HEADER.size};")
    for index, group in enumerate(record["slot_groups"]):
        name = group[0]
        require(re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name), "invalid slot owner")
        assembly.extend(
            [
                f'.section .slot.{index},"ax",%progbits',
                ".thumb_func",
                f"slot_{index}:",
                "push {r3,lr}",
                "bl dispatcher",
            ]
        )
        if name == record.get("inline_trampoline"):
            entries.append("KEEP(*(.text.sigtramp)) . += 4;")
        else:
            owner = bindings.get(name, name)
            entries.append(f"KEEP(*(.slot.{index})) SHORT(({owner} - 0x10020000) | 1)")
    assembly.extend(
        [
            '.section .dispatch,"ax",%progbits',
            ".balign 4",
            ".thumb_func",
            "dispatcher:",
            "push {r0,r1}",
            "mov r0,lr",
            "subs r0,#1",
            "ldrh r0,[r0]",
            "ldr r1,1f",
            "adds r0,r1",
            "ldr r1,[sp,#12]",
            "mov lr,r1",
            "str r0,[sp,#12]",
            "pop {r0,r1,r3,pc}",
            ".balign 4",
            "1: .word 0x10020000",
        ]
    )
    for name in bindings:
        index = next(i for i, group in enumerate(record["slot_groups"]) if name in group)
        assembly.extend([f".global {name}", f".thumb_set {name},slot_{index}", f".size {name},8"])
    entries.extend(["KEEP(*(.dispatch))", "*(.text .text.*)"])
    root = Path(__file__).resolve().parents[2]
    work = Path(tempfile.mkdtemp(prefix="slot-relink-", dir=root / ".build-logs"))
    if provider:
        archive = bind_members(work, archive)
    (work / "startup.o").write_bytes(startup)
    (work / "slot.S").write_text("\n".join(assembly) + "\n")
    (work / "slot.ld").write_text(script.replace("*(.text .text.*)", " ".join(entries)))
    subprocess.run(
        ["arm-none-eabi-as", "-mcpu=cortex-m0plus", "slot.S", "-o", "slot.o"], cwd=work, check=True
    )
    libgcc = subprocess.check_output(
        [
            "arm-none-eabi-gcc",
            "-mcpu=cortex-m0plus",
            "-mthumb",
            "-mfloat-abi=soft",
            "-print-libgcc-file-name",
        ],
        text=True,
    ).strip()
    subprocess.run(
        [
            "arm-none-eabi-ld",
            "--fatal-warnings",
            "-z",
            "noexecstack",
            "--emit-relocs",
            "--gc-sections",
            "-T",
            "slot.ld",
            "-Map=slot.map",
            "-o",
            "slot.elf",
            "--whole-archive",
            archive,
            "--no-whole-archive",
            libgcc,
            "startup.o",
            "slot.o",
            *("--undefined=" + name for name in roots),
        ],
        cwd=work,
        check=True,
    )
    source = (work / "slot.elf").read_bytes()
    imports = import_source(source, record)
    if provider:
        pack_provider(work, source, record)
    build_imports(work, imports, record)
    return work


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pic_archive", type=Path)
    parser.add_argument("link_fixture", type=Path)
    parser.add_argument(
        "--provider", action="store_true", help="emit the header-bearing diagnostic flash image"
    )
    args = parser.parse_args()
    print(relink(args.pic_archive.resolve(), args.link_fixture.resolve(), args.provider))
