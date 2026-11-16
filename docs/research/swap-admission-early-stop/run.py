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
"""Cycle cost and verdicts of the SwapRAM admission rules in rules.c.

usage: run.py RULES_ELF SH_AOUT AWK_AOUT CORPUS_LIST OUTDIR

Writes OUTDIR/run-output.tsv, one row per input and rule with the cycles
the rule spends at 125 MHz, and OUTDIR/corpus.tsv, the verdict of every
rule on the data+bss segment of every a.out CORPUS_LIST names (one path
per line), with the counts of false refusals and misses at its end.

The cost table is the one docs/research/rp2040-second-core-emulation/cycles.py
uses: ALU 1, LDR/STR 2, LDM/STM/PUSH/POP 1+N (+2 for POP {pc}), taken
branch 2, branch not taken 1, BL/BX/BLX 3, MRS/MSR/barriers 4, every fetch
one cycle. The corpus runs without the cost hook: its verdicts come from the
same Thumb code, so they need no cycle count.

The reference verdict of an input is the counting pass's: refuse when the
whole input compresses above 15/16 of its length. A rule refuses falsely
when it refuses an input the reference admits, and misses when it admits
one the reference refuses. run.py exits 1 when enc_stop() with both exits
disabled disagrees with enc(), or when the 1 KB budget does not stop the
4 KB random input before the full counting pass ends.
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

SRC, AUX = 0x30000000, 0x30800000
SENTINEL = 0x20F00000
CLOCK_HZ = 125e6
NONE = 0xFFFFFFFF
NM = os.environ.get("NM_ARM", "arm-none-eabi-nm")
OBJCOPY = os.environ.get("OBJCOPY_ARM", "arm-none-eabi-objcopy")
PROBES = (64, 128, 256, 512, 1024)
RATIOS = (256, 512, 1024)
BUDGETS = (1024, 4096)
HISTS = (256, 1024)
# Entropy thresholds in sixteenths of a bit: btrfs's ENTROPY_LVL_HIGH (80
# percent of 8 bits) and 7 bits.
H_LIMITS = ((103, "6.4b"), (112, "7.0b"))


def load(elf):
    """Return the image bytes and the Thumb text symbols of elf."""
    syms = {}
    for line in subprocess.check_output([NM, "-n", elf], text=True).splitlines():
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


class Machine:
    """One emulator holding the rules image; call() runs one function."""

    def __init__(self, img, syms, counting):
        self.syms = syms
        self.mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.mu.mem_map(0x20000000, 0x1000000)
        self.mu.mem_map(0x30000000, 0x1000000)
        self.mu.mem_write(0x20000000, img)
        self.total = [0]
        self.prev = [None]
        if counting:
            self.mu.hook_add(UC_HOOK_CODE, self.hook)

    def hook(self, emu, addr, size, _data):
        prev = self.prev[0]
        if prev is not None and prev[2] == "b":
            self.total[0] += 2 if addr != prev[0] + prev[1] else 1
        c = cost(emu, addr, size)
        if c != "b":
            self.total[0] += c
        self.prev[0] = (addr, size, c)

    def load_input(self, blob):
        self.mu.mem_write(SRC, blob)

    def call(self, fn, args):
        mu = self.mu
        mu.reg_write(UC_ARM_REG_SP, 0x200F0000)
        mu.reg_write(UC_ARM_REG_LR, SENTINEL | 1)
        regs = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
        for reg, value in zip(regs, args):
            mu.reg_write(reg, value)
        self.total[0] = 0
        self.prev[0] = None
        mu.emu_start(self.syms[fn] | 1, SENTINEL)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        if r0 & 0x80000000:
            r0 -= 1 << 32
        return r0, self.total[0]


def data_segment(path):
    """a_data from the a.out followed by a_bss zeros, as swapout sees it."""
    with open(path, "rb") as handle:
        raw = handle.read()
    _, text, data, bss = struct.unpack_from("<4I", raw, 0)
    return raw[32 + text : 32 + text + data] + bytes(bss)


def synthetic():
    """The model inputs of picocalc-device-first plus four that split rules."""
    stack = bytearray(4096)
    for i in range(4096 - 512, 4096):
        stack[i] = 0x20 + (i % 64)
    uarea = bytearray(3072)
    for i in range(384):
        uarea[i] = (i * 7 + (i >> 3)) & 0xFF
    rng = random.Random(1)
    rand4k = bytes(rng.getrandbits(8) for _ in range(4096))
    rng = random.Random(2)
    sym16 = bytes(0x40 + rng.getrandbits(4) for _ in range(4096))
    rng = random.Random(3)
    pattern = bytes(rng.getrandbits(8) for _ in range(64)) * 64
    return [
        ("stack 4 KB synthetic", bytes(stack)),
        ("u area 3 KB synthetic", bytes(uarea)),
        ("zeros 4 KB", bytes(4096)),
        ("random 4 KB", rand4k),
        ("random 1 KB then zeros 3 KB", rand4k[:1024] + bytes(3072)),
        ("zeros 3 KB then random 1 KB", bytes(3072) + rand4k[:1024]),
        ("16 symbols 4 KB", sym16),
        ("64-byte random pattern x64", pattern),
    ]


def verdicts(m, raw):
    """Rows (rule, result, refuse, cycles) for one loaded input."""
    n = len(raw)
    rows = []
    full, c = m.call("enc", (SRC, n))
    rows.append(("full count", full, full * 16 > n * 15, c))
    for lim in BUDGETS:
        r, c = m.call("enc_stop", (SRC, n, lim, 0))
        rows.append((f"E1 budget {lim}", r, r == -2, c))
    for p in RATIOS:
        r, c = m.call("enc_stop", (SRC, n, NONE, p))
        rows.append((f"R ratio from {p}", r, r == -3, c))
    for p in PROBES:
        r, c = m.call("enc_prefix", (SRC, n, p))
        rows.append((f"P prefix {p}", r, r == 1, c))
    for p in HISTS:
        r, c = m.call("entropy_q4", (SRC, n, p, AUX))
        for lim, tag in H_LIMITS:
            rows.append((f"H entropy {p} >= {tag}", r, r >= lim, c))
    r, c = m.call("same_filled", (SRC, n))
    rows.append(("S same-filled", r, False, c))
    return full, rows


def main():
    elf, sh, awk, corpus_list, outdir = sys.argv[1:6]
    img, syms = load(elf)
    m = Machine(img, syms, True)
    pm = Machine(img, syms, False)
    failed = []

    m.mu.mem_write(AUX, struct.pack("<17H", *([512] * 16 + [2048])))
    _, c_e0 = m.call("largest_run", (AUX, 17))

    inputs = [("sh data+bss", data_segment(sh)), ("awk data+bss", data_segment(awk))]
    inputs += synthetic()
    with open(os.path.join(outdir, "run-output.tsv"), "w") as out:
        for path in (sh, awk):
            with open(path, "rb") as handle:
                digest = hashlib.sha256(handle.read()).hexdigest()
            out.write(f"# input {os.path.basename(path)} sha256 {digest}\n")
        out.write(f"# E0 free-run pre-check over 17 runs: {c_e0} cycles\n")
        out.write("input\traw\tcompressed\treference_refuses\trule\tresult\trefuses\tcycles\tms\n")
        for name, raw in inputs:
            m.load_input(raw)
            full, rows = verdicts(m, raw)
            ref = full * 16 > len(raw) * 15
            pm.load_input(raw)
            plain, _ = pm.call("enc_stop", (SRC, len(raw), NONE, 0))
            if plain != full:
                failed.append(f"{name}: enc_stop without exits gives {plain}, enc {full}")
            for rule, result, refuses, cyc in rows:
                out.write(
                    f"{name}\t{len(raw)}\t{full}\t{int(ref)}\t{rule}\t"
                    f"{result}\t{int(refuses)}\t{cyc}\t"
                    f"{cyc / CLOCK_HZ * 1e3:.2f}\n"
                )
            if name == "random 4 KB":
                budget = [r for r in rows if r[0] == "E1 budget 1024"][0]
                if budget[1] != -2 or budget[3] >= rows[0][3]:
                    failed.append("random 4 KB: the 1 KB budget did not stop early")

    with open(corpus_list) as handle:
        paths = [line.strip() for line in handle if line.strip()]
    tally = {}
    digest = hashlib.sha256()
    with open(os.path.join(outdir, "corpus.tsv"), "w") as out:
        out.write("program\traw\tcompressed\treference_refuses")
        header_done = False
        body = []
        for path in paths:
            raw = data_segment(path)
            with open(path, "rb") as handle:
                digest.update(path.encode() + b"\0" + hashlib.sha256(handle.read()).digest())
            if len(raw) == 0:
                continue
            pm.load_input(raw)
            full, rows = verdicts(pm, raw)
            ref = full * 16 > len(raw) * 15
            if not header_done:
                out.write("".join("\t" + r[0] for r in rows[1:]) + "\n")
                header_done = True
            body.append(
                f"{path}\t{len(raw)}\t{full}\t{int(ref)}"
                + "".join(f"\t{int(r[2])}" for r in rows[1:])
                + "\n"
            )
            for rule, _, refuses, _ in rows[1:]:
                t = tally.setdefault(rule, [0, 0, 0, 0])
                t[0] += 1
                t[1] += int(refuses and not ref)
                t[2] += int(ref and not refuses)
                t[3] += int(ref)
        out.writelines(body)
        out.write(f"# corpus {len(body)} segments, identity sha256 {digest.hexdigest()}\n")
        out.write("# rule\tsegments\tfalse_refusals\tmisses\treference_refusals\n")
        for rule, t in tally.items():
            out.write(f"# {rule}\t{t[0]}\t{t[1]}\t{t[2]}\t{t[3]}\n")
    if failed:
        for line in failed:
            print("run.py: " + line, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
