"""Reject linked RP2040 kernel accesses to the per-core SIO divider."""

from __future__ import annotations

import argparse
import bisect
import re
import sys
from collections import deque
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any, BinaryIO, Protocol, cast

try:
    from capstone import (  # type: ignore[import-not-found]
        CS_ARCH_ARM,
        CS_MODE_LITTLE_ENDIAN,
        CS_MODE_MCLASS,
        CS_MODE_THUMB,
        Cs,
    )
    from capstone.arm import (  # type: ignore[import-not-found]
        ARM_OP_IMM,
        ARM_OP_MEM,
        ARM_OP_REG,
    )
    from elftools.elf.constants import SH_FLAGS  # type: ignore[import-not-found]
    from elftools.elf.elffile import ELFFile  # type: ignore[import-not-found]
except ImportError as import_error:
    print(
        "divider ownership verifier requires Capstone and pyelftools",
        file=sys.stderr,
    )
    raise SystemExit(2) from import_error

WORD_MASK = 0xFFFFFFFF
SIO_BASE = 0xD0000000
SIO_LIMIT = 0xD0000200
DIVIDER_FIRST_BYTE = 0xD0000060
DIVIDER_LAST_BYTE = 0xD000007B
MAX_SIO_CONSTANTS = 8
TRACKED_REGISTERS = tuple(
    [f"r{register_number}" for register_number in range(13)] + ["sp", "lr", "pc"]
)
REGISTER_ALIASES = {
    "sb": "r9",
    "sl": "r10",
    "fp": "r11",
    "ip": "r12",
}
DIVIDER_SYMBOL_PATTERN = re.compile(
    r"\bDIV_(?:U|S)?DIVI(?:DEND|SOR)(?:_OFFSET)?\b|"
    r"\bDIV_(?:QUOTIENT|REMAINDER|CSR)(?:_OFFSET)?\b"
)
DIRECT_ADDRESS_PATTERN = re.compile(
    r"(?<![0-9A-Za-z_])0[xX][dD]00000(?:60|64|68|6[cC]|70|74|78)" r"(?:[uUlL]{0,3})?\b"
)
SIO_OFFSET_PATTERN = re.compile(
    r"(?:\bSIO_BASE\b|0[xX][dD]0000000(?:[uUlL]{0,3})?)\s*"
    r"\+\s*\(?\s*(?:0[xX](?:60|64|68|6[cC]|70|74|78)|"
    r"(?:96|100|104|108|112|116|120))\s*\)?(?:[uUlL]{0,3})?"
)


class MemoryReference(Protocol):
    base: int
    index: int
    scale: int
    disp: int


class InstructionOperand(Protocol):
    type: int
    reg: int
    imm: int
    mem: MemoryReference


class DecodedInstruction(Protocol):
    address: int
    size: int
    mnemonic: str
    op_str: str
    operands: Sequence[InstructionOperand]

    def reg_name(self, register_id: int) -> str: ...

    def regs_access(self) -> tuple[list[int], list[int]]: ...


@dataclass(frozen=True)
class AbstractValue:
    constants: frozenset[int] = frozenset()
    unknown: bool = True
    sio_relative: bool = False

    @staticmethod
    def exact(constant: int) -> AbstractValue:
        return AbstractValue(frozenset({constant & WORD_MASK}), False, False)

    @staticmethod
    def unknown_value() -> AbstractValue:
        return AbstractValue()

    def join(self, other: AbstractValue) -> AbstractValue:
        combined_constants = self.constants | other.constants
        combined_unknown = self.unknown or other.unknown
        sio_constants = frozenset(
            constant
            for constant in combined_constants
            if SIO_BASE <= constant < SIO_LIMIT
        )
        if combined_unknown:
            combined_constants = sio_constants
        elif sio_constants and len(sio_constants) != len(combined_constants):
            combined_constants = sio_constants
            combined_unknown = True
        elif len(combined_constants) > 1 and not sio_constants:
            combined_constants = frozenset()
            combined_unknown = True
        if len(combined_constants) > MAX_SIO_CONSTANTS:
            combined_constants = frozenset(sorted(sio_constants)[:MAX_SIO_CONSTANTS])
            combined_unknown = True
        return AbstractValue(
            combined_constants,
            combined_unknown,
            self.sio_relative or other.sio_relative,
        )


RegisterState = tuple[AbstractValue, ...]


@dataclass(frozen=True)
class Finding:
    image: Path
    address: int
    function: str
    instruction: str
    effective_addresses: tuple[int, ...]
    unresolved_sio_relative: bool


@dataclass(frozen=True)
class ImageResult:
    image: Path
    image_bytes: int
    code_bytes: int
    literal_bytes: int
    instructions: int
    memory_operations: int
    sio_access_addresses: tuple[int, ...]
    findings: tuple[Finding, ...]


def normalize_register(register_name: str | None) -> str | None:
    if register_name is None:
        return None
    return REGISTER_ALIASES.get(register_name, register_name)


def unknown_state() -> RegisterState:
    return tuple(AbstractValue.unknown_value() for _ in TRACKED_REGISTERS)


def state_index(register_name: str) -> int:
    return TRACKED_REGISTERS.index(normalize_register(register_name))


def get_register(state: RegisterState, register_name: str | None) -> AbstractValue:
    normalized_name = normalize_register(register_name)
    if normalized_name not in TRACKED_REGISTERS:
        return AbstractValue.unknown_value()
    return state[state_index(normalized_name)]


def set_register(
    state: RegisterState,
    register_name: str | None,
    value: AbstractValue,
) -> RegisterState:
    normalized_name = normalize_register(register_name)
    if normalized_name not in TRACKED_REGISTERS:
        return state
    mutable_state = list(state)
    mutable_state[state_index(normalized_name)] = value
    return tuple(mutable_state)


def join_states(first: RegisterState, second: RegisterState) -> RegisterState:
    return tuple(
        first_value.join(second_value)
        for first_value, second_value in zip(first, second, strict=True)
    )


def is_sio_constant(constant: int) -> bool:
    return SIO_BASE <= constant < SIO_LIMIT


def combine_values(
    left: AbstractValue,
    right: AbstractValue,
    operation: Callable[[int, int], int],
    *,
    addition_like: bool = False,
) -> AbstractValue:
    result_constants = {
        operation(left_constant, right_constant) & WORD_MASK
        for left_constant in left.constants
        for right_constant in right.constants
    }
    result_unknown = left.unknown or right.unknown
    if len(result_constants) > MAX_SIO_CONSTANTS:
        result_constants = {
            constant for constant in result_constants if is_sio_constant(constant)
        }
        result_constants = set(sorted(result_constants)[:MAX_SIO_CONSTANTS])
        result_unknown = True
    sio_relative = left.sio_relative or right.sio_relative
    if addition_like:
        sio_relative = sio_relative or (
            left.unknown and any(is_sio_constant(value) for value in right.constants)
        )
        sio_relative = sio_relative or (
            right.unknown and any(is_sio_constant(value) for value in left.constants)
        )
    return AbstractValue(frozenset(result_constants), result_unknown, sio_relative)


def unary_value(value: AbstractValue, operation: Callable[[int], int]) -> AbstractValue:
    result_constants = {operation(constant) & WORD_MASK for constant in value.constants}
    return AbstractValue(frozenset(result_constants), value.unknown, value.sio_relative)


def operand_value(
    instruction: DecodedInstruction,
    operand: InstructionOperand,
    state: RegisterState,
) -> AbstractValue:
    if operand.type == ARM_OP_IMM:
        return AbstractValue.exact(operand.imm)
    if operand.type == ARM_OP_REG:
        return get_register(state, instruction.reg_name(operand.reg))
    return AbstractValue.unknown_value()


def memory_width(instruction: DecodedInstruction) -> int:
    mnemonic = instruction.mnemonic
    if mnemonic.startswith(("ldrb", "ldrsb", "strb")):
        return 1
    if mnemonic.startswith(("ldrh", "ldrsh", "strh")):
        return 2
    if mnemonic.startswith(("ldrd", "strd")):
        return 8
    return 4


def address_overlaps_divider(address: int, width: int) -> bool:
    address &= WORD_MASK
    access_last_byte = min(address + width - 1, WORD_MASK)
    return not (access_last_byte < DIVIDER_FIRST_BYTE or address > DIVIDER_LAST_BYTE)


def address_overlaps_sio(address: int, width: int) -> bool:
    address &= WORD_MASK
    access_last_byte = min(address + width - 1, WORD_MASK)
    return not (access_last_byte < SIO_BASE or address >= SIO_LIMIT)


class ImageAnalyzer:
    def __init__(self, image_path: Path) -> None:
        self.image_path = image_path
        self.stream: BinaryIO = image_path.open("rb")
        self.elf: Any = ELFFile(self.stream)
        self.initial_memory: list[tuple[int, int, bytes]] = []
        self.instructions: dict[int, DecodedInstruction] = {}
        self.code_range_starts: set[int] = set()
        self.function_addresses: list[int] = []
        self.function_names: list[str] = []
        self.code_bytes = 0
        self.literal_bytes = 0
        self._load_initial_memory()
        self._load_functions()
        self._decode_mapping_ranges()

    def close(self) -> None:
        self.stream.close()

    def _load_initial_memory(self) -> None:
        for segment in self.elf.iter_segments():
            if segment["p_type"] != "PT_LOAD":
                continue
            segment_start = int(segment["p_vaddr"])
            segment_data = segment.data()
            self.initial_memory.append(
                (segment_start, segment_start + len(segment_data), segment_data)
            )

    def read_initial(self, address: int, width: int) -> int | None:
        for segment_start, segment_end, segment_data in self.initial_memory:
            if segment_start <= address and address + width <= segment_end:
                data_offset = address - segment_start
                raw_value = segment_data[data_offset : data_offset + width]
                return int.from_bytes(raw_value, "little")
        return None

    def _load_functions(self) -> None:
        symbol_table = self.elf.get_section_by_name(".symtab")
        if symbol_table is None:
            raise ValueError(f"{self.image_path}: missing .symtab")
        function_pairs = sorted(
            (int(symbol["st_value"]) & ~1, str(symbol.name))
            for symbol in symbol_table.iter_symbols()
            if symbol["st_info"]["type"] == "STT_FUNC" and symbol.name
        )
        self.function_addresses = [pair[0] for pair in function_pairs]
        self.function_names = [pair[1] for pair in function_pairs]

    def function_name(self, address: int) -> str:
        function_index = bisect.bisect_right(self.function_addresses, address) - 1
        if function_index < 0:
            return "<unknown>"
        return self.function_names[function_index]

    def _mapping_symbols(self, section_index: int) -> list[tuple[int, str]]:
        symbol_table = self.elf.get_section_by_name(".symtab")
        mappings: list[tuple[int, str]] = []
        for symbol in symbol_table.iter_symbols():
            if symbol.name not in {"$a", "$d", "$t"}:
                continue
            if symbol["st_shndx"] != section_index:
                continue
            mappings.append((int(symbol["st_value"]), str(symbol.name)))
        return sorted(set(mappings))

    def _decode_mapping_ranges(self) -> None:
        decoder = Cs(
            CS_ARCH_ARM,
            CS_MODE_THUMB | CS_MODE_LITTLE_ENDIAN | CS_MODE_MCLASS,
        )
        decoder.detail = True
        for section_index, section in enumerate(self.elf.iter_sections()):
            if not int(section["sh_flags"]) & SH_FLAGS.SHF_EXECINSTR:
                continue
            section_start = int(section["sh_addr"])
            section_end = section_start + int(section["sh_size"])
            mappings = self._mapping_symbols(section_index)
            if not mappings:
                raise ValueError(
                    f"{self.image_path}: executable section {section.name} "
                    "has no ARM mapping symbols"
                )
            if mappings[0][0] > section_start:
                self.literal_bytes += mappings[0][0] - section_start
            for mapping_index, (range_start, mapping_kind) in enumerate(mappings):
                range_end = (
                    mappings[mapping_index + 1][0]
                    if mapping_index + 1 < len(mappings)
                    else section_end
                )
                if range_end <= range_start:
                    continue
                if mapping_kind != "$t":
                    self.literal_bytes += range_end - range_start
                    continue
                range_offset = range_start - section_start
                range_data = section.data()[
                    range_offset : range_offset + (range_end - range_start)
                ]
                decoded = [
                    cast(DecodedInstruction, instruction)
                    for instruction in decoder.disasm(range_data, range_start)
                ]
                decoded_bytes = sum(instruction.size for instruction in decoded)
                if decoded_bytes != len(range_data):
                    raise ValueError(
                        f"{self.image_path}: Capstone decoded {decoded_bytes} "
                        f"of {len(range_data)} bytes at 0x{range_start:08x}"
                    )
                self.code_range_starts.add(range_start)
                self.code_bytes += decoded_bytes
                for instruction in decoded:
                    self.instructions[instruction.address] = instruction

    def effective_value(
        self,
        instruction: DecodedInstruction,
        memory_operand: InstructionOperand,
        state: RegisterState,
    ) -> AbstractValue:
        base_name = instruction.reg_name(memory_operand.mem.base)
        index_name = instruction.reg_name(memory_operand.mem.index)
        if normalize_register(base_name) == "pc":
            base_value = AbstractValue.exact((instruction.address + 4) & ~3)
        else:
            base_value = get_register(state, base_name)
        if memory_operand.mem.index:
            index_value = get_register(state, index_name)
            if memory_operand.mem.scale not in {0, 1}:
                index_value = unary_value(
                    index_value,
                    lambda value: value * memory_operand.mem.scale,
                )
        else:
            index_value = AbstractValue.exact(0)
        effective_value = combine_values(
            base_value,
            index_value,
            lambda left, right: left + right,
            addition_like=True,
        )
        displacement_value = AbstractValue.exact(memory_operand.mem.disp)
        return combine_values(
            effective_value,
            displacement_value,
            lambda left, right: left + right,
            addition_like=True,
        )

    def inspect_memory(
        self, instruction: DecodedInstruction, state: RegisterState
    ) -> tuple[Finding | None, tuple[int, ...]]:
        mnemonic = instruction.mnemonic
        effective = None
        width = memory_width(instruction)
        memory_operands = [
            operand for operand in instruction.operands if operand.type == ARM_OP_MEM
        ]
        if memory_operands:
            effective = self.effective_value(instruction, memory_operands[0], state)
        elif mnemonic.startswith(("ldm", "stm")) and instruction.operands:
            base_operand = instruction.operands[0]
            if base_operand.type == ARM_OP_REG:
                effective = operand_value(instruction, base_operand, state)
                transferred_registers = sum(
                    operand.type == ARM_OP_REG for operand in instruction.operands[1:]
                )
                width = max(1, transferred_registers) * 4
        if effective is None:
            return None, ()
        sio_addresses = tuple(
            sorted(
                address
                for address in effective.constants
                if address_overlaps_sio(address, width)
            )
        )
        matching_addresses = tuple(
            sorted(
                address
                for address in effective.constants
                if address_overlaps_divider(address, width)
            )
        )
        if not matching_addresses and not effective.sio_relative:
            return None, sio_addresses
        return (
            Finding(
                self.image_path,
                instruction.address,
                self.function_name(instruction.address),
                f"{instruction.mnemonic} {instruction.op_str}".rstrip(),
                matching_addresses,
                effective.sio_relative,
            ),
            sio_addresses,
        )

    def _clear_written_registers(
        self, instruction: DecodedInstruction, state: RegisterState
    ) -> RegisterState:
        result_state = state
        _, written_register_ids = instruction.regs_access()
        for register_id in written_register_ids:
            register_name = instruction.reg_name(register_id)
            result_state = set_register(
                result_state, register_name, AbstractValue.unknown_value()
            )
        return result_state

    def _loaded_value(
        self, instruction: DecodedInstruction, state: RegisterState
    ) -> AbstractValue:
        memory_operands = [
            operand for operand in instruction.operands if operand.type == ARM_OP_MEM
        ]
        if not memory_operands:
            return AbstractValue.unknown_value()
        effective = self.effective_value(instruction, memory_operands[0], state)
        loaded_constants: set[int] = set()
        width = memory_width(instruction)
        for address in effective.constants:
            initial_value = self.read_initial(address, width)
            if initial_value is None:
                continue
            mnemonic = instruction.mnemonic
            if mnemonic.startswith("ldrsb") and initial_value & 0x80:
                initial_value |= 0xFFFFFF00
            elif mnemonic.startswith("ldrsh") and initial_value & 0x8000:
                initial_value |= 0xFFFF0000
            loaded_constants.add(initial_value & WORD_MASK)
        return AbstractValue(
            frozenset(loaded_constants),
            effective.unknown or len(loaded_constants) != len(effective.constants),
            effective.sio_relative,
        )

    def transfer(
        self, instruction: DecodedInstruction, state: RegisterState
    ) -> RegisterState:
        original_state = state
        result_state = self._clear_written_registers(instruction, state)
        mnemonic = instruction.mnemonic
        operands = instruction.operands

        if mnemonic in {"mov", "movs"} and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            result_state = set_register(
                result_state,
                destination,
                operand_value(instruction, operands[1], original_state),
            )
        elif mnemonic == "movw" and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            result_state = set_register(
                result_state, destination, AbstractValue.exact(operands[1].imm)
            )
        elif mnemonic == "movt" and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            old_value = get_register(original_state, destination)
            high_half = operands[1].imm << 16
            result_state = set_register(
                result_state,
                destination,
                unary_value(old_value, lambda value: (value & 0xFFFF) | high_half),
            )
        elif mnemonic == "adr" and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            result_state = set_register(
                result_state, destination, AbstractValue.exact(operands[1].imm)
            )
        elif mnemonic.startswith("ldr") and operands:
            destination = instruction.reg_name(operands[0].reg)
            result_state = set_register(
                result_state,
                destination,
                self._loaded_value(instruction, original_state),
            )
        elif mnemonic in {"add", "adds", "sub", "subs"} and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            if len(operands) == 2:
                left_value = get_register(original_state, destination)
                right_value = operand_value(instruction, operands[1], original_state)
            else:
                left_value = operand_value(instruction, operands[1], original_state)
                right_value = operand_value(instruction, operands[2], original_state)
            if mnemonic.startswith("sub"):
                computed_value = combine_values(
                    left_value,
                    right_value,
                    lambda left, right: left - right,
                    addition_like=True,
                )
            else:
                computed_value = combine_values(
                    left_value,
                    right_value,
                    lambda left, right: left + right,
                    addition_like=True,
                )
            result_state = set_register(
                result_state,
                destination,
                computed_value,
            )
        elif mnemonic in {"lsl", "lsls", "lsr", "lsrs", "asr", "asrs"}:
            destination = instruction.reg_name(operands[0].reg)
            if len(operands) == 2:
                source_value = get_register(original_state, destination)
                shift_value = operand_value(instruction, operands[1], original_state)
            else:
                source_value = operand_value(instruction, operands[1], original_state)
                shift_value = operand_value(instruction, operands[2], original_state)
            if mnemonic.startswith("lsl"):
                computed_value = combine_values(
                    source_value,
                    shift_value,
                    lambda value, shift: value << (shift & 31),
                )
            elif mnemonic.startswith("lsr"):
                computed_value = combine_values(
                    source_value,
                    shift_value,
                    lambda value, shift: value >> (shift & 31),
                )
            else:
                computed_value = combine_values(
                    source_value,
                    shift_value,
                    lambda value, shift: (
                        (value if value < 0x80000000 else value - 0x100000000)
                        >> (shift & 31)
                    ),
                )
            result_state = set_register(
                result_state,
                destination,
                computed_value,
            )
        elif mnemonic in {"and", "ands", "orr", "orrs", "eor", "eors", "bic", "bics"}:
            destination = instruction.reg_name(operands[0].reg)
            if len(operands) == 2:
                left_value = get_register(original_state, destination)
                right_value = operand_value(instruction, operands[1], original_state)
            else:
                left_value = operand_value(instruction, operands[1], original_state)
                right_value = operand_value(instruction, operands[2], original_state)
            operations = {
                "and": lambda left, right: left & right,
                "orr": lambda left, right: left | right,
                "eor": lambda left, right: left ^ right,
                "bic": lambda left, right: left & ~right,
            }
            operation_name = mnemonic.rstrip("s")
            result_state = set_register(
                result_state,
                destination,
                combine_values(left_value, right_value, operations[operation_name]),
            )
        elif mnemonic in {"mul", "muls"} and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            left_value = get_register(original_state, destination)
            right_value = operand_value(instruction, operands[-1], original_state)
            result_state = set_register(
                result_state,
                destination,
                combine_values(
                    left_value, right_value, lambda left, right: left * right
                ),
            )
        elif mnemonic in {"mvn", "mvns"} and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            source_value = operand_value(instruction, operands[1], original_state)
            result_state = set_register(
                result_state,
                destination,
                unary_value(source_value, lambda value: ~value),
            )
        elif mnemonic in {"neg", "negs"} and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            source_value = operand_value(instruction, operands[1], original_state)
            result_state = set_register(
                result_state,
                destination,
                unary_value(source_value, lambda value: -value),
            )
        elif mnemonic in {"uxtb", "uxth", "sxtb", "sxth"} and len(operands) >= 2:
            destination = instruction.reg_name(operands[0].reg)
            source_value = operand_value(instruction, operands[1], original_state)
            if mnemonic == "uxtb":
                computed_value = unary_value(source_value, lambda value: value & 0xFF)
            elif mnemonic == "uxth":
                computed_value = unary_value(source_value, lambda value: value & 0xFFFF)
            elif mnemonic == "sxtb":
                computed_value = unary_value(
                    source_value,
                    lambda value: (value & 0xFF) | (0xFFFFFF00 if value & 0x80 else 0),
                )
            else:
                computed_value = unary_value(
                    source_value,
                    lambda value: (
                        (value & 0xFFFF) | (0xFFFF0000 if value & 0x8000 else 0)
                    ),
                )
            result_state = set_register(result_state, destination, computed_value)
        return result_state

    def _direct_target(self, instruction: DecodedInstruction) -> int | None:
        for operand in reversed(instruction.operands):
            if operand.type == ARM_OP_IMM:
                return int(operand.imm) & ~1
        return None

    def successors(
        self, instruction: DecodedInstruction, state: RegisterState
    ) -> list[tuple[int, RegisterState]]:
        mnemonic = instruction.mnemonic
        next_address = instruction.address + instruction.size
        fallthrough_exists = next_address in self.instructions
        successors: list[tuple[int, RegisterState]] = []
        if mnemonic in {"bl", "blx"}:
            direct_target = self._direct_target(instruction)
            if direct_target in self.instructions:
                successors.append((direct_target, state))
            if fallthrough_exists:
                return_state = state
                for clobbered_register in ("r0", "r1", "r2", "r3", "r12", "lr"):
                    return_state = set_register(
                        return_state,
                        clobbered_register,
                        AbstractValue.unknown_value(),
                    )
                successors.append((next_address, return_state))
            return successors
        if mnemonic == "b":
            direct_target = self._direct_target(instruction)
            if direct_target in self.instructions:
                successors.append((direct_target, state))
            return successors
        if mnemonic.startswith("b") and mnemonic not in {"bkpt", "bx"}:
            direct_target = self._direct_target(instruction)
            if direct_target in self.instructions:
                successors.append((direct_target, state))
            if fallthrough_exists:
                successors.append((next_address, state))
            return successors
        if mnemonic in {"cbz", "cbnz"}:
            direct_target = self._direct_target(instruction)
            if direct_target in self.instructions:
                successors.append((direct_target, state))
            if fallthrough_exists:
                successors.append((next_address, state))
            return successors
        if mnemonic in {"bx", "udf"}:
            return successors
        if mnemonic == "pop" and any(
            operand.type == ARM_OP_REG
            and normalize_register(instruction.reg_name(operand.reg)) == "pc"
            for operand in instruction.operands
        ):
            return successors
        if mnemonic in {"ldr", "mov"} and instruction.operands:
            first_operand = instruction.operands[0]
            if (
                first_operand.type == ARM_OP_REG
                and normalize_register(instruction.reg_name(first_operand.reg)) == "pc"
            ):
                return successors
        if fallthrough_exists:
            successors.append((next_address, state))
        return successors

    def analyze(self) -> ImageResult:
        if not self.instructions:
            raise ValueError(f"{self.image_path}: no mapped Thumb instructions")
        analysis_roots: set[int] = set(self.code_range_starts)
        analysis_roots.update(
            address
            for address in self.function_addresses
            if address in self.instructions
        )
        findings: dict[tuple[int, tuple[int, ...], bool], Finding] = {}
        sio_access_addresses: set[int] = set()
        memory_operation_addresses: set[int] = set()
        incoming_states: dict[int, RegisterState] = {}
        pending_addresses: deque[int] = deque()
        pending_address_set: set[int] = set()

        def enqueue(instruction_address: int, incoming_state: RegisterState) -> None:
            previous_state = incoming_states.get(instruction_address)
            joined_state = (
                incoming_state
                if previous_state is None
                else join_states(previous_state, incoming_state)
            )
            if previous_state == joined_state:
                return
            incoming_states[instruction_address] = joined_state
            if instruction_address not in pending_address_set:
                pending_addresses.append(instruction_address)
                pending_address_set.add(instruction_address)

        for analysis_root in sorted(analysis_roots):
            enqueue(analysis_root, unknown_state())

        ordered_instruction_addresses = iter(sorted(self.instructions))
        while True:
            if not pending_addresses:
                for residual_address in ordered_instruction_addresses:
                    if residual_address not in incoming_states:
                        enqueue(residual_address, unknown_state())
                        break
                else:
                    break
            instruction_address = pending_addresses.popleft()
            pending_address_set.remove(instruction_address)
            instruction = self.instructions[instruction_address]
            current_state = incoming_states[instruction_address]
            finding, instruction_sio_addresses = self.inspect_memory(
                instruction, current_state
            )
            if any(
                operand.type == ARM_OP_MEM for operand in instruction.operands
            ) or instruction.mnemonic.startswith(("ldm", "stm")):
                memory_operation_addresses.add(instruction_address)
            sio_access_addresses.update(instruction_sio_addresses)
            if finding is not None:
                finding_key = (
                    finding.address,
                    finding.effective_addresses,
                    finding.unresolved_sio_relative,
                )
                findings[finding_key] = finding
            output_state = self.transfer(instruction, current_state)
            for successor_address, successor_state in self.successors(
                instruction, output_state
            ):
                enqueue(successor_address, successor_state)

        for segment_start, segment_end, segment_data in self.initial_memory:
            aligned_start = (segment_start + 3) & ~3
            for literal_address in range(aligned_start, segment_end - 3, 4):
                data_offset = literal_address - segment_start
                literal_value = int.from_bytes(
                    segment_data[data_offset : data_offset + 4], "little"
                )
                if not address_overlaps_divider(literal_value, 4):
                    continue
                finding = Finding(
                    self.image_path,
                    literal_address,
                    self.function_name(literal_address),
                    f".word 0x{literal_value:08x}",
                    (literal_value,),
                    False,
                )
                finding_key = (
                    finding.address,
                    finding.effective_addresses,
                    finding.unresolved_sio_relative,
                )
                findings[finding_key] = finding
        return ImageResult(
            self.image_path,
            self.image_path.stat().st_size,
            self.code_bytes,
            self.literal_bytes,
            len(self.instructions),
            len(memory_operation_addresses),
            tuple(sorted(sio_access_addresses)),
            tuple(findings[key] for key in sorted(findings)),
        )


def dependency_sources(dependency_directory: Path) -> set[Path]:
    if not dependency_directory.is_dir():
        raise ValueError(f"{dependency_directory}: missing dependency directory")
    compile_directory = dependency_directory.parent
    sources = set()
    dependency_files = sorted(dependency_directory.glob("*.dep"))
    if not dependency_files:
        raise ValueError(f"{dependency_directory}: contains no dependency files")
    for dependency_file in dependency_files:
        dependency_text = dependency_file.read_text(encoding="utf-8")
        flattened_text = dependency_text.replace("\\\n", " ")
        _, separator, dependencies = flattened_text.partition(":")
        if not separator:
            raise ValueError(f"{dependency_file}: malformed dependency file")
        for dependency_token in dependencies.split():
            candidate = (compile_directory / dependency_token).resolve()
            if candidate.suffix not in {".c", ".h", ".S", ".s"}:
                continue
            if candidate.is_file():
                sources.add(candidate)
    return sources


ASSEMBLY_SUFFIXES = frozenset({".S", ".s"})


def strip_comments(source_text: str, assembly: bool) -> str:
    """Blank comment text, keeping every line and column position.

    A divider register named in a comment is documentation: the compiler
    emits nothing for it, so an access has to appear in code. Scanning the
    stripped text keeps the gate on code and lets a file state which
    registers the boot ROM writes without being read as writing them.

    Block-comment state carries across lines because C comments do. String
    state resets at each newline, so an unbalanced quote costs one line
    rather than hiding the rest of the file. Text inside a string literal
    survives the strip and is still scanned, which keeps the gate
    conservative where it cannot prove intent.
    """
    stripped_lines = []
    in_block = False
    for source_line in source_text.split("\n"):
        output: list[str] = []
        in_string = False
        index = 0
        length = len(source_line)
        while index < length:
            character = source_line[index]
            pair = source_line[index : index + 2]
            if in_block:
                if pair == "*/":
                    in_block = False
                    output.append("  ")
                    index += 2
                    continue
                output.append(" ")
                index += 1
                continue
            if in_string:
                if character == "\\" and index + 1 < length:
                    output.append("  ")
                    index += 2
                    continue
                if character == '"':
                    in_string = False
                output.append(character)
                index += 1
                continue
            if pair == "/*":
                in_block = True
                output.append("  ")
                index += 2
                continue
            if pair == "//" or (assembly and character == "@"):
                output.append(" " * (length - index))
                index = length
                continue
            if character == '"':
                in_string = True
            output.append(character)
            index += 1
        stripped_lines.append("".join(output))
    return "\n".join(stripped_lines)


def scan_sources(source_paths: Iterable[Path]) -> list[str]:
    findings = []
    for source_path in sorted(set(source_paths)):
        source_text = strip_comments(
            source_path.read_text(encoding="utf-8", errors="replace"),
            source_path.suffix in ASSEMBLY_SUFFIXES,
        )
        for line_number, source_line in enumerate(source_text.splitlines(), 1):
            matched_patterns = []
            if DIVIDER_SYMBOL_PATTERN.search(source_line):
                matched_patterns.append("divider symbol")
            if DIRECT_ADDRESS_PATTERN.search(source_line):
                matched_patterns.append("direct divider address")
            if SIO_OFFSET_PATTERN.search(source_line):
                matched_patterns.append("SIO divider offset")
            if matched_patterns:
                findings.append(
                    f"{source_path}:{line_number}: "
                    f"{', '.join(matched_patterns)}: {source_line.strip()}"
                )
    return findings


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--elf",
        action="append",
        type=Path,
        required=True,
        help="linked RP2040 ELF image; repeat for each configuration",
    )
    parser.add_argument(
        "--dependency-directory",
        action="append",
        type=Path,
        default=[],
        help="compiler .dep directory whose source closure must be scanned",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    source_paths = set()
    for dependency_directory in arguments.dependency_directory:
        source_paths.update(dependency_sources(dependency_directory.resolve()))
    source_findings = scan_sources(source_paths)
    image_results = []
    for image_path in arguments.elf:
        analyzer = ImageAnalyzer(image_path.resolve())
        try:
            image_results.append(analyzer.analyze())
        finally:
            analyzer.close()

    for image_result in image_results:
        print(
            f"{image_result.image}: bytes={image_result.image_bytes} "
            f"code_bytes={image_result.code_bytes} "
            f"literal_bytes={image_result.literal_bytes} "
            f"instructions={image_result.instructions} "
            f"memory_operations={image_result.memory_operations} "
            "sio_access_addresses="
            f"{','.join(f'0x{address:08x}' for address in image_result.sio_access_addresses)} "
            f"divider_findings={len(image_result.findings)}"
        )
        for finding in image_result.findings:
            effective_text = ",".join(
                f"0x{address:08x}" for address in finding.effective_addresses
            )
            if finding.unresolved_sio_relative:
                effective_text = f"{effective_text},SIO-relative-unknown".lstrip(",")
            print(
                f"{finding.image}:0x{finding.address:08x}: "
                f"{finding.function}: {finding.instruction}: {effective_text}",
                file=sys.stderr,
            )
    print(
        f"source_files={len(source_paths)} "
        f"source_divider_findings={len(source_findings)}"
    )
    for source_finding in source_findings:
        print(source_finding, file=sys.stderr)
    if source_findings or any(result.findings for result in image_results):
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"divider ownership verifier: {error}", file=sys.stderr)
        raise SystemExit(2) from error
