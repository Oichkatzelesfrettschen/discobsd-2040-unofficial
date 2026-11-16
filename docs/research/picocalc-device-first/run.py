# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""Cortex-M0+ cycle estimate of the kernel's swap encode and decode.

usage: run.py BENCH_ELF SH_AOUT AWK_AOUT

The cost table is the one docs/research/rp2040-second-core-emulation/cycles.py
uses: ALU 1, LDR/STR 2, LDM/STM/PUSH/POP 1+N (+2 for POP {pc}), taken branch
2, branch not taken 1, BL/BX/BLX 3, MRS/MSR/barriers 4. Instruction fetch is
charged one cycle, so code running from the XIP cache can only be slower.
Each input runs a counting encode, a writing encode into a buffer of the
counted length and a decode, which must reproduce the input byte for byte.
"""

import hashlib
import os
import random
import struct
import subprocess
import sys
import tempfile

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
from capstone.arm_const import ARM_OP_REG, ARM_REG_PC
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_R3,
    UC_ARM_REG_SP,
)

SRC, DST, OUT = 0x30000000, 0x30400000, 0x30800000
SENTINEL = 0x20F00000
CLOCK_HZ = 125e6
# build.sh passes the binutils that match its compiler prefix.
NM = os.environ.get("NM_ARM", "arm-none-eabi-nm")
OBJCOPY = os.environ.get("OBJCOPY_ARM", "arm-none-eabi-objcopy")


def load(elf):
    """Return the image bytes and the Thumb text symbols of elf."""
    syms = {}
    nm = subprocess.check_output([NM, "-n", elf], text=True)
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tT":
            syms[parts[2]] = int(parts[0], 16)
    fd, tmp = tempfile.mkstemp()
    os.close(fd)
    subprocess.check_call([OBJCOPY, "-O", "binary", elf, tmp])
    with open(tmp, "rb") as handle:
        img = handle.read()
    os.unlink(tmp)
    return img, syms


CS = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
CS.detail = True
COST = {}


def cost(mu, pc, size):
    """Cycles of the instruction at pc, or "b" for a branch resolved later."""
    if pc in COST:
        return COST[pc]
    ins = next(iter(CS.disasm(bytes(mu.mem_read(pc, size)), pc)))
    m = ins.mnemonic
    if m.startswith(("ldm", "stm", "push", "pop")):
        c = 1 + len(ins.operands) - (1 if m.startswith(("ldm", "stm")) else 0)
        if m.startswith("pop") and any(
            o.type == ARM_OP_REG and o.reg == ARM_REG_PC for o in ins.operands
        ):
            c += 2
    elif m.startswith(("ldr", "str")):
        c = 2
    elif m in ("bl", "bx", "blx"):
        c = 3
    elif m.startswith("b") and m != "bics":
        c = "b"
    elif m in ("mrs", "msr", "dsb", "isb", "dmb"):
        c = 4
    else:
        c = 1
    COST[pc] = c
    return c


def call(img, syms, fn, blob, args):
    """Run fn(args) with blob at SRC; return r0, cycles and the emulator."""
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    mu.mem_map(0x20000000, 0x1000000)
    mu.mem_map(0x30000000, 0x1000000)
    mu.mem_write(0x20000000, img)
    mu.mem_write(SRC, blob)
    mu.reg_write(UC_ARM_REG_SP, 0x200F0000)
    mu.reg_write(UC_ARM_REG_LR, SENTINEL | 1)
    regs = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
    for reg, value in zip(regs, args):
        mu.reg_write(reg, value)
    total, prev = [0], [None]

    def hook(emu, addr, size, _data):
        if prev[0] is not None and prev[0][2] == "b":
            total[0] += 2 if addr != prev[0][0] + prev[0][1] else 1
        c = cost(emu, addr, size)
        if c != "b":
            total[0] += c
        prev[0] = (addr, size, c)

    mu.hook_add(UC_HOOK_CODE, hook)
    mu.emu_start(syms[fn] | 1, SENTINEL, count=2_000_000_000)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    if r0 & 0x80000000:
        r0 -= 1 << 32
    return r0, total[0], mu


def data_segment(path):
    """a_data from the a.out followed by a_bss zeros, as swapout sees it."""
    with open(path, "rb") as handle:
        raw = handle.read()
    _, text, data, bss = struct.unpack_from("<4I", raw, 0)
    return raw[32 + text : 32 + text + data] + bytes(bss)


def main():
    elf, sh, awk = sys.argv[1:4]
    img, syms = load(elf)
    stack = bytearray(4096)
    for i in range(4096 - 512, 4096):
        stack[i] = 0x20 + (i % 64)
    uarea = bytearray(3072)
    for i in range(384):
        uarea[i] = (i * 7 + (i >> 3)) & 0xFF
    rng = random.Random(1)
    inputs = [
        ("sh data+bss", data_segment(sh)),
        ("awk data+bss", data_segment(awk)),
        ("stack 4 KB synthetic", bytes(stack)),
        ("u area 3 KB synthetic", bytes(uarea)),
        ("zeros 4 KB", bytes(4096)),
        ("random 4 KB", bytes(rng.getrandbits(8) for _ in range(4096))),
    ]
    for path in (sh, awk):
        with open(path, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        print(f"# input {os.path.basename(path)} sha256 {digest}")
    print(
        "segment\traw\tcompressed\tcount_cycles\twrite_cycles\t"
        "decode_cycles\tencode_cycles_per_byte\tdecode_cycles_per_byte\t"
        "encode_ms\tdecode_ms\tround_trip"
    )
    failed = []
    for name, raw in inputs:
        n, c_count, _ = call(img, syms, "enc", raw, (SRC, len(raw), 0, 0))
        m, c_write, mu = call(img, syms, "enc", raw, (SRC, len(raw), DST, n))
        packed = bytes(mu.mem_read(DST, m)) if m > 0 else b""
        d, c_dec, mu2 = call(img, syms, "dec", packed, (SRC, len(packed), OUT, len(raw)))
        ok = n == m and d == len(raw) and bytes(mu2.mem_read(OUT, len(raw))) == raw
        if not ok:
            failed.append(name)
        enc = c_count + c_write
        print(
            f"{name}\t{len(raw)}\t{m}\t{c_count}\t{c_write}\t{c_dec}\t"
            f"{enc / len(raw):.1f}\t{c_dec / len(raw):.1f}\t"
            f"{enc / CLOCK_HZ * 1e3:.2f}\t{c_dec / CLOCK_HZ * 1e3:.2f}\t"
            f"{'ok' if ok else 'FAIL'}"
        )
    if failed:
        print("run.py: round trip failed for " + ", ".join(failed), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
