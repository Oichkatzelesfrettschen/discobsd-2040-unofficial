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

"""Execute compact libc startup at two SRAM addresses and compare the state bytes."""

import argparse
import io
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile
from envelope_imports import BASE, RECORD
from state_entry import initializer
from symbols import require, state_relocations
from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, UC_PROT_EXEC, UC_PROT_READ, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R4,
    UC_ARM_REG_R5,
    UC_ARM_REG_R9,
    UC_ARM_REG_SP,
    UC_CPU_ARM_CORTEX_M0,
)


def replay(path):
    source = path.read_bytes()
    record = json.loads(RECORD.read_text())
    assembly = initializer(source, record)
    with tempfile.TemporaryDirectory() as name:
        work = Path(name)
        (work / "entry.s").write_text(assembly)
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
        routine = (work / "entry.bin").read_bytes()
    elf = ELFFile(io.BytesIO(source))
    code, state = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    template = state.data()
    extent = state["p_memsz"]
    entry = state["p_paddr"] + len(template)
    require(entry % 4 == 0 and entry + len(routine) <= BASE + 49152, "entry leaves flash")
    got = elf.get_section_by_name(".got")
    cells = state_relocations(elf, got, len(template))
    table = struct.pack(f"<{len(cells)}H", *cells)
    require(routine.count(table) == 1, "offset table identity differs")
    damaged = bytearray(routine)
    offset = routine.index(table)
    damaged[offset : offset + 2] = table[2:4]
    for program, valid in [(routine, True), (bytes(damaged), False)]:
        machine = Uc(UC_ARCH_ARM, UC_MODE_MCLASS | UC_MODE_THUMB)
        machine.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
        machine.mem_map(BASE, 49152)
        machine.mem_write(BASE, code.data())
        machine.mem_write(state["p_paddr"], template)
        machine.mem_write(entry, program)
        machine.mem_protect(BASE, 49152, UC_PROT_READ | UC_PROT_EXEC)
        machine.mem_map(0x20000000, 0x30000)
        for base in (0x20001000, 0x20005000):
            expected = bytearray(template).ljust(extent, bytes(1))
            for cell in cells:
                value = struct.unpack_from("<I", template, cell)[0]
                struct.pack_into("<I", expected, cell, base + value)
            machine.mem_write(base - 4, b"G" * 4 + bytes([0xA5]) * extent + b"G" * 4)
            preserved = [(UC_ARM_REG_SP, 0x20021000), (UC_ARM_REG_R4, 13), (UC_ARM_REG_R5, 17)]
            for reg, val in [*preserved, (UC_ARM_REG_R0, base), (UC_ARM_REG_LR, 0x20024001)]:
                machine.reg_write(reg, val)
            machine.emu_start(entry | 1, 0x20024000, count=100000)
            for reg, val in [
                *preserved,
                (UC_ARM_REG_R0, base + got["sh_addr"]),
                (UC_ARM_REG_PC, 0x20024000),
            ]:
                require(machine.reg_read(reg) == val, "entry return differs")
            actual = bytes(machine.mem_read(base - 4, extent + 8))
            require(actual[:4] == actual[-4:] == b"G" * 4, "state guard differs")
            require(
                (actual[4:-4] == expected) == valid, "state bytes differ from control prediction"
            )
            if valid:
                slot = next(i for i, group in enumerate(record["slot_groups"]) if "rand" in group)
                machine.reg_write(UC_ARM_REG_R9, machine.reg_read(UC_ARM_REG_R0))
                machine.reg_write(UC_ARM_REG_LR, 0x20024001)
                machine.emu_start(
                    (BASE + slot * record["slot_width_bytes"]) | 1, 0x20024000, count=4096
                )
                require(
                    machine.reg_read(UC_ARM_REG_PC) == 0x20024000
                    and machine.reg_read(UC_ARM_REG_R0) == 16838,
                    "first rand call differs",
                )
    print(f"PASS two cold starts and duplicate offset control; {len(routine)} flash bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    replay(parser.parse_args().image)
