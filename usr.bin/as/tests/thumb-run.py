#!/usr/bin/env python3
"""Execute a DiscoBSD a.out Thumb executable under Unicorn.

The kernel hands _start its arguments in r0 to r2 over a stack laid out as
lib/startup-arm/crt0.c documents. A syscall is an SVC whose immediate is the
call number, with arguments in r0 to r3, the result in r0, and the carry flag
set on error. Only the calls a hello program or a small stdio/exec test
reaches are implemented; any other stops the run and is named in the
result.
"""
import struct
import sys

from unicorn import (
    UC_ARCH_ARM,
    UC_HOOK_INTR,
    UC_HOOK_MEM_UNMAPPED,
    UC_MODE_LITTLE_ENDIAN,
    UC_MODE_THUMB,
    Uc,
    UcError,
)
from unicorn.arm_const import (
    UC_ARM_REG_CPSR,
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_SP,
)

BADDR   = 0x20000000
STACK   = 0x7f0ff000
BRK     = 0x7f200000

d = open(sys.argv[1], "rb").read()
mid, text, data, bss, rt, rd, sy, entry = struct.unpack("<8I", d[:32])
if (mid & 0xffff) != 0o407:
    sys.exit("not an OMAGIC executable")
image = d[32:32+text+data] + b"\0" * bss

mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_LITTLE_ENDIAN)
mu.mem_map(BADDR, ((len(image) + 0xffff) & ~0xfff) + 0x10000)
mu.mem_write(BADDR, image)
mu.mem_map(STACK - 0xf000, 0x10000)
mu.mem_map(BRK, 0x20000)

out = bytearray()
st = {"brk": BRK, "exit": None, "fault": None}

def svc(uc, intno, user):
    if intno != 2:
        uc.emu_stop()
        return
    pc = uc.reg_read(UC_ARM_REG_PC)
    n = struct.unpack("<H", uc.mem_read(pc - 2, 2))[0] & 0xff
    a0 = uc.reg_read(UC_ARM_REG_R0)
    a1 = uc.reg_read(UC_ARM_REG_R1)
    a2 = uc.reg_read(UC_ARM_REG_R2)
    cpsr = uc.reg_read(UC_ARM_REG_CPSR) & ~(1 << 29)    # clear carry: success
    if n == 4:                                          # write(fd, buf, len)
        out.extend(uc.mem_read(a1, a2))
        r = a2
    elif n == 1:                                        # exit(status)
        st["exit"] = a0
        uc.emu_stop()
        return
    elif n == 3:
        r = 0                                           # read: end of file
    elif n == 5:
        r = 3                                           # open: fixed fake fd
    elif n == 6:
        r = 0                                           # close
    elif n == 19:
        r = 0                                           # lseek
    elif n == 20:
        r = 42                                          # getpid
    elif n == 54:
        r = 0                                           # ioctl
    elif n == 59:                                       # execve: always fails
        # No program to load under this stub, so the call reports ENOEXEC
        # (8, include/sys/errno.h) and the carry stays set; a caller that
        # checks the result and carries on past a failed exec keeps running.
        uc.reg_write(UC_ARM_REG_R0, 8)
        uc.reg_write(UC_ARM_REG_CPSR, uc.reg_read(UC_ARM_REG_CPSR) | (1 << 29))
        return
    elif n == 62:                                       # fstat
        # struct stat (include/sys/stat.h) is 14 4-byte members, 56 bytes;
        # a caller that stack-allocates exactly that much, as
        # lib/libc/stdio/flsbuf.c does to size its first stdio buffer, has
        # its saved registers and return address right above the struct,
        # so writing more than sizeof(struct stat) here corrupts them.
        uc.mem_write(a1, b"\0" * 56)
        r = 0
    elif n == 69:                                       # _brk(newbreak)
        # lib/libc/arm/sys/_brk.S passes the new break address and takes
        # zero as success; the heap lives in the slack above the image.
        st["brk"] = a0
        r = 0 if a0 < BADDR + 0x18000 else -1
    else:
        st["exit"] = "unimplemented syscall %d at %#x" % (n, pc)
        uc.emu_stop()
        return
    uc.reg_write(UC_ARM_REG_R0, r)
    uc.reg_write(UC_ARM_REG_CPSR, cpsr)

def unmapped(uc, typ, addr, size, val, user):
    st["fault"] = "unmapped access at %#x from pc %#x" % (addr, uc.reg_read(UC_ARM_REG_PC))
    return False

mu.hook_add(UC_HOOK_INTR, svc)
mu.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)

sp = STACK
mu.mem_write(sp + 0x200, b"hello\0")
mu.mem_write(sp + 0x100, struct.pack("<II", sp + 0x200, 0))
mu.mem_write(sp + 0x110, struct.pack("<I", 0))
mu.mem_write(sp, struct.pack("<III", 1, sp + 0x100, sp + 0x110))
mu.reg_write(UC_ARM_REG_SP, sp)
mu.reg_write(UC_ARM_REG_R0, 1)
mu.reg_write(UC_ARM_REG_R1, sp + 0x100)
mu.reg_write(UC_ARM_REG_R2, sp + 0x110)
mu.reg_write(UC_ARM_REG_LR, 0xfffffffe)

try:
    mu.emu_start((entry or BADDR) | 1, 0, count=50000000)
except UcError as e:
    if not st["fault"]:
        st["fault"] = "%s at pc %#x" % (e, mu.reg_read(UC_ARM_REG_PC))

sys.stdout.write(out.decode("latin1"))
if st["fault"]:
    sys.stderr.write("FAULT: %s\n" % st["fault"])
    sys.exit(2)
sys.exit(0)
