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

"""Emit a Thumb cold-start routine and relocation offsets for the compact diagnostic ELF."""

import argparse
import hashlib
import io
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from envelope_imports import BASE, image_record
from symbols import require, state_relocations


def initializer(source, record):
    require(hashlib.sha256(source).hexdigest() == record["slot_elf_sha256"], "image digest differs")
    elf = ELFFile(io.BytesIO(source))
    flash, state = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    template = state["p_paddr"]
    initialized = state["p_filesz"]
    extent = state["p_memsz"]
    got = elf.get_section_by_name(".got")
    require(
        flash["p_paddr"] == flash["p_vaddr"] == BASE
        and template == BASE + flash["p_filesz"]
        and state["p_vaddr"] == 0
        and 0 < initialized <= extent == record["state"]
        and got["sh_addr"] + got["sh_size"] <= initialized,
        "compact process state layout differs",
    )
    cells = state_relocations(elf, got, initialized)
    require(
        cells
        and all(
            off <= 65535 and struct.unpack_from("<I", state.data(), off)[0] <= extent
            for off in cells
        ),
        "process pointer extent differs",
    )
    # The caller supplies the aligned state base in r0 and installs the returned GOT in r9.
    return f""".syntax unified
.cpu cortex-m0plus
.thumb
.section .text.xip_state_entry,"ax",%progbits
.balign 4
.global xip_state_entry
.thumb_func
xip_state_entry:
    push {{r4,r5}}
    mov r1,r0
    ldr r2,={template}
    ldr r3,={initialized}
1:
    ldrb r4,[r2]
    strb r4,[r1]
    adds r1,#1
    adds r2,#1
    subs r3,#1
    bne 1b
    ldr r3,={extent - initialized}
    movs r4,#0
    cmp r3,#0
    beq 3f
2:
    strb r4,[r1]
    adds r1,#1
    subs r3,#1
    bne 2b
3:
    adr r2,offsets
    ldr r3,={len(cells)}
4:
    ldrh r4,[r2]
    ldr r5,[r0,r4]
    adds r5,r5,r0
    str r5,[r0,r4]
    adds r2,#2
    subs r3,#1
    bne 4b
    ldr r1,={got["sh_addr"]}
    adds r0,r0,r1
    pop {{r4,r5}}
    bx lr
    .ltorg
    .balign 4
offsets:
    .hword {",".join(map(str, cells))}
"""


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    source = parser.parse_args().image.read_bytes()
    print(initializer(source, image_record(source)), end="")
