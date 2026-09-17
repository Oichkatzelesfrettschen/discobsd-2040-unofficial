#!/usr/bin/env python3
"""Compute reachable/unreachable functions directly from an ELF's symbols.

Groups STT_FUNC symbols into functions by (section, offset, size), builds a
call graph from decoded Thumb/ARM branch instructions, relocation records,
and raw pointer literals in allocated sections, and flags functions
unreached from a caller-supplied set of root symbols. Unlike
aout_reachability.py this reads one ELF directly (ET_REL or a linked
executable) instead of pairing an a.out with an objdump text listing.

Inputs: one ELF (relocatable .o or linked executable) with a symbol table,
such as a kernel object file or the built kernel at
sys/arch/rp2040/compile/PICO/unix, plus one or more --root symbol names.
Third-party modules: capstone and pyelftools (elftools).
"""

import argparse
import collections
import json
import pathlib
import re
import struct

import capstone
from elftools.elf.elffile import ELFFile

EXECUTABLE_FLAG = 0x4
ALLOCATED_FLAG = 0x2
MAPPING_SYMBOL_PATTERN = re.compile(r"^\$([atd])(?:\..*)?$")


def normalized_thumb_address(address):
    return address & ~1


def load_analysis(binary_path, root_symbol_names):
    with binary_path.open("rb") as binary_stream:
        elf_file = ELFFile(binary_stream)
        symbol_table = elf_file.get_section_by_name(".symtab")
        if symbol_table is None:
            raise RuntimeError(f"{binary_path} has no symbol table")

        sections = {
            section_index: {
                "index": section_index,
                "name": section.name,
                "address": int(section["sh_addr"]),
                "size": int(section["sh_size"]),
                "flags": int(section["sh_flags"]),
                "type": section["sh_type"],
                "data": section.data()
                if section["sh_type"] != "SHT_NOBITS"
                else b"",
            }
            for section_index, section in enumerate(elf_file.iter_sections())
        }

        mapping_markers_by_section = collections.defaultdict(list)
        for symbol in symbol_table.iter_symbols():
            section_index = symbol["st_shndx"]
            mapping_match = MAPPING_SYMBOL_PATTERN.fullmatch(symbol.name)
            if mapping_match is None or not isinstance(section_index, int):
                continue
            if not sections[section_index]["flags"] & EXECUTABLE_FLAG:
                continue
            marker_offset = (
                normalized_thumb_address(int(symbol["st_value"]))
                - sections[section_index]["address"]
            )
            mapping_markers_by_section[section_index].append(
                (marker_offset, mapping_match.group(1))
            )

        executable_ranges_by_section = collections.defaultdict(list)
        for section_index, section in sections.items():
            if not section["flags"] & EXECUTABLE_FLAG:
                continue
            distinct_markers = []
            for marker_offset, marker_kind in sorted(
                mapping_markers_by_section.get(section_index, [])
            ):
                if distinct_markers and marker_offset == distinct_markers[-1][0]:
                    if marker_kind == distinct_markers[-1][1]:
                        continue
                    distinct_markers[-1] = (marker_offset, marker_kind)
                else:
                    distinct_markers.append((marker_offset, marker_kind))
            if not distinct_markers:
                executable_ranges_by_section[section_index].append(
                    (0, section["size"])
                )
                continue
            if distinct_markers[0][0] > 0:
                executable_ranges_by_section[section_index].append(
                    (0, distinct_markers[0][0])
                )
            for marker_index, (range_start, range_kind) in enumerate(
                distinct_markers
            ):
                range_end = (
                    distinct_markers[marker_index + 1][0]
                    if marker_index + 1 < len(distinct_markers)
                    else section["size"]
                )
                if range_kind in ("a", "t") and range_start < range_end:
                    executable_ranges_by_section[section_index].append(
                        (range_start, range_end)
                    )

        grouped_functions = {}
        symbol_index_to_function_key = {}
        symbol_name_to_function_keys = collections.defaultdict(set)
        for symbol_index, symbol in enumerate(symbol_table.iter_symbols()):
            section_index = symbol["st_shndx"]
            if symbol["st_info"]["type"] != "STT_FUNC":
                continue
            if not isinstance(section_index, int):
                continue
            function_size = int(symbol["st_size"])
            if function_size <= 0:
                continue
            section = sections[section_index]
            if not section["flags"] & EXECUTABLE_FLAG:
                continue
            absolute_start = normalized_thumb_address(int(symbol["st_value"]))
            section_offset = absolute_start - section["address"]
            function_key = (section_index, section_offset, function_size)
            function = grouped_functions.setdefault(
                function_key,
                {
                    "key": function_key,
                    "section_index": section_index,
                    "section_name": section["name"],
                    "section_offset": section_offset,
                    "absolute_start": absolute_start,
                    "size": function_size,
                    "names": set(),
                },
            )
            function["names"].add(symbol.name)
            symbol_index_to_function_key[symbol_index] = function_key
            symbol_name_to_function_keys[symbol.name].add(function_key)

        functions_by_section = collections.defaultdict(list)
        for function in grouped_functions.values():
            functions_by_section[function["section_index"]].append(function)
        for section_functions in functions_by_section.values():
            section_functions.sort(
                key=lambda function: (function["section_offset"], function["size"])
            )

        def function_containing(section_index, section_offset):
            matching_functions = [
                function
                for function in functions_by_section.get(section_index, [])
                if function["section_offset"]
                <= section_offset
                < function["section_offset"] + function["size"]
            ]
            if not matching_functions:
                return None
            return min(matching_functions, key=lambda function: function["size"])

        root_function_keys = set()
        missing_roots = []
        for root_symbol_name in root_symbol_names:
            matching_keys = symbol_name_to_function_keys.get(root_symbol_name, set())
            if matching_keys:
                root_function_keys.update(matching_keys)
            else:
                missing_roots.append(root_symbol_name)

        direct_edges = collections.defaultdict(set)
        direct_incoming = collections.defaultdict(set)
        indirect_control_flow = []
        decoder = capstone.Cs(
            capstone.CS_ARCH_ARM,
            capstone.CS_MODE_THUMB
            | capstone.CS_MODE_LITTLE_ENDIAN
            | capstone.CS_MODE_MCLASS,
        )
        decoder.detail = True
        for function_key, function in grouped_functions.items():
            section = sections[function["section_index"]]
            function_start = function["section_offset"]
            function_end = function_start + function["size"]
            function_code_ranges = []
            for code_start, code_end in executable_ranges_by_section[
                function["section_index"]
            ]:
                intersection_start = max(function_start, code_start)
                intersection_end = min(function_end, code_end)
                if intersection_start < intersection_end:
                    function_code_ranges.append(
                        (intersection_start, intersection_end)
                    )
            for code_start, code_end in function_code_ranges:
                function_bytes = section["data"][code_start:code_end]
                instruction_base = section["address"] + code_start
                instructions = decoder.disasm(function_bytes, instruction_base)
                for instruction in instructions:
                    if not (
                        instruction.group(capstone.CS_GRP_CALL)
                        or instruction.group(capstone.CS_GRP_JUMP)
                    ):
                        continue
                    immediate_operands = [
                        operand.imm
                        for operand in instruction.operands
                        if operand.type == capstone.arm.ARM_OP_IMM
                    ]
                    if not immediate_operands:
                        if instruction.mnemonic == "bx" and instruction.op_str == "lr":
                            continue
                        indirect_control_flow.append(
                            {
                                "function": sorted(function["names"])[0],
                                "address": instruction.address,
                                "mnemonic": instruction.mnemonic,
                                "operands": instruction.op_str,
                            }
                        )
                        continue
                    for target_address in immediate_operands:
                        target_offset = (
                            normalized_thumb_address(target_address)
                            - section["address"]
                        )
                        target_function = function_containing(
                            function["section_index"], target_offset
                        )
                        if target_function is None:
                            continue
                        target_key = target_function["key"]
                        if target_key == function_key:
                            continue
                        direct_edges[function_key].add(target_key)
                        direct_incoming[target_key].add(function_key)

        relocation_edges = collections.defaultdict(set)
        relocation_incoming = collections.defaultdict(set)
        data_referenced_function_keys = set()
        unassociated_function_relocations = []
        for relocation_section in elf_file.iter_sections():
            if relocation_section["sh_type"] not in ("SHT_REL", "SHT_RELA"):
                continue
            source_section_index = int(relocation_section["sh_info"])
            if source_section_index not in sections:
                continue
            source_section = sections[source_section_index]
            linked_symbol_table = elf_file.get_section(relocation_section["sh_link"])
            for relocation in relocation_section.iter_relocations():
                target_symbol_index = int(relocation["r_info_sym"])
                target_symbol = linked_symbol_table.get_symbol(target_symbol_index)
                target_keys = set()
                if linked_symbol_table is symbol_table:
                    target_key = symbol_index_to_function_key.get(target_symbol_index)
                    if target_key is not None:
                        target_keys.add(target_key)
                if not target_keys and target_symbol.name:
                    target_keys.update(
                        symbol_name_to_function_keys.get(target_symbol.name, set())
                    )
                if not target_keys:
                    continue
                source_offset = int(relocation["r_offset"])
                source_function = function_containing(
                    source_section_index, source_offset
                )
                if source_function is not None:
                    for target_key in target_keys:
                        if target_key == source_function["key"]:
                            continue
                        relocation_edges[source_function["key"]].add(target_key)
                        relocation_incoming[target_key].add(source_function["key"])
                elif source_section["flags"] & ALLOCATED_FLAG:
                    data_referenced_function_keys.update(target_keys)
                else:
                    unassociated_function_relocations.append(
                        {
                            "source_section": source_section["name"],
                            "source_offset": source_offset,
                            "target": target_symbol.name,
                        }
                    )

        raw_pointer_references = collections.defaultdict(list)
        if elf_file["e_type"] != "ET_REL":
            function_starts = collections.defaultdict(set)
            for function_key, function in grouped_functions.items():
                function_starts[function["absolute_start"]].add(function_key)
            for section in sections.values():
                if not section["flags"] & ALLOCATED_FLAG or not section["data"]:
                    continue
                for byte_offset in range(0, max(0, len(section["data"]) - 3)):
                    pointer_value = struct.unpack_from(
                        "<I", section["data"], byte_offset
                    )[0]
                    matching_keys = function_starts.get(
                        normalized_thumb_address(pointer_value), set()
                    )
                    for matching_key in matching_keys:
                        raw_pointer_references[matching_key].append(
                            {
                                "section": section["name"],
                                "address": section["address"] + byte_offset,
                                "value": pointer_value,
                            }
                        )
            data_referenced_function_keys.update(raw_pointer_references)

        root_function_keys.update(data_referenced_function_keys)
        combined_edges = collections.defaultdict(set)
        for source_key, target_keys in direct_edges.items():
            combined_edges[source_key].update(target_keys)
        for source_key, target_keys in relocation_edges.items():
            combined_edges[source_key].update(target_keys)

        reachable_function_keys = set(root_function_keys)
        pending_function_keys = list(root_function_keys)
        while pending_function_keys:
            source_key = pending_function_keys.pop()
            for target_key in combined_edges.get(source_key, set()):
                if target_key in reachable_function_keys:
                    continue
                reachable_function_keys.add(target_key)
                pending_function_keys.append(target_key)

        unreachable_function_keys = set(grouped_functions) - reachable_function_keys
        incoming_edges = collections.defaultdict(set)
        for target_key, source_keys in direct_incoming.items():
            incoming_edges[target_key].update(source_keys)
        for target_key, source_keys in relocation_incoming.items():
            incoming_edges[target_key].update(source_keys)

        candidate_rows = []
        for function_key in sorted(
            unreachable_function_keys,
            key=lambda key: (-grouped_functions[key]["size"], key),
        ):
            function = grouped_functions[function_key]
            incoming_names = sorted(
                sorted(grouped_functions[source_key]["names"])[0]
                for source_key in incoming_edges.get(function_key, set())
            )
            candidate_rows.append(
                {
                    "names": sorted(function["names"]),
                    "section": function["section_name"],
                    "address": function["absolute_start"],
                    "size": function["size"],
                    "incoming_functions": incoming_names,
                }
            )

        total_function_bytes = sum(
            function["size"] for function in grouped_functions.values()
        )
        unreachable_function_bytes = sum(
            grouped_functions[function_key]["size"]
            for function_key in unreachable_function_keys
        )
        executable_section_bytes = sum(
            section["size"]
            for section in sections.values()
            if section["flags"] & EXECUTABLE_FLAG
        )
        return {
            "binary": str(binary_path),
            "elf_type": elf_file["e_type"],
            "roots_requested": sorted(root_symbol_names),
            "roots_missing": missing_roots,
            "function_groups": len(grouped_functions),
            "function_symbol_bytes": total_function_bytes,
            "executable_section_bytes": executable_section_bytes,
            "function_symbol_coverage_percent": round(
                100.0 * total_function_bytes / executable_section_bytes, 3
            )
            if executable_section_bytes
            else 0.0,
            "data_referenced_function_groups": len(
                data_referenced_function_keys
            ),
            "reachable_function_groups": len(reachable_function_keys),
            "unreachable_function_groups": len(unreachable_function_keys),
            "unreachable_function_bytes": unreachable_function_bytes,
            "indirect_control_flow_sites": indirect_control_flow,
            "unassociated_function_relocations": unassociated_function_relocations,
            "candidates": candidate_rows,
        }


def main():
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("binary", type=pathlib.Path)
    argument_parser.add_argument(
        "--root", action="append", required=True, dest="root_symbol_names"
    )
    argument_parser.add_argument("--output", type=pathlib.Path)
    arguments = argument_parser.parse_args()

    analysis = load_analysis(arguments.binary, set(arguments.root_symbol_names))
    serialized_analysis = json.dumps(analysis, indent=2, sort_keys=True)
    if arguments.output is not None:
        arguments.output.write_text(serialized_analysis + "\n", encoding="utf-8")
    print(serialized_analysis)


if __name__ == "__main__":
    main()
