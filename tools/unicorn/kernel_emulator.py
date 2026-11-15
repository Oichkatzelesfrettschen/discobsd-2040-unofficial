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
capstone and fails the call on a 32-bit encoding outside the ARMv6-M set,
and armv6m_check() applies the same rule statically to a whole function.

call() follows the AAPCS (ARM IHI 0042, section 6): the first four word
arguments in r0-r3, the rest on the stack at an 8-byte aligned sp, the
return address in lr with the Thumb bit set and the result in r0. The
return address is a trap page outside the RP2040 map; reaching it ends the
call. A call fails when it faults, exceeds its instruction budget, stops
anywhere but the trap, makes an unaligned halfword or word access (which
Unicorn performs and ARMv6-M faults), or touches memory outside the policy
the caller installs. The policy is byte exact and complements the guard pages that
surround every scratch buffer: Unicorn maps whole 4 KiB pages, so a guard
page alone catches an overrun only on the side where a buffer sits flush.

A host callback is a code address in the trap page holding `bx lr`; a code
hook runs the Python function there with the AAPCS argument registers and
writes its result to r0, so a kernel function that takes a function
pointer calls back into the model.
"""

from __future__ import annotations

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
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_R3,
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
BX_LR = b"\x70\x47"
NOP = b"\x00\xbf"

# ARMv6-M Thumb (ARM DDI 0419, section A5.1) admits 32-bit encodings only
# for BL, MSR, MRS, DMB, DSB and ISB.
ARMV6M_WIDE = frozenset({"bl", "msr", "mrs", "dmb", "dsb", "isb"})


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
        if decoded.size == 4 and decoded.mnemonic.split(".")[0] not in ARMV6M_WIDE:
            return f"{decoded.mnemonic} {decoded.op_str} at {address + offset:#010x} is not ARMv6-M"
        offset += decoded.size
    return None


def armv6m_check(image: KernelImage, name: str) -> int:
    """Reject a 32-bit encoding outside the ARMv6-M set; return the count."""
    instructions = disassemble(image, name)
    for address, size, mnemonic, operands, _ in instructions:
        if size == 4 and mnemonic not in ARMV6M_WIDE:
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
        result = function(*arguments)
        uc.reg_write(UC_ARM_REG_R0, result & 0xFFFFFFFF)

    # The call.

    def call(self, name: str, *arguments: int, policy: Policy | None, budget: int) -> int:
        """Call a function. A policy of None runs it without the access
        hook, for a helper outside the test (the encoder that builds a
        stream), where a Python call per access would dominate the run."""
        entry = self.image.function(name).address
        for register in ARGUMENT_REGISTERS:
            self.uc.reg_write(register, 0)
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
        if self.uc.reg_read(UC_ARM_REG_SP) != sp:
            raise CallFailure(
                "abi",
                f"{name} returned with sp {self.uc.reg_read(UC_ARM_REG_SP):#x}, expected {sp:#x}",
            )
        return self.uc.reg_read(UC_ARM_REG_R0)


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
    "disassemble",
    "literal_words",
    "thumb2_probe",
]
