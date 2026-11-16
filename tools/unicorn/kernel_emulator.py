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
"""Run single functions of a linked rp2040 kernel ELF on a Unicorn Cortex-M0.

KernelEmulator loads every PT_LOAD segment of unix.elf at its virtual
address (and, for a segment whose load address differs, at its load address
in flash too), so the bytes that execute are the bytes the linker placed.
Flash maps read and execute only and SRAM read and write only, so a store
into flash or a fetch from SRAM faults. The CPU model is
UC_CPU_ARM_CORTEX_M0, yet Unicorn 2.1.4 executes Thumb-2 instructions
under it (RBIT, MLA, ADD.W and UDIV all run, measured on the probe in
thumb2_probe()), so the model alone does not confine execution to ARMv6-M.
A block hook therefore disassembles every translated block once through
capstone and fails the call on an encoding outside the ARMv6-M set (a
32-bit one other than BL, MSR, MRS, DMB, DSB and ISB, an MRS or MSR naming a
special register ARMv6-M lacks, CBZ, CBNZ and IT), and armv6m_check() applies
the same rule statically to a whole function.

call() follows the AAPCS (ARM IHI 0042, section 6): the first four word
arguments in r0-r3, the rest on the stack at an 8-byte aligned sp, the
return address in lr with the Thumb bit set and the result in r0; r4-r11
and sp must come back unchanged. The
return address is a trap page outside the RP2040 map; reaching it ends the
call. A call fails when it faults, exceeds its instruction budget, stops
anywhere but the trap, or, when the caller installs a policy, makes an
unaligned halfword or word access (which Unicorn performs and ARMv6-M
faults) or touches memory outside it. A call with policy=None, the helper
that builds a test input, runs without the access hook and so without
either check. The policy is byte exact and complements the guard pages that
surround every scratch buffer: Unicorn maps whole 4 KiB pages, so a guard
page alone catches an overrun only on the side where a buffer sits flush.

A host callback is a code address in the trap page holding `bx lr`; a code
hook runs the Python function there with the AAPCS argument registers and
writes its result to r0, so a kernel function that takes a function
pointer calls back into the model. The callback returns the way an AAPCS
callee may: r1-r3, r12 and the NZCV flags leave it holding fresh varied
values (clobber_caller_saved), so machine code that keeps live state in one
of them across the call fails. clobber_probe() runs six synthetic functions,
five of which do that, and requires the model to expose each.
"""

from __future__ import annotations

import hashlib
import re
import struct
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from elftools.elf.elffile import ELFFile
from unicorn import (
    UC_ARCH_ARM,
    UC_HOOK_BLOCK,
    UC_HOOK_CODE,
    UC_HOOK_MEM_READ,
    UC_HOOK_MEM_WRITE,
    UC_MEM_WRITE,
    UC_MODE_MCLASS,
    UC_MODE_THUMB,
    UC_PROT_ALL,
    UC_PROT_EXEC,
    UC_PROT_READ,
    UC_PROT_WRITE,
    Uc,
    UcError,
)
from unicorn.arm_const import (
    UC_ARM_REG_APSR_NZCV,
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_R3,
    UC_ARM_REG_R4,
    UC_ARM_REG_R5,
    UC_ARM_REG_R6,
    UC_ARM_REG_R7,
    UC_ARM_REG_R8,
    UC_ARM_REG_R9,
    UC_ARM_REG_R10,
    UC_ARM_REG_R11,
    UC_ARM_REG_R12,
    UC_ARM_REG_SP,
    UC_CPU_ARM_CORTEX_M0,
)

PAGE = 0x1000
FLASH_BASE = 0x10000000
FLASH_SIZE = 0x00200000
SRAM_BASE = 0x20000000
SRAM_SIZE = 0x00042000
SCRATCH_BASE = 0x30000000
SCRATCH_LIMIT = 0x3E000000
STACK_BASE = 0x3F000000
STACK_SIZE = 0x00010000
TRAP_BASE = 0x3FF00000
RETURN_ADDRESS = TRAP_BASE
CALLBACK_BASE = TRAP_BASE + 0x100
ARGUMENT_REGISTERS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
CALLEE_SAVED = (
    UC_ARM_REG_R4,
    UC_ARM_REG_R5,
    UC_ARM_REG_R6,
    UC_ARM_REG_R7,
    UC_ARM_REG_R8,
    UC_ARM_REG_R9,
    UC_ARM_REG_R10,
    UC_ARM_REG_R11,
)
BX_LR = b"\x70\x47"
NOP = b"\x00\xbf"

# ARMv6-M Thumb (ARM DDI 0419, section A5.1) admits 32-bit encodings only
# for BL, MSR, MRS, DMB, DSB and ISB. MRS and MSR name a special register
# (B5.4.1 and B5.4.2); the ARMv7-M BASEPRI, BASEPRI_MAX and FAULTMASK, and
# the DSP forms of the APSR mask, are not ARMv6-M. CBZ, CBNZ and IT are
# 16-bit ARMv7-M encodings. MSR is defined for the same SYSm set as MRS
# ({0..3, 5..9, 16, 20}); a write to IPSR or EPSR is architecturally ignored,
# not UNPREDICTABLE (DDI 0419E section B4.2.3), so one allow-list serves both.
ARMV6M_WIDE = frozenset({"bl", "msr", "mrs", "dmb", "dsb", "isb"})
ARMV6M_SYSREGS = frozenset(
    {"apsr", "iapsr", "eapsr", "xpsr", "ipsr", "epsr", "iepsr", "msp", "psp", "primask", "control"}
)
ARMV7M_NARROW = frozenset({"cbz", "cbnz"})
IT_MNEMONIC = re.compile(r"it[te]{0,3}")


def outside_armv6m(size: int, mnemonic: str, operands: str) -> bool:
    """Whether a decoded instruction is not in ARMv6-M Thumb."""
    base = mnemonic.split(".")[0]
    if size == 2:
        return base in ARMV7M_NARROW or IT_MNEMONIC.fullmatch(base) is not None
    if base not in ARMV6M_WIDE:
        return True
    if base in ("mrs", "msr"):
        fields = [field.strip() for field in operands.split(",")]
        register = fields[1] if base == "mrs" else fields[0]
        return register.removesuffix("_nzcvq") not in ARMV6M_SYSREGS
    return False


class CallFailure(Exception):
    """A kernel function call that did not return cleanly."""

    def __init__(self, kind: str, detail: str) -> None:
        super().__init__(f"{kind}: {detail}")
        self.kind = kind


@dataclass
class Policy:
    """Byte-exact data access allowed during one call, as [start, end) spans.

    The stack mapping is always allowed. Flash reads are allowed when
    flash_reads is set; a flash write faults through the page protection.
    check_read, when given, sees each read inside an allowed span and
    returns a reason string to reject it.
    """

    reads: list[tuple[int, int]] = field(default_factory=list)
    writes: list[tuple[int, int]] = field(default_factory=list)
    flash_reads: bool = True
    check_read: Callable[[int, int], str | None] | None = None
    on_write: Callable[[int, int], None] | None = None


def _inside(spans: Iterable[tuple[int, int]], address: int, size: int) -> bool:
    return any(start <= address and address + size <= end for start, end in spans)


@dataclass(frozen=True)
class Symbol:
    name: str
    address: int
    size: int
    kind: str


class KernelImage:
    """The loadable bytes and symbols of one linked kernel ELF."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.segments: list[tuple[int, bytes, int]] = []
        self.symbols: dict[str, list[Symbol]] = {}
        with path.open("rb") as handle:
            elf = ELFFile(handle)
            if elf["e_machine"] != "EM_ARM":
                raise CallFailure("setup", f"{path} is not an ARM ELF")
            for segment in elf.iter_segments():
                if segment["p_type"] != "PT_LOAD":
                    continue
                self.segments.append(
                    (int(segment["p_vaddr"]), segment.data(), int(segment["p_memsz"]))
                )
                if segment["p_paddr"] != segment["p_vaddr"] and segment["p_filesz"]:
                    self.segments.append((int(segment["p_paddr"]), segment.data(), 0))
            table = elf.get_section_by_name(".symtab")
            if table is None:
                raise CallFailure("setup", f"{path} has no symbol table")
            for entry in table.iter_symbols():
                kind = entry["st_info"]["type"]
                if not entry.name or kind not in ("STT_FUNC", "STT_OBJECT"):
                    continue
                address = int(entry["st_value"])
                if kind == "STT_FUNC":
                    address &= ~1
                self.symbols.setdefault(entry.name, []).append(
                    Symbol(entry.name, address, int(entry["st_size"]), kind)
                )

    def function(self, name: str) -> Symbol:
        """The one FUNC symbol of that exact name; a missing, renamed
        (.constprop, .isra) or duplicated function is a setup failure."""
        found = [s for s in self.symbols.get(name, []) if s.kind == "STT_FUNC"]
        if len(found) != 1:
            near = sorted(n for n in self.symbols if n.startswith(name + "."))
            raise CallFailure(
                "setup",
                f"{len(found)} FUNC symbols named {name} (suffixed copies: {near})",
            )
        if found[0].size == 0:
            raise CallFailure("setup", f"{name} has no recorded size")
        return found[0]

    def objects(self, name: str) -> list[Symbol]:
        return [s for s in self.symbols.get(name, []) if s.kind == "STT_OBJECT"]

    def read(self, address: int, size: int) -> bytes:
        for base, data, _ in self.segments:
            if base <= address and address + size <= base + len(data):
                return data[address - base : address - base + size]
        raise CallFailure("setup", f"no file bytes at {address:#x}")


def disassemble(image: KernelImage, name: str) -> list[tuple[int, int, str, str, bytes]]:
    """(address, size, mnemonic, operands, bytes) for the code of a function.

    The linear sweep skips the literal-pool words that a pc-relative load
    in the function names, so pool data never decodes as an instruction.
    """
    symbol = image.function(name)
    body = image.read(symbol.address, symbol.size)
    disasm = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    pool: set[int] = set()
    out = []
    offset = 0
    while offset < len(body):
        address = symbol.address + offset
        if address in pool:
            offset += 4
            continue
        decoded = next(disasm.disasm(body[offset : offset + 4], address, 1), None)
        if decoded is None:
            raise CallFailure("subset", f"{name}+{offset:#x}: undecodable bytes")
        mnemonic = decoded.mnemonic.split(".")[0]
        if mnemonic == "ldr" and "[pc," in decoded.op_str:
            immediate = int(decoded.op_str.rsplit("#", 1)[1].rstrip("]"), 0)
            pool.add(((address + 4) & ~3) + immediate)
        out.append((address, decoded.size, mnemonic, decoded.op_str, bytes(decoded.bytes)))
        offset += decoded.size
    return out


def literal_words(image: KernelImage, name: str) -> dict[int, int]:
    """The literal-pool words a function loads, keyed by their address."""
    words = {}
    for address, _, mnemonic, operands, _ in disassemble(image, name):
        if mnemonic == "ldr" and "[pc," in operands:
            immediate = int(operands.rsplit("#", 1)[1].rstrip("]"), 0)
            slot = ((address + 4) & ~3) + immediate
            words[slot] = struct.unpack("<I", image.read(slot, 4))[0]
    return words


_THUMB = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)


def block_violation(code: bytes, address: int) -> str | None:
    """The first instruction of a block outside ARMv6-M, or None."""
    offset = 0
    while offset < len(code):
        decoded = next(_THUMB.disasm(code[offset : offset + 4], address + offset, 1), None)
        if decoded is None:
            return f"undecodable instruction at {address + offset:#010x}"
        if outside_armv6m(decoded.size, decoded.mnemonic, decoded.op_str):
            return f"{decoded.mnemonic} {decoded.op_str} at {address + offset:#010x} is not ARMv6-M"
        offset += decoded.size
    return None


def armv6m_check(image: KernelImage, name: str) -> int:
    """Reject an encoding outside the ARMv6-M set; return the count."""
    instructions = disassemble(image, name)
    for address, size, mnemonic, operands, _ in instructions:
        if outside_armv6m(size, mnemonic, operands):
            raise CallFailure(
                "subset", f"{name} at {address:#x}: {mnemonic} {operands} is not ARMv6-M"
            )
    return len(instructions)


class KernelEmulator:
    """One Unicorn Cortex-M0 holding a kernel image and optional byte patches."""

    def __init__(self, image: KernelImage, patches: Iterable[tuple[int, bytes]] = ()) -> None:
        self.image = image
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
        self.uc.mem_map(FLASH_BASE, FLASH_SIZE, UC_PROT_READ | UC_PROT_EXEC)
        self.uc.mem_map(SRAM_BASE, SRAM_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        for base, data, memsz in image.segments:
            if memsz > len(data):
                self.uc.mem_write(base + len(data), bytes(memsz - len(data)))
            self.uc.mem_write(base, data)
        for address, data in patches:
            self.uc.mem_write(address, data)
        self.uc.mem_map(STACK_BASE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        self.uc.mem_map(TRAP_BASE, PAGE, UC_PROT_ALL)
        self.uc.mem_write(TRAP_BASE, NOP * (PAGE // 2))
        self.callbacks: dict[int, Callable[..., int]] = {}
        self.scratch: list[tuple[int, int]] = []
        self.next_scratch = SCRATCH_BASE
        self.policy: Policy | None = None
        self.violation: str | None = None
        self.calls = 0
        self.callbacks_run = 0
        # A digest of every call and callback (name, arguments, result): two
        # runs of one property that make the same calls print the same value.
        self.trace = hashlib.sha256()
        self.violation_kind = "memory"
        self.checked_blocks: set[int] = set()
        self.memory_hook = self.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self._access)
        self.uc.hook_add(UC_HOOK_BLOCK, self._block)
        self.uc.hook_add(
            UC_HOOK_CODE, self._callback, begin=CALLBACK_BASE, end=TRAP_BASE + PAGE - 1
        )

    # Scratch memory: every buffer sits in its own mapping between two
    # unmapped guard pages, flush against the upper guard unless low is set.

    def alloc(self, size: int, fill: bytes = b"", low: bool = False, align: int = 1) -> int:
        pages = max(1, -(-(size + align) // PAGE))
        base = self.next_scratch + PAGE
        if base + pages * PAGE >= SCRATCH_LIMIT:
            raise CallFailure("setup", "scratch space exhausted")
        self.uc.mem_map(base, pages * PAGE, UC_PROT_READ | UC_PROT_WRITE)
        self.scratch.append((base, pages * PAGE))
        self.next_scratch = base + pages * PAGE
        if low:
            address = base
        else:
            address = base + pages * PAGE - size
            address -= address % align
        pattern = fill or b"\xa5"
        span = pages * PAGE
        self.uc.mem_write(base, (pattern * (span // len(pattern) + 1))[:span])
        return address

    def release(self) -> None:
        for base, size in self.scratch:
            self.uc.mem_unmap(base, size)
        self.scratch.clear()
        self.next_scratch = SCRATCH_BASE
        self.callbacks.clear()

    def write(self, address: int, data: bytes) -> None:
        self.uc.mem_write(address, data)

    def read(self, address: int, size: int) -> bytes:
        return bytes(self.uc.mem_read(address, size))

    def word(self, address: int) -> int:
        return struct.unpack("<I", self.read(address, 4))[0]

    def callback(self, function: Callable[..., int]) -> int:
        """A Thumb function address whose call runs function(r0, r1, r2, r3)."""
        address = CALLBACK_BASE + 4 * len(self.callbacks)
        self.uc.mem_write(address, BX_LR)
        self.callbacks[address] = function
        return address | 1

    # Hooks.

    def _access(
        self, uc: Uc, access: int, address: int, size: int, _value: int, _data: object
    ) -> None:
        policy = self.policy
        if policy is None or self.violation is not None:
            return
        if address % size:
            # ARMv6-M faults every unaligned halfword or word access (ARM
            # DDI 0419, "Alignment support"); Unicorn performs it, so the
            # hook refuses it.
            self._violate(f"unaligned {size}-byte access at {address:#010x}", "alignment")
            return
        if STACK_BASE <= address < STACK_BASE + STACK_SIZE:
            return
        if access == UC_MEM_WRITE:
            if _inside(policy.writes, address, size):
                if policy.on_write is not None:
                    policy.on_write(address, size)
                return
            self._violate(f"write of {size} at {address:#010x} outside the allowed spans")
            return
        if FLASH_BASE <= address < FLASH_BASE + FLASH_SIZE and policy.flash_reads:
            return
        if not _inside(policy.reads, address, size):
            self._violate(f"read of {size} at {address:#010x} outside the allowed spans")
            return
        if policy.check_read is not None:
            reason = policy.check_read(address, size)
            if reason is not None:
                self._violate(reason)

    def fail(self, reason: str) -> None:
        """Record a contract breach a host callback detects and stop."""
        self._violate(reason, "contract")

    def _violate(self, reason: str, kind: str = "memory") -> None:
        pc = self.uc.reg_read(UC_ARM_REG_PC)
        self.violation = f"{reason} (pc {pc:#010x})"
        self.violation_kind = kind
        self.uc.emu_stop()

    def _block(self, uc: Uc, address: int, size: int, _data: object) -> None:
        if address in self.checked_blocks or address == RETURN_ADDRESS:
            return
        reason = block_violation(bytes(uc.mem_read(address, size)), address)
        if reason is not None:
            self._violate(reason, "subset")
            return
        self.checked_blocks.add(address)

    def _callback(self, uc: Uc, address: int, _size: int, _data: object) -> None:
        function = self.callbacks.get(address)
        if function is None:
            return
        arguments = [uc.reg_read(r) for r in ARGUMENT_REGISTERS]
        result = function(*arguments) & 0xFFFFFFFF
        self.callbacks_run += 1
        self.trace.update(f"callback {self.callbacks_run} {arguments} {result:#x};".encode())
        # The modeled callee returns in r0 and, as the AAPCS lets any callee,
        # leaves r1-r3, r12 and the flags undefined.
        self.clobber_caller_saved((self.callbacks_run * 0x85EBCA6B) & 0xFFFFFFFF)
        uc.reg_write(UC_ARM_REG_R0, result)

    def clobber_caller_saved(self, salt: int) -> None:
        """Give r1-r3, r12 and NZCV fresh values derived from the call count
        and salt: the registers a callee may change without restoring them."""
        mix = (self.calls * 0x9E3779B9 + salt) & 0xFFFFFFFF
        for register in ARGUMENT_REGISTERS[1:]:
            self.uc.reg_write(register, mix ^ register)
        self.uc.reg_write(UC_ARM_REG_R12, mix ^ 0x5C5C5C5C)
        self.uc.reg_write(UC_ARM_REG_APSR_NZCV, (mix >> 28) << 28)

    # The call.

    def call(
        self,
        name: str,
        *arguments: int,
        policy: Policy | None,
        budget: int,
        entry: int | None = None,
    ) -> int:
        """Call a function. A policy of None runs it without the access
        hook, for a helper outside the test (the encoder that builds a
        stream), where a Python call per access would dominate the run.
        An entry address runs code outside the kernel image (the synthetic
        functions of clobber_probe) under name."""
        if entry is None:
            entry = self.image.function(name).address
        # Every register a caller may not rely on gets a fresh value per
        # call: the unused argument registers, r12 and the NZCV flags, so
        # code that reads one before writing it cannot borrow the previous
        # call's value. r4-r11 get values the AAPCS requires back.
        self.calls += 1
        mix = (self.calls * 0x9E3779B9) & 0xFFFFFFFF
        for register in ARGUMENT_REGISTERS:
            self.uc.reg_write(register, mix ^ register)
        self.uc.reg_write(UC_ARM_REG_R12, mix ^ 0x5C5C5C5C)
        self.uc.reg_write(UC_ARM_REG_APSR_NZCV, (mix >> 28) << 28)
        preserved = {register: mix ^ (0x01010101 * register) for register in CALLEE_SAVED}
        for register, value in preserved.items():
            self.uc.reg_write(register, value)
        for register, value in zip(ARGUMENT_REGISTERS, arguments[:4]):
            self.uc.reg_write(register, value & 0xFFFFFFFF)
        stacked = arguments[4:]
        sp = (STACK_BASE + STACK_SIZE - 0x100 - 4 * len(stacked)) & ~7
        if stacked:
            self.uc.mem_write(
                sp, struct.pack(f"<{len(stacked)}I", *(v & 0xFFFFFFFF for v in stacked))
            )
        self.uc.reg_write(UC_ARM_REG_SP, sp)
        self.uc.reg_write(UC_ARM_REG_LR, RETURN_ADDRESS | 1)
        self.policy = policy
        self.violation = None
        if policy is None:
            self.uc.hook_del(self.memory_hook)
        try:
            self.uc.emu_start(entry | 1, RETURN_ADDRESS, count=budget)
        except UcError as error:
            pc = self.uc.reg_read(UC_ARM_REG_PC)
            raise CallFailure("fault", f"{name}: {error} at pc {pc:#010x}") from None
        finally:
            if policy is None:
                self.memory_hook = self.uc.hook_add(
                    UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self._access
                )
            self.policy = None
        if self.violation is not None:
            raise CallFailure(self.violation_kind, f"{name}: {self.violation}")
        pc = self.uc.reg_read(UC_ARM_REG_PC)
        if pc != RETURN_ADDRESS:
            raise CallFailure(
                "budget", f"{name} stopped at {pc:#010x} after at most {budget} instructions"
            )
        for register, value in preserved.items():
            if self.uc.reg_read(register) != value:
                raise CallFailure(
                    "abi", f"{name} returned with callee-saved register {register} changed"
                )
        if self.uc.reg_read(UC_ARM_REG_SP) != sp:
            raise CallFailure(
                "abi",
                f"{name} returned with sp {self.uc.reg_read(UC_ARM_REG_SP):#x}, expected {sp:#x}",
            )
        result = self.uc.reg_read(UC_ARM_REG_R0)
        self.trace.update(f"call {self.calls} {name} {arguments} {result:#x};".encode())
        return result


# Known-good and known-bad encodings for the subset filter: (bytes, text).
SUBSET_ACCEPTED = (
    (b"\xef\xf3\x10\x80", "mrs r0, primask"),
    (b"\x80\xf3\x10\x88", "msr primask, r0"),
    (b"\x80\xf3\x14\x88", "msr control, r0"),
    (b"\x80\xf3\x05\x88", "msr ipsr, r0"),
    (b"\xef\xf3\x08\x80", "mrs r0, msp"),
    (b"\xef\xf3\x05\x80", "mrs r0, ipsr"),
    (b"\x80\xf3\x00\x88", "msr apsr_nzcvq, r0"),
    (b"\x00\xbf", "nop"),
    (b"\x72\xb6", "cpsid i"),
)
SUBSET_REFUSED = (
    (b"\x80\xf3\x11\x88", "msr basepri, r0"),
    (b"\xef\xf3\x12\x80", "mrs r0, basepri_max"),
    (b"\xef\xf3\x13\x80", "mrs r0, faultmask"),
    (b"\x80\xf3\x00\x84", "msr apsr_g, r0"),
    (b"\x90\xfa\xa0\xf0", "rbit r0, r0"),
    (b"\x08\xb1", "cbz r0"),
    (b"\x08\xbf", "it eq"),
)


def subset_probe() -> list[str]:
    """The filter's own check: the failures among SUBSET_ACCEPTED (refused)
    and SUBSET_REFUSED (accepted), empty when it sorts every encoding."""
    failures = []
    for code, text in SUBSET_ACCEPTED:
        reason = block_violation(code, FLASH_BASE)
        if reason is not None:
            failures.append(f"refused {text}: {reason}")
    for code, text in SUBSET_REFUSED:
        if block_violation(code, FLASH_BASE) is None:
            failures.append(f"accepted {text}")
    return failures


# Synthetic Thumb functions f(callback, a, b, c) for clobber_probe: each calls
# the callback and returns a value. CLOBBER_GOOD keeps a, b and c in r5-r7
# across the call and returns their sum, the AAPCS-conforming shape. Each
# CLOBBER_BAD entry returns a register it did not save, which the AAPCS lets
# the callee change: r1 (a), r2 (b), r3 (c), r12 (a, moved there first) and
# the Z flag set by MOVS (expected 1, the value preserved flags would give).
CLOBBER_ARGUMENTS = (0x11111111, 0x22222222, 0x33333333)
CLOBBER_GOOD = bytes.fromhex("f0b504000d0016001f000020a047a819c019f0bd")
CLOBBER_BAD = (
    ("r1", bytes.fromhex("10b504000020a047080010bd"), 0x11111111),
    ("r2", bytes.fromhex("10b504000020a047100010bd"), 0x22222222),
    ("r3", bytes.fromhex("10b504000020a047180010bd"), 0x33333333),
    ("r12", bytes.fromhex("10b504008c460020a047604610bd"), 0x11111111),
    ("flags", bytes.fromhex("10b504000020a04701d0002010bd012010bd"), 1),
)
CLOBBER_CODE = TRAP_BASE + 0x800  # one 0x40-byte slot per function: Unicorn caches translations
CLOBBER_RUNS = 8


def clobber_probe(image: KernelImage) -> list[str]:
    """Check the callback model against synthetic machine code: the failures
    among the conforming function (it must return the sum of its arguments
    on every run) and the five that read a caller-saved register after the
    callback (each must return a wrong value on some run). Empty when the
    model clobbers r1-r3, r12 and the flags and conforming code still passes."""
    failures = []
    emu = KernelEmulator(image)
    callback = emu.callback(lambda *_arguments: 0)
    expected = sum(CLOBBER_ARGUMENTS) & 0xFFFFFFFF

    def run(slot: int, code: bytes) -> list[int]:
        address = CLOBBER_CODE + 0x40 * slot
        emu.write(address, code)
        return [
            emu.call(
                "clobber_probe",
                callback,
                *CLOBBER_ARGUMENTS,
                policy=None,
                budget=200,
                entry=address | 1,
            )
            for _ in range(CLOBBER_RUNS)
        ]

    if any(result != expected for result in run(0, CLOBBER_GOOD)):
        failures.append("conforming function returned a wrong sum")
    for slot, (label, code, preserved) in enumerate(CLOBBER_BAD, 1):
        if all(result == preserved for result in run(slot, code)):
            failures.append(f"{label} survived the callback in all {CLOBBER_RUNS} runs")
    return failures


def thumb2_probe() -> tuple[bool, str]:
    """Run RBIT r0, r0 (fa90 f0a0, ARMv7-M only) under the Cortex-M0 model.

    Returns whether the bare model executed it and what the block check
    reports for the same bytes; the subset claim rests on the second.
    """
    code = b"\x90\xfa\xa0\xf0" + NOP * 4
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M0)
    uc.mem_map(FLASH_BASE, PAGE, UC_PROT_READ | UC_PROT_EXEC)
    uc.mem_write(FLASH_BASE, code)
    uc.reg_write(UC_ARM_REG_R0, 1)
    try:
        uc.emu_start(FLASH_BASE | 1, FLASH_BASE + 4, count=1)
        executed = uc.reg_read(UC_ARM_REG_R0) == 0x80000000
    except UcError:
        executed = False
    return executed, block_violation(code[:4], FLASH_BASE) or ""


__all__ = [
    "CallFailure",
    "KernelEmulator",
    "KernelImage",
    "Policy",
    "armv6m_check",
    "clobber_probe",
    "disassemble",
    "literal_words",
    "subset_probe",
    "thumb2_probe",
]
