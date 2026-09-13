#!/usr/bin/env python3
"""Find Thumb-2 peephole opportunities: collapsible branches, redundant
literal-pool loads, and stack reload/add/pop triples.

Reports three independent Thumb-2 code-size opportunities: long-conditional
`Bcc`/`b` pairs that would fit a short conditional encoding, PC-relative
`ldr` literal-pool loads whose target lies in the same section within ADR
range, and `ldr [sp,#n]; add sp,#n; pop {...}` triples. This is the
single-pass (non-fixed-point) predecessor of
branch_relaxation_fixed_point.py's conditional-branch analysis.

Inputs: one ELF with Thumb code, mapping symbols, and sized STT_FUNC
symbols, such as the built kernel at sys/arch/rp2040/compile/PICO/unix.
Third-party modules: capstone and pyelftools (elftools, via
reconcile_disassembly).
"""

import collections
import pathlib
import struct
import sys

import capstone
import capstone.arm
from elftools.elf.elffile import ELFFile

from reconcile_disassembly import bytes_for_range, load_elf_metadata, mapping_ranges


CONDITIONAL_BRANCHES = {
    "beq",
    "bne",
    "bhs",
    "blo",
    "bmi",
    "bpl",
    "bvs",
    "bvc",
    "bhi",
    "bls",
    "bge",
    "blt",
    "bgt",
    "ble",
}


def decode_all(executable_sections, thumb_ranges):
    decoder = capstone.Cs(
        capstone.CS_ARCH_ARM,
        capstone.CS_MODE_THUMB
        | capstone.CS_MODE_LITTLE_ENDIAN
        | capstone.CS_MODE_MCLASS,
    )
    decoder.detail = True
    instructions = []
    for thumb_range in thumb_ranges:
        range_bytes = bytes_for_range(executable_sections, thumb_range)
        decoded_range = list(decoder.disasm(range_bytes, thumb_range["start"]))
        if sum(instruction.size for instruction in decoded_range) != len(range_bytes):
            raise RuntimeError(f"incomplete decode at 0x{thumb_range['start']:08x}")
        instructions.extend(decoded_range)
    return instructions


def branch_target(instruction):
    if not instruction.operands:
        return None
    first_operand = instruction.operands[0]
    if first_operand.type != capstone.arm.ARM_OP_IMM:
        return None
    return int(first_operand.imm)


def load_sized_function_symbols(elf_path):
    functions = []
    exact_symbols = collections.defaultdict(list)
    with elf_path.open("rb") as elf_stream:
        elf_file = ELFFile(elf_stream)
        symbol_table = elf_file.get_section_by_name(".symtab")
        for symbol in symbol_table.iter_symbols():
            symbol_address = int(symbol["st_value"]) & ~1
            if symbol.name:
                exact_symbols[symbol_address].append(symbol.name)
            if symbol["st_info"]["type"] != "STT_FUNC":
                continue
            symbol_size = int(symbol["st_size"])
            if symbol_size == 0:
                continue
            functions.append(
                (symbol_address, symbol_address + symbol_size, symbol.name)
            )
    return functions, exact_symbols


def containing_function(functions, address):
    candidates = [
        item for item in functions if item[0] <= address < item[1]
    ]
    if not candidates:
        return "??"
    return min(candidates, key=lambda item: item[1] - item[0])[2]


def section_for_address(executable_sections, address, byte_count=1):
    for section in executable_sections.values():
        section_start = section["address"]
        section_end = section_start + section["size"]
        if section_start <= address and address + byte_count <= section_end:
            return section
    return None


def read_u32(executable_sections, address):
    section = section_for_address(executable_sections, address, 4)
    if section is None:
        return None
    section_offset = address - section["address"]
    return struct.unpack_from("<I", section["data"], section_offset)[0]


def summarize_conditional_pairs(instructions, functions):
    candidates = []
    adjacent_pairs = 0
    for instruction_index, instruction in enumerate(instructions[:-1]):
        if instruction.mnemonic not in CONDITIONAL_BRANCHES:
            continue
        next_instruction = instructions[instruction_index + 1]
        if next_instruction.address != instruction.address + instruction.size:
            continue
        if next_instruction.mnemonic != "b":
            continue
        skip_target = branch_target(instruction)
        final_target = branch_target(next_instruction)
        if skip_target != next_instruction.address + next_instruction.size:
            continue
        adjacent_pairs += 1
        replacement_delta = final_target - (instruction.address + 4)
        if -256 <= replacement_delta <= 254 and replacement_delta % 2 == 0:
            candidates.append(
                {
                    "address": instruction.address,
                    "function": containing_function(functions, instruction.address),
                    "condition": instruction.mnemonic,
                    "target": final_target,
                    "delta": replacement_delta,
                }
            )
    return adjacent_pairs, candidates


def summarize_calls(instructions, functions):
    in_range_calls = []
    terminal_suffix_counts = collections.Counter()
    instruction_by_address = {
        instruction.address: instruction for instruction in instructions
    }
    for instruction in instructions:
        if instruction.mnemonic != "bl":
            continue
        target = branch_target(instruction)
        replacement_delta = target - (instruction.address + 4)
        if not (-2048 <= replacement_delta <= 2046 and replacement_delta % 2 == 0):
            continue
        following = []
        next_address = instruction.address + instruction.size
        for _ in range(4):
            next_instruction = instruction_by_address.get(next_address)
            if next_instruction is None:
                break
            following.append(next_instruction.mnemonic + " " + next_instruction.op_str)
            next_address += next_instruction.size
            if next_instruction.mnemonic in {"bx", "pop", "b"}:
                break
        suffix_key = " ; ".join(following) if following else "<range end>"
        terminal_suffix_counts[suffix_key] += 1
        in_range_calls.append(
            {
                "address": instruction.address,
                "function": containing_function(functions, instruction.address),
                "target": target,
                "delta": replacement_delta,
                "suffix": suffix_key,
            }
        )
    return in_range_calls, terminal_suffix_counts


def summarize_adr_candidates(
    executable_sections, instructions, functions, exact_symbols
):
    literal_uses = collections.Counter()
    candidate_rows = []
    for instruction in instructions:
        if instruction.mnemonic != "ldr" or len(instruction.operands) != 2:
            continue
        memory_operand = instruction.operands[1]
        if memory_operand.type != capstone.arm.ARM_OP_MEM:
            continue
        if memory_operand.mem.base != capstone.arm.ARM_REG_PC:
            continue
        literal_base = (instruction.address + 4) & ~3
        literal_address = literal_base + int(memory_operand.mem.disp)
        literal_uses[literal_address] += 1
        loaded_value = read_u32(executable_sections, literal_address)
        if loaded_value is None:
            continue
        instruction_section = section_for_address(
            executable_sections, instruction.address
        )
        target_section = section_for_address(executable_sections, loaded_value)
        if instruction_section is None or target_section is None:
            continue
        if instruction_section["name"] != target_section["name"]:
            continue
        adr_delta = loaded_value - literal_base
        if adr_delta < 0 or adr_delta > 1020 or adr_delta % 4 != 0:
            continue
        candidate_rows.append(
            {
                "address": instruction.address,
                "function": containing_function(functions, instruction.address),
                "literal_address": literal_address,
                "loaded_value": loaded_value,
                "target_symbols": exact_symbols.get(loaded_value, []),
                "delta": adr_delta,
            }
        )

    candidates_by_literal = collections.defaultdict(list)
    for candidate_row in candidate_rows:
        candidates_by_literal[candidate_row["literal_address"]].append(candidate_row)
    removable_literals = {
        literal_address: rows
        for literal_address, rows in candidates_by_literal.items()
        if len(rows) == literal_uses[literal_address]
    }
    return literal_uses, candidate_rows, removable_literals


def summarize_stack_reload_patterns(instructions, functions):
    pattern_rows = []
    for instruction_index, instruction in enumerate(instructions[:-2]):
        if instruction.mnemonic != "ldr" or len(instruction.operands) != 2:
            continue
        destination_operand, memory_operand = instruction.operands
        if destination_operand.type != capstone.arm.ARM_OP_REG:
            continue
        if memory_operand.type != capstone.arm.ARM_OP_MEM:
            continue
        if memory_operand.mem.base != capstone.arm.ARM_REG_SP:
            continue
        add_instruction = instructions[instruction_index + 1]
        pop_instruction = instructions[instruction_index + 2]
        if add_instruction.address != instruction.address + instruction.size:
            continue
        if pop_instruction.address != add_instruction.address + add_instruction.size:
            continue
        if add_instruction.mnemonic != "add" or pop_instruction.mnemonic != "pop":
            continue
        pattern_rows.append(
            {
                "address": instruction.address,
                "function": containing_function(functions, instruction.address),
                "load": instruction.op_str,
                "add": add_instruction.op_str,
                "pop": pop_instruction.op_str,
            }
        )
    return pattern_rows


def print_rows(title, rows, maximum=80):
    print(f"\n{title}: {len(rows)}")
    for row in rows[:maximum]:
        print(row)
    if len(rows) > maximum:
        print(f"... {len(rows) - maximum} more")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: thumb_peepholes.py ELF")
    elf_path = pathlib.Path(sys.argv[1])
    executable_sections, mapping_symbols_by_section = load_elf_metadata(elf_path)
    thumb_ranges = [
        item
        for item in mapping_ranges(executable_sections, mapping_symbols_by_section)
        if item["kind"] == "t"
    ]
    instructions = decode_all(executable_sections, thumb_ranges)
    functions, exact_symbols = load_sized_function_symbols(elf_path)

    adjacent_pairs, conditional_candidates = summarize_conditional_pairs(
        instructions, functions
    )
    print(f"long-conditional pairs: {adjacent_pairs}")
    print_rows("collapsible conditional pairs", conditional_candidates)

    in_range_calls, suffix_counts = summarize_calls(instructions, functions)
    print_rows("in-range BL instructions", in_range_calls, maximum=30)
    print("\nin-range BL suffix histogram")
    for suffix, count in suffix_counts.most_common(40):
        print(f"{count:4d} | {suffix}")

    literal_uses, adr_candidates, removable_literals = summarize_adr_candidates(
        executable_sections, instructions, functions, exact_symbols
    )
    print_rows("ADR-compatible literal loads", adr_candidates)
    print(f"literal addresses used by LDR: {len(literal_uses)}")
    print(f"fully replaceable literal words: {len(removable_literals)}")
    print(f"candidate literal-word bytes: {4 * len(removable_literals)}")

    stack_reload_patterns = summarize_stack_reload_patterns(instructions, functions)
    print_rows("stack reload/add/pop triples", stack_reload_patterns)


if __name__ == "__main__":
    main()
