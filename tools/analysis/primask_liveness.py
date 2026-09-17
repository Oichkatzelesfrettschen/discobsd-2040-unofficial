#!/usr/bin/env python3
"""Trace whether a PRIMASK read's value is consumed or discarded.

For every `mrs Rd, primask` in the kernel's Thumb code, forward-traces Rd
through the enclosing function's control-flow graph until it is read,
overwritten, carried across a call boundary through a caller-saved register,
or escapes the function, and reports the outcome grouped by the
arm_intr_disable/arm_intr_enable helper (if any) that issued the read.

Inputs: one ELF with Thumb code, mapping symbols, and sized STT_FUNC
symbols, such as the built kernel at sys/arch/rp2040/compile/PICO/unix.
Third-party modules: capstone and lief (via reconcile_disassembly) and
pyelftools (elftools, via reconcile_disassembly). Requires
arm-none-eabi-addr2line on PATH.
"""

import collections
import pathlib
import sys

from dead_primask_saves import inline_chain
from reconcile_disassembly import load_elf_metadata, mapping_ranges
from thumb_peepholes import (
    CONDITIONAL_BRANCHES,
    branch_target,
    decode_all,
    load_sized_function_symbols,
)

CALLER_SAVED_REGISTER_NAMES = {"r0", "r1", "r2", "r3", "r12", "ip"}


def register_names(instruction, register_identifiers):
    return {
        instruction.reg_name(register_identifier)
        for register_identifier in register_identifiers
    }


def function_for_address(functions, address):
    candidates = [function for function in functions if function[0] <= address < function[1]]
    if not candidates:
        return None
    return min(candidates, key=lambda function: function[1] - function[0])


def instruction_successors(instruction, instruction_by_address, function_range):
    function_start, function_end, _ = function_range
    fallthrough_address = instruction.address + instruction.size
    target_address = branch_target(instruction)

    if instruction.mnemonic == "b":
        candidate_addresses = [target_address]
    elif instruction.mnemonic in CONDITIONAL_BRANCHES or instruction.mnemonic in {
        "cbz",
        "cbnz",
    }:
        candidate_addresses = [fallthrough_address, target_address]
    elif instruction.mnemonic == "bx":
        candidate_addresses = []
    elif instruction.mnemonic == "pop" and "pc" in instruction.op_str:
        candidate_addresses = []
    elif instruction.mnemonic in {"udf", "bkpt"}:
        candidate_addresses = []
    else:
        candidate_addresses = [fallthrough_address]

    successors = []
    escaped = False
    for candidate_address in candidate_addresses:
        if candidate_address is None:
            escaped = True
            continue
        if not (function_start <= candidate_address < function_end):
            escaped = True
            continue
        candidate_instruction = instruction_by_address.get(candidate_address)
        if candidate_instruction is None:
            escaped = True
            continue
        successors.append(candidate_instruction)
    return successors, escaped


def trace_register_value(
    start_instruction, register_name, instruction_by_address, function_range
):
    pending_instructions = collections.deque([start_instruction])
    visited_addresses = set()
    outcomes = set()

    while pending_instructions:
        instruction = pending_instructions.popleft()
        if instruction.address in visited_addresses:
            outcomes.add("cycle")
            continue
        visited_addresses.add(instruction.address)

        read_identifiers, write_identifiers = instruction.regs_access()
        read_names = register_names(instruction, read_identifiers)
        write_names = register_names(instruction, write_identifiers)
        if register_name in read_names:
            outcomes.add("read")
            continue
        if instruction.mnemonic in {"bl", "blx"} and register_name in CALLER_SAVED_REGISTER_NAMES:
            outcomes.add("call-boundary")
            continue
        if register_name in write_names:
            outcomes.add("overwritten")
            continue

        successors, escaped = instruction_successors(
            instruction, instruction_by_address, function_range
        )
        if escaped:
            outcomes.add("function-boundary")
        if not successors and not escaped:
            outcomes.add("return")
        pending_instructions.extend(successors)

    return outcomes


def helper_name(chain):
    for function_name, _ in chain:
        if function_name in {"arm_intr_disable", "arm_intr_enable"}:
            return function_name
    return None


def first_non_helper_frame(chain):
    helper_names = {"arm_get_primask", "arm_intr_disable", "arm_intr_enable"}
    for function_name, location in chain:
        if function_name not in helper_names:
            return function_name, location
    return "??", "??:0"


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: primask_liveness.py ELF")
    elf_path = pathlib.Path(sys.argv[1])
    executable_sections, mapping_symbols_by_section = load_elf_metadata(elf_path)
    thumb_ranges = [
        item
        for item in mapping_ranges(executable_sections, mapping_symbols_by_section)
        if item["kind"] == "t"
    ]
    instructions = decode_all(executable_sections, thumb_ranges)
    instruction_by_address = {
        instruction.address: instruction for instruction in instructions
    }
    functions, _ = load_sized_function_symbols(elf_path)

    rows = []
    for instruction in instructions:
        if instruction.mnemonic != "mrs" or "primask" not in instruction.op_str:
            continue
        function_range = function_for_address(functions, instruction.address)
        next_instruction = instruction_by_address.get(instruction.address + instruction.size)
        if function_range is None or next_instruction is None:
            outcomes = {"missing-boundary"}
        else:
            destination_register = instruction.op_str.split(",", 1)[0].strip()
            outcomes = trace_register_value(
                next_instruction,
                destination_register,
                instruction_by_address,
                function_range,
            )
        chain = inline_chain(elf_path, instruction.address)
        caller_name, caller_location = first_non_helper_frame(chain)
        rows.append(
            {
                "address": instruction.address,
                "register": instruction.op_str.split(",", 1)[0].strip(),
                "helper": helper_name(chain),
                "caller": caller_name,
                "location": caller_location,
                "outcomes": outcomes,
            }
        )

    outcome_counts = collections.Counter()
    for row in rows:
        outcome_counts[(row["helper"] or "standalone", tuple(sorted(row["outcomes"])))] += 1
    print(f"PRIMASK reads: {len(rows)}")
    print("outcome groups")
    for (helper, outcomes), count in sorted(
        outcome_counts.items(), key=lambda item: (-item[1], item[0])
    ):
        print(f"{count:3d} | {helper:20s} | {','.join(outcomes)}")

    print("\nrows")
    for row in rows:
        print(
            f"0x{row['address']:08x} | {row['register']:3s} | "
            f"{row['helper'] or 'standalone':20s} | "
            f"{','.join(sorted(row['outcomes'])):20s} | "
            f"{row['caller']} | {row['location']}"
        )


if __name__ == "__main__":
    main()
