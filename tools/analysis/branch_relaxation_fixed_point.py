#!/usr/bin/env python3
"""Fixed-point count of Thumb long-conditional-branch pairs collapsible.

Finds `Bcc.w`/`b` two-instruction pairs implementing a long conditional
branch and iteratively re-checks, as each earlier pair's removal shifts
later addresses by 2 bytes, whether the shifted branch would still fit a
short conditional encoding (+/-256 bytes) -- a fixed point of collapsible
pairs, not just the pairs collapsible before any relaxation.

Inputs: one ELF with Thumb code and mapping symbols ($t/$d), such as the
built kernel at sys/arch/rp2040/compile/PICO/unix. Third-party modules:
capstone and lief (via reconcile_disassembly) and pyelftools (elftools, via
reconcile_disassembly).
"""

import bisect
import collections
import pathlib
import sys

from reconcile_disassembly import load_elf_metadata, mapping_ranges
from thumb_peepholes import (
    CONDITIONAL_BRANCHES,
    branch_target,
    containing_function,
    decode_all,
    load_sized_function_symbols,
)

INVERSE_CONDITION = {
    "beq": "bne",
    "bne": "beq",
    "bhs": "blo",
    "blo": "bhs",
    "bmi": "bpl",
    "bpl": "bmi",
    "bvs": "bvc",
    "bvc": "bvs",
    "bhi": "bls",
    "bls": "bhi",
    "bge": "blt",
    "blt": "bge",
    "bgt": "ble",
    "ble": "bgt",
}


def section_name_for_address(executable_sections, address):
    for section_name, section in executable_sections.items():
        section_start = section["address"]
        section_end = section_start + section["size"]
        if section_start <= address < section_end:
            return section_name
    return None


def collect_long_conditional_pairs(executable_sections, instructions, functions):
    rows = []
    instruction_addresses = {instruction.address for instruction in instructions}
    for instruction_index, instruction in enumerate(instructions[:-1]):
        if instruction.mnemonic not in CONDITIONAL_BRANCHES:
            continue
        next_instruction = instructions[instruction_index + 1]
        if instruction.size != 2 or next_instruction.size != 2:
            continue
        if next_instruction.address != instruction.address + 2:
            continue
        if next_instruction.mnemonic != "b":
            continue
        skip_target = branch_target(instruction)
        final_target = branch_target(next_instruction)
        if skip_target != instruction.address + 4:
            continue
        if final_target not in instruction_addresses:
            continue
        source_section = section_name_for_address(
            executable_sections, instruction.address
        )
        target_section = section_name_for_address(executable_sections, final_target)
        if source_section is None or target_section != source_section:
            continue
        rows.append(
            {
                "source": instruction.address,
                "removed": next_instruction.address,
                "target": final_target,
                "section": source_section,
                "function": containing_function(functions, instruction.address),
                "old_condition": instruction.mnemonic,
                "new_condition": INVERSE_CONDITION[instruction.mnemonic],
            }
        )
    return rows


def shifted_address(address, section_name, removed_by_section):
    removed_addresses = removed_by_section[section_name]
    removed_before_address = bisect.bisect_left(removed_addresses, address)
    return address - 2 * removed_before_address


def replacement_delta(row, removed_rows):
    removed_by_section = collections.defaultdict(list)
    for removed_row in removed_rows:
        removed_by_section[removed_row["section"]].append(removed_row["removed"])
    removed_by_section[row["section"]].append(row["removed"])
    for removed_addresses in removed_by_section.values():
        removed_addresses.sort()
    new_source = shifted_address(row["source"], row["section"], removed_by_section)
    new_target = shifted_address(row["target"], row["section"], removed_by_section)
    return new_target - (new_source + 4)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: branch_relaxation_fixed_point.py ELF")
    elf_path = pathlib.Path(sys.argv[1])
    executable_sections, mapping_symbols_by_section = load_elf_metadata(elf_path)
    thumb_ranges = [
        item
        for item in mapping_ranges(executable_sections, mapping_symbols_by_section)
        if item["kind"] == "t"
    ]
    instructions = decode_all(executable_sections, thumb_ranges)
    functions, _ = load_sized_function_symbols(elf_path)
    pair_rows = collect_long_conditional_pairs(
        executable_sections, instructions, functions
    )

    admitted_rows = []
    admitted_keys = set()
    round_number = 0
    while True:
        new_rows = []
        for row in pair_rows:
            if row["source"] in admitted_keys:
                continue
            candidate_delta = replacement_delta(row, admitted_rows)
            if -256 <= candidate_delta <= 254 and candidate_delta % 2 == 0:
                admitted_row = dict(row)
                admitted_row["round"] = round_number + 1
                admitted_row["new_delta"] = candidate_delta
                new_rows.append(admitted_row)
                admitted_keys.add(row["source"])
        if not new_rows:
            break
        round_number += 1
        admitted_rows.extend(new_rows)
        print(f"round {round_number}: {len(new_rows)} newly encodable")

    print(f"long-conditional pairs: {len(pair_rows)}")
    print(f"fixed-point encodable pairs: {len(admitted_rows)}")
    print(f"instruction-byte upper bound: {2 * len(admitted_rows)}")
    print("\nrows")
    for row in admitted_rows:
        print(
            f"round={row['round']} source=0x{row['source']:08x} "
            f"target=0x{row['target']:08x} delta={row['new_delta']:+d} "
            f"{row['old_condition']}->{row['new_condition']} "
            f"function={row['function']}"
        )


if __name__ == "__main__":
    main()
