#!/usr/bin/env python3
"""Compute reachable/unreachable functions in a linked a.out program.

Parses an OMAGIC a.out binary and its matching `objdump -d` text listing,
groups instructions into functions by disassembly labels, and flags
functions unreached from the entry point, stored function-pointer literals
in the data payload, and any explicit linker-forced roots (PROGRAM:SYMBOL).

Inputs: one or more (name, a.out binary, `.dis` disassembly) triples for
programs built for the target (for example games/*/*  and games/*/*.dis).
Standard library only.
"""

import argparse
import collections
import json
import pathlib
import re
import struct

AOUT_HEADER = struct.Struct("<8I")
OMAGIC = 0o407
DEFAULT_TEXT_BASE = 0x20000000
SYMBOL_LABEL_PATTERN = re.compile(r"^\s*([0-9a-fA-F]+)\s+<([^>]+)>:\s*$")
INSTRUCTION_PATTERN = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+"
    r"(?:[0-9a-fA-F]{4}(?:\s+[0-9a-fA-F]{4})?|[0-9a-fA-F]{8})"
    r"\s+([^\s]+)(?:\s+(.*?))?\s*$"
)
TARGET_PATTERN = re.compile(r"<([^>]+)>")
SECTION_PATTERN = re.compile(r"^Disassembly of section ([^:]+):\s*$")
ENCODED_LINE_PATTERN = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+"
    r"([0-9a-fA-F]{4}(?:\s+[0-9a-fA-F]{4})?|[0-9a-fA-F]{8})\s+"
)


def normalize_symbol_name(symbol_name):
    return symbol_name.split("+", maxsplit=1)[0]


def is_function_label(symbol_name):
    return "+" not in symbol_name and not symbol_name.startswith(".")


def is_control_transfer(mnemonic):
    normalized_mnemonic = mnemonic.removesuffix(".n").removesuffix(".w")
    return normalized_mnemonic in {
        "b",
        "bl",
        "blx",
        "bx",
        "cbz",
        "cbnz",
    } or normalized_mnemonic.startswith("b.")


def parse_aout(aout_path, text_base):
    aout_bytes = aout_path.read_bytes()
    if len(aout_bytes) < AOUT_HEADER.size:
        raise RuntimeError(f"{aout_path} is shorter than its a.out header")
    (
        magic,
        text_size,
        data_size,
        bss_size,
        text_relocation_size,
        data_relocation_size,
        symbol_table_size,
        entry_address,
    ) = AOUT_HEADER.unpack_from(aout_bytes)
    if magic != OMAGIC:
        raise RuntimeError(
            f"{aout_path} has magic {magic:#x}, expected OMAGIC {OMAGIC:#x}"
        )
    payload_end = AOUT_HEADER.size + text_size + data_size
    if payload_end > len(aout_bytes):
        raise RuntimeError(
            f"{aout_path} declares {text_size + data_size} payload bytes but "
            f"contains only {len(aout_bytes) - AOUT_HEADER.size}"
        )
    return {
        "text_base": text_base,
        "text_size": text_size,
        "data_size": data_size,
        "bss_size": bss_size,
        "text_relocation_size": text_relocation_size,
        "data_relocation_size": data_relocation_size,
        "symbol_table_size": symbol_table_size,
        "entry_address": entry_address,
        "payload": aout_bytes[AOUT_HEADER.size : payload_end],
    }


def parse_disassembly(disassembly_path, text_start, text_end):
    disassembly_lines = disassembly_path.read_text(
        encoding="utf-8", errors="replace"
    ).splitlines()
    names_by_address = collections.defaultdict(set)
    observed_text_end = None
    current_section = None
    for disassembly_line in disassembly_lines:
        section_match = SECTION_PATTERN.match(disassembly_line)
        if section_match is not None:
            current_section = section_match.group(1)
            continue
        if current_section != ".text":
            continue
        encoded_line_match = ENCODED_LINE_PATTERN.match(disassembly_line)
        if encoded_line_match is not None:
            encoded_address = int(encoded_line_match.group(1), 16)
            encoded_size = len(encoded_line_match.group(2).replace(" ", "")) // 2
            encoded_end = encoded_address + encoded_size
            observed_text_end = max(observed_text_end or encoded_end, encoded_end)
        symbol_match = SYMBOL_LABEL_PATTERN.match(disassembly_line)
        if symbol_match is None:
            continue
        symbol_address = int(symbol_match.group(1), 16) & ~1
        symbol_name = symbol_match.group(2)
        if not text_start <= symbol_address < text_end:
            continue
        if not is_function_label(symbol_name):
            continue
        names_by_address[symbol_address].add(symbol_name)

    if not names_by_address:
        raise RuntimeError(
            f"{disassembly_path} contains no bounded .text symbol labels"
        )
    if observed_text_end is None:
        raise RuntimeError(f"{disassembly_path} contains no encoded .text lines")
    if observed_text_end > text_end:
        raise RuntimeError(
            f"{disassembly_path} .text ends at {observed_text_end:#x}, beyond "
            f"the a.out text boundary {text_end:#x}"
        )

    sorted_addresses = sorted(names_by_address)
    function_groups = {}
    name_to_address = {}
    for address_index, function_address in enumerate(sorted_addresses):
        next_address = (
            sorted_addresses[address_index + 1]
            if address_index + 1 < len(sorted_addresses)
            else observed_text_end
        )
        function_size = next_address - function_address
        if function_size <= 0:
            continue
        function_names = sorted(names_by_address[function_address])
        function_groups[function_address] = {
            "address": function_address,
            "names": function_names,
            "size": function_size,
        }
        for function_name in function_names:
            name_to_address[function_name] = function_address

    direct_edges = collections.defaultdict(set)
    direct_incoming = collections.defaultdict(set)
    current_function_address = None
    current_section = None
    for disassembly_line in disassembly_lines:
        section_match = SECTION_PATTERN.match(disassembly_line)
        if section_match is not None:
            current_section = section_match.group(1)
            current_function_address = None
            continue
        if current_section != ".text":
            continue
        symbol_match = SYMBOL_LABEL_PATTERN.match(disassembly_line)
        if symbol_match is not None:
            label_address = int(symbol_match.group(1), 16) & ~1
            label_name = symbol_match.group(2)
            normalized_label_name = normalize_symbol_name(label_name)
            current_function_address = name_to_address.get(normalized_label_name)
            if current_function_address is None:
                matching_addresses = [
                    function_address
                    for function_address, function in function_groups.items()
                    if function_address
                    <= label_address
                    < function_address + function["size"]
                ]
                current_function_address = (
                    max(matching_addresses) if matching_addresses else None
                )
            continue

        instruction_match = INSTRUCTION_PATTERN.match(disassembly_line)
        if instruction_match is None or current_function_address is None:
            continue
        mnemonic = instruction_match.group(2)
        operands_and_comment = instruction_match.group(3) or ""
        if not is_control_transfer(mnemonic):
            continue
        for target_name in TARGET_PATTERN.findall(operands_and_comment):
            normalized_target_name = normalize_symbol_name(target_name)
            target_address = name_to_address.get(normalized_target_name)
            if target_address is None or target_address == current_function_address:
                continue
            direct_edges[current_function_address].add(target_address)
            direct_incoming[target_address].add(current_function_address)

    return (
        function_groups,
        name_to_address,
        direct_edges,
        direct_incoming,
        observed_text_end,
    )


def find_stored_function_pointers(payload, text_base, function_groups):
    addresses_to_functions = collections.defaultdict(set)
    for function_address in function_groups:
        addresses_to_functions[function_address].add(function_address)
        addresses_to_functions[function_address | 1].add(function_address)

    stored_pointer_references = collections.defaultdict(list)
    for payload_offset in range(max(0, len(payload) - 3)):
        pointer_value = struct.unpack_from("<I", payload, payload_offset)[0]
        for function_address in addresses_to_functions.get(pointer_value, set()):
            stored_pointer_references[function_address].append(
                {
                    "payload_offset": payload_offset,
                    "runtime_address": text_base + payload_offset,
                    "pointer_value": pointer_value,
                }
            )
    return stored_pointer_references


def analyze_program(
    program_name,
    aout_path,
    disassembly_path,
    text_base,
    explicit_root_names,
):
    aout = parse_aout(aout_path, text_base)
    text_end = text_base + aout["text_size"]
    (
        function_groups,
        name_to_address,
        direct_edges,
        direct_incoming,
        observed_text_end,
    ) = parse_disassembly(disassembly_path, text_base, text_end)
    stored_pointer_references = find_stored_function_pointers(
        aout["payload"], text_base, function_groups
    )

    normalized_entry_address = aout["entry_address"] & ~1
    entry_function_address = normalized_entry_address
    if entry_function_address not in function_groups:
        containing_functions = [
            function_address
            for function_address, function in function_groups.items()
            if function_address
            <= normalized_entry_address
            < function_address + function["size"]
        ]
        if not containing_functions:
            raise RuntimeError(
                f"{aout_path} entry {aout['entry_address']:#x} is outside all "
                "disassembly function groups"
            )
        entry_function_address = max(containing_functions)

    root_reasons = collections.defaultdict(set)
    root_reasons[entry_function_address].add("a.out entry point")
    for function_address in stored_pointer_references:
        root_reasons[function_address].add("stored payload pointer")
    for explicit_root_name in explicit_root_names:
        explicit_root_address = name_to_address.get(explicit_root_name)
        if explicit_root_address is None:
            raise RuntimeError(
                f"{program_name} explicit root {explicit_root_name!r} does not "
                f"name a function in {disassembly_path}"
            )
        root_reasons[explicit_root_address].add(
            f"explicit linker root: {explicit_root_name}"
        )

    root_function_addresses = set(root_reasons)
    reachable_function_addresses = set(root_function_addresses)
    pending_function_addresses = list(root_function_addresses)
    while pending_function_addresses:
        source_address = pending_function_addresses.pop()
        for target_address in direct_edges.get(source_address, set()):
            if target_address in reachable_function_addresses:
                continue
            reachable_function_addresses.add(target_address)
            pending_function_addresses.append(target_address)

    unreachable_function_addresses = set(function_groups) - reachable_function_addresses
    candidate_rows = []
    for function_address in sorted(
        unreachable_function_addresses,
        key=lambda address: (-function_groups[address]["size"], address),
    ):
        function = function_groups[function_address]
        candidate_rows.append(
            {
                "address": function_address,
                "names": function["names"],
                "size": function["size"],
                "incoming_functions": [
                    function_groups[incoming_address]["names"][0]
                    for incoming_address in sorted(
                        direct_incoming.get(function_address, set())
                    )
                ],
            }
        )

    unreachable_bytes = sum(row["size"] for row in candidate_rows)
    function_described_bytes = sum(
        function["size"] for function in function_groups.values()
    )
    return {
        "program": program_name,
        "aout": str(aout_path),
        "disassembly": str(disassembly_path),
        "entry_address": aout["entry_address"],
        "entry_names": function_groups[entry_function_address]["names"],
        "explicit_root_names": sorted(explicit_root_names),
        "roots": [
            {
                "address": function_address,
                "names": function_groups[function_address]["names"],
                "reasons": sorted(root_reasons[function_address]),
            }
            for function_address in sorted(root_function_addresses)
        ],
        "text_bytes": aout["text_size"],
        "disassembled_text_bytes": observed_text_end - text_base,
        "non_disassembled_text_bytes": aout["text_size"]
        - (observed_text_end - text_base),
        "data_bytes": aout["data_size"],
        "bss_bytes": aout["bss_size"],
        "function_groups": len(function_groups),
        "function_described_bytes": function_described_bytes,
        "function_coverage_percent": round(
            100.0 * function_described_bytes / (observed_text_end - text_base), 3
        ),
        "stored_pointer_function_groups": len(stored_pointer_references),
        "reachable_function_groups": len(reachable_function_addresses),
        "unreachable_function_groups": len(unreachable_function_addresses),
        "unreachable_function_bytes": unreachable_bytes,
        "candidates": candidate_rows,
    }


def main():
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument(
        "--program",
        action="append",
        nargs=3,
        metavar=("NAME", "AOUT", "DISASSEMBLY"),
        required=True,
    )
    argument_parser.add_argument(
        "--text-base", type=lambda value: int(value, 0), default=DEFAULT_TEXT_BASE
    )
    argument_parser.add_argument(
        "--root",
        action="append",
        default=[],
        metavar="PROGRAM:SYMBOL",
        help="preserve a linker-forced symbol as a root in one named program",
    )
    argument_parser.add_argument("--output", type=pathlib.Path)
    argument_parser.add_argument("--quiet", action="store_true")
    arguments = argument_parser.parse_args()

    program_names = [program_name for program_name, _, _ in arguments.program]
    if len(program_names) != len(set(program_names)):
        raise RuntimeError("--program names must be unique")

    explicit_roots_by_program = collections.defaultdict(set)
    for root_specification in arguments.root:
        program_name, separator, symbol_name = root_specification.partition(":")
        if not separator or not program_name or not symbol_name:
            raise RuntimeError(
                f"invalid --root {root_specification!r}; expected PROGRAM:SYMBOL"
            )
        if program_name not in program_names:
            raise RuntimeError(
                f"--root program {program_name!r} does not match any --program"
            )
        explicit_roots_by_program[program_name].add(symbol_name)

    program_analyses = [
        analyze_program(
            program_name,
            pathlib.Path(aout_path),
            pathlib.Path(disassembly_path),
            arguments.text_base,
            explicit_roots_by_program[program_name],
        )
        for program_name, aout_path, disassembly_path in arguments.program
    ]
    result = {
        "program_count": len(program_analyses),
        "text_bytes": sum(analysis["text_bytes"] for analysis in program_analyses),
        "unreachable_function_groups": sum(
            analysis["unreachable_function_groups"] for analysis in program_analyses
        ),
        "unreachable_function_bytes": sum(
            analysis["unreachable_function_bytes"] for analysis in program_analyses
        ),
        "programs": program_analyses,
    }
    serialized_result = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output is not None:
        arguments.output.write_text(serialized_result, encoding="utf-8")
    if not arguments.quiet:
        print(serialized_result, end="")


if __name__ == "__main__":
    main()
