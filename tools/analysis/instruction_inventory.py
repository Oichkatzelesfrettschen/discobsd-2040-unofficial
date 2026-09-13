#!/usr/bin/env python3
"""Inventory barrier and interrupt-mask instructions with source locations.

Decodes every Thumb instruction in an ELF's executable sections, keeps the
memory-barrier and PRIMASK-manipulation mnemonics (isb, dsb, dmb, cpsid,
cpsie, mrs, msr), resolves each site's source function and line through
addr2line, and prints per-mnemonic and per-source-location counts.

Inputs: one ELF with Thumb code and mapping symbols, such as the built
kernel at sys/arch/rp2040/compile/PICO/unix. Third-party modules: capstone
and lief (via reconcile_disassembly) and pyelftools (elftools, via
reconcile_disassembly). Requires arm-none-eabi-addr2line on PATH.
"""

import collections
import pathlib
import re
import subprocess
import sys

from reconcile_disassembly import (
    decode_with_capstone,
    load_elf_metadata,
    mapping_ranges,
)


ADDR2LINE_PATTERN = re.compile(r"^(0x[0-9a-f]+): (.*?) at (.*)$")


def source_locations(elf_path, addresses):
    if not addresses:
        return {}
    completed_process = subprocess.run(
        [
            "arm-none-eabi-addr2line",
            "-a",
            "-f",
            "-p",
            "-e",
            str(elf_path),
            *[f"0x{address:x}" for address in addresses],
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    locations = {}
    for output_line in completed_process.stdout.splitlines():
        line_match = ADDR2LINE_PATTERN.match(output_line)
        if line_match is None:
            continue
        locations[int(line_match.group(1), 16)] = {
            "function": line_match.group(2),
            "location": line_match.group(3),
        }
    return locations


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: instruction_inventory.py ELF")
    elf_path = pathlib.Path(sys.argv[1])
    executable_sections, mapping_symbols_by_section = load_elf_metadata(elf_path)
    thumb_ranges = [
        item
        for item in mapping_ranges(executable_sections, mapping_symbols_by_section)
        if item["kind"] == "t"
    ]
    instructions, undecoded_ranges = decode_with_capstone(
        executable_sections, thumb_ranges
    )
    if undecoded_ranges:
        raise RuntimeError(f"undecoded ranges: {undecoded_ranges}")

    barrier_instructions = [
        instruction
        for instruction in instructions
        if instruction.mnemonic in {"isb", "dsb", "dmb", "cpsid", "cpsie", "mrs", "msr"}
    ]
    locations = source_locations(
        elf_path, [instruction.address for instruction in barrier_instructions]
    )
    by_instruction_and_location = collections.Counter()
    for instruction in barrier_instructions:
        source_row = locations.get(
            instruction.address, {"function": "??", "location": "??:0"}
        )
        instruction_text = instruction.mnemonic
        if instruction.op_str:
            instruction_text += " " + instruction.op_str
        counter_key = (
            instruction_text,
            source_row["function"],
            source_row["location"],
        )
        by_instruction_and_location[counter_key] += 1

    mnemonic_counts = collections.Counter(
        instruction.mnemonic for instruction in barrier_instructions
    )
    print("barrier-and-mask mnemonic counts")
    for mnemonic, count in sorted(mnemonic_counts.items()):
        print(f"{mnemonic}: {count}")
    print("\nsource groups")
    for (instruction_text, function_name, location), count in sorted(
        by_instruction_and_location.items(),
        key=lambda item: (-item[1], item[0]),
    ):
        print(f"{count:4d} | {instruction_text:22s} | {function_name} | {location}")


if __name__ == "__main__":
    main()
