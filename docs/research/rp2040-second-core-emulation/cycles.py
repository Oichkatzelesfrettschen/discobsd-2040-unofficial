"""Cortex-M0+ cycle estimate for a Thumb-1 function under Unicorn.

usage: cycles.py ELF PACKED RAW   (ELF exports run() and run_discard())
Costs: ALU 1, LDR/STR 2, LDM/STM/PUSH/POP 1+N (+2 for POP {pc}), B taken 2,
B not taken 1, BL/BX/BLX 3, MRS/MSR/DSB/ISB 4. Instruction fetch is assumed
to hit (1 cycle); there is no bus contention and no interrupt.
"""

import bisect
import os
import subprocess
import sys
import tempfile
import zlib

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
from capstone.arm_const import ARM_OP_REG, ARM_REG_PC
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_SP,
)

elf, packed, raw = sys.argv[1:4]
syms = []
for line in subprocess.check_output(["arm-none-eabi-nm", "-n", elf], text=True).splitlines():
    addr, kind, name = line.split()
    if kind in "tT":
        syms.append((int(addr, 16), name))
addrs = [a for a, _ in syms]
entry = {n: a for a, n in syms}
fd, tmp = tempfile.mkstemp()
os.close(fd)
subprocess.check_call(["arm-none-eabi-objcopy", "-O", "binary", elf, tmp])
with open(tmp, "rb") as handle:
    img = handle.read()
os.unlink(tmp)
with open(packed, "rb") as handle:
    pk = handle.read()
with open(raw, "rb") as handle:
    rw = handle.read()

cs = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
cs.detail = True
cost_cache, func_cache = {}, {}


def cost(mu, pc, size):
    """Cycles of the instruction at pc, or "b" for a branch resolved later."""
    if pc in cost_cache:
        return cost_cache[pc]
    ins = next(iter(cs.disasm(bytes(mu.mem_read(pc, size)), pc)))
    m = ins.mnemonic
    if m.startswith(("ldm", "stm", "push", "pop")):
        n = 1 + len(ins.operands) - (1 if m.startswith(("ldm", "stm")) else 0)
        if m.startswith("pop") and any(
            o.type == ARM_OP_REG and o.reg == ARM_REG_PC for o in ins.operands
        ):
            n += 2
        c = n
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
    cost_cache[pc] = c
    return c


def run(fn, clen, rlen):
    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    mu.mem_map(0x20000000, 0x1000000)
    mu.mem_map(0x30000000, 0x1000000)
    mu.mem_write(0x20000000, img)
    mu.mem_write(0x30000000, pk)
    sentinel = 0x20F00000
    for reg, val in (
        (UC_ARM_REG_SP, 0x200F0000),
        (UC_ARM_REG_LR, sentinel | 1),
        (UC_ARM_REG_R0, clen),
        (UC_ARM_REG_R1, rlen),
        (UC_ARM_REG_R2, 0x200E0000),
    ):
        mu.reg_write(reg, val)
    per_func, prev = {}, [None]

    def hook(_mu, addr, size, _ud):
        if prev[0] is not None:
            pa, psz, pcost = prev[0]
            if pcost == "b":
                per_func[func_cache[pa]][0] += 2 if addr != pa + psz else 1
        if addr not in func_cache:
            func_cache[addr] = syms[bisect.bisect_right(addrs, addr) - 1][1]
        slot = per_func.setdefault(func_cache[addr], [0, 0])
        c = cost(mu, addr, size)
        if c != "b":
            slot[0] += c
        slot[1] += 1
        prev[0] = (addr, size, c)

    mu.hook_add(UC_HOOK_CODE, hook)
    mu.emu_start(entry[fn] | 1, sentinel, count=400_000_000)
    crc = int.from_bytes(mu.mem_read(0x200E0000, 4), "little")
    out = bytes(mu.mem_read(0x30200000, rlen))
    return mu.reg_read(UC_ARM_REG_R0), crc, out, per_func


for fn in ("run", "run_discard"):
    ret, crc, out, per_func = run(fn, len(pk), len(rw))
    ok = ret == 0 and crc == zlib.crc32(rw) and (fn != "run" or out == rw)
    total = sum(v[0] for v in per_func.values())
    print(
        f"{fn}: ok={ok} raw={len(rw)} packed={len(pk)} cycles={total} "
        f"cycles/rawbyte={total / len(rw):.1f} ms@125MHz={total / 125e3:.2f}"
    )
    for name, (c, i) in sorted(per_func.items(), key=lambda kv: -kv[1][0])[:5]:
        print(f"   {name:20s} {c:10d} cycles {100 * c / total:5.1f}%  {i} instructions")
