#!/usr/bin/env python3
"""Filter aout_reachability candidates against a linked archive's call graph.

Reads an aout_reachability.py JSON result (per-program unreachable-function
candidates) plus a static archive (a `.a` library such as the a.out libc),
and reclassifies each candidate as co-retained (an archive object also holds
a still-reachable definition), discardable, or unmapped, by building a
section-level call graph from the archive's ELF object members.

Inputs: an aout_reachability.py --output JSON file and the archive it was
computed against (for example lib/libc_aout/libc.a). Third-party module:
pyelftools (elftools).
"""

import argparse
import collections
import hashlib
import io
import json
import pathlib

from aout_reachability import parse_aout, parse_disassembly
from elftools.elf.elffile import ELFFile

AR_MAGIC = b"!<arch>\n"
AR_HEADER_SIZE = 60
SHF_ALLOC = 0x2


def parse_archive_members(archive_path):
    archive_bytes = archive_path.read_bytes()
    if not archive_bytes.startswith(AR_MAGIC):
        raise RuntimeError(f"{archive_path} lacks the Unix archive magic")

    long_name_table = None
    archive_offset = len(AR_MAGIC)
    while archive_offset < len(archive_bytes):
        header_end = archive_offset + AR_HEADER_SIZE
        if header_end > len(archive_bytes):
            raise RuntimeError(f"{archive_path} has a truncated member header")
        member_header = archive_bytes[archive_offset:header_end]
        if member_header[58:60] != b"`\n":
            raise RuntimeError(
                f"{archive_path} has invalid member magic at {archive_offset:#x}"
            )
        raw_member_name = member_header[:16].decode("ascii").rstrip()
        try:
            stored_member_size = int(member_header[48:58].decode("ascii").strip())
        except ValueError as error:
            raise RuntimeError(
                f"{archive_path} has invalid member size at {archive_offset:#x}"
            ) from error

        stored_member_start = header_end
        stored_member_end = stored_member_start + stored_member_size
        if stored_member_end > len(archive_bytes):
            raise RuntimeError(
                f"{archive_path} member at {archive_offset:#x} exceeds the archive"
            )
        member_bytes = archive_bytes[stored_member_start:stored_member_end]
        archive_offset = stored_member_end + (stored_member_size & 1)

        if raw_member_name == "//":
            long_name_table = member_bytes
            continue
        if raw_member_name in {"/", "/SYM64/"}:
            continue

        if raw_member_name.startswith("#1/"):
            name_size = int(raw_member_name[3:])
            member_name = member_bytes[:name_size].decode("utf-8")
            member_bytes = member_bytes[name_size:]
        elif raw_member_name.startswith("/") and raw_member_name[1:].isdigit():
            if long_name_table is None:
                raise RuntimeError(
                    f"{archive_path} references a missing long-name table"
                )
            name_offset = int(raw_member_name[1:])
            name_end = long_name_table.find(b"/\n", name_offset)
            if name_end < 0:
                raise RuntimeError(
                    f"{archive_path} has an unterminated long member name"
                )
            member_name = long_name_table[name_offset:name_end].decode("utf-8")
        else:
            member_name = raw_member_name.removesuffix("/")

        yield member_name, member_bytes


def read_archive_graph(archive_path):
    definitions_by_name = collections.defaultdict(set)
    symbol_sizes_by_name_and_node = collections.defaultdict(set)
    unresolved_edges = []
    direct_edges = collections.defaultdict(set)
    node_sizes = {}
    member_count = 0

    for member_name, member_bytes in parse_archive_members(archive_path):
        member_count += 1
        if not member_bytes.startswith(b"\x7fELF"):
            continue
        elf_file = ELFFile(io.BytesIO(member_bytes))
        alloc_nodes_by_index = {}
        for section_index, section in enumerate(elf_file.iter_sections()):
            if int(section["sh_flags"]) & SHF_ALLOC:
                node = (member_name, section.name)
                alloc_nodes_by_index[section_index] = node
                node_sizes[node] = int(section["sh_size"])

        symbol_table = elf_file.get_section_by_name(".symtab")
        if symbol_table is None:
            continue
        for symbol in symbol_table.iter_symbols():
            section_index = symbol["st_shndx"]
            if not isinstance(section_index, int):
                continue
            node = alloc_nodes_by_index.get(section_index)
            if node is None or not symbol.name:
                continue
            definitions_by_name[symbol.name].add(node)
            symbol_sizes_by_name_and_node[(symbol.name, node)].add(
                int(symbol["st_size"])
            )

        for relocation_section in elf_file.iter_sections():
            if relocation_section["sh_type"] not in {"SHT_REL", "SHT_RELA"}:
                continue
            source_node = alloc_nodes_by_index.get(relocation_section["sh_info"])
            if source_node is None:
                continue
            relocation_symbol_table = elf_file.get_section(
                relocation_section["sh_link"]
            )
            for relocation in relocation_section.iter_relocations():
                target_symbol = relocation_symbol_table.get_symbol(
                    relocation["r_info_sym"]
                )
                target_section_index = target_symbol["st_shndx"]
                if isinstance(target_section_index, int):
                    target_node = alloc_nodes_by_index.get(target_section_index)
                    if target_node is not None and target_node != source_node:
                        direct_edges[source_node].add(target_node)
                elif target_symbol.name:
                    unresolved_edges.append((source_node, target_symbol.name))

    for source_node, target_name in unresolved_edges:
        direct_edges[source_node].update(definitions_by_name.get(target_name, set()))

    return {
        "member_count": member_count,
        "definitions_by_name": definitions_by_name,
        "symbol_sizes_by_name_and_node": symbol_sizes_by_name_and_node,
        "edges": direct_edges,
        "node_sizes": node_sizes,
    }


def match_archive_nodes(function, archive_graph):
    exact_size_nodes = set()
    fallback_nodes = set()
    for function_name in function["names"]:
        for node in archive_graph["definitions_by_name"].get(function_name, set()):
            fallback_nodes.add(node)
            archive_symbol_sizes = archive_graph["symbol_sizes_by_name_and_node"].get(
                (function_name, node), set()
            )
            if function["size"] in archive_symbol_sizes:
                exact_size_nodes.add(node)
    return exact_size_nodes or fallback_nodes


def analyze_program(program_analysis, archive_graph):
    aout_path = pathlib.Path(program_analysis["aout"])
    disassembly_path = pathlib.Path(program_analysis["disassembly"])
    aout = parse_aout(aout_path, 0x20000000)
    text_end = aout["text_base"] + aout["text_size"]
    function_groups, _, _, _, _ = parse_disassembly(
        disassembly_path, aout["text_base"], text_end
    )

    candidate_addresses = {
        candidate["address"] for candidate in program_analysis["candidates"]
    }
    nodes_by_function_address = {
        function_address: match_archive_nodes(function, archive_graph)
        for function_address, function in function_groups.items()
    }
    present_nodes = set().union(*nodes_by_function_address.values())
    retained_nodes = {
        node
        for function_address, matching_nodes in nodes_by_function_address.items()
        if function_address not in candidate_addresses
        for node in matching_nodes
    }

    pending_nodes = list(retained_nodes)
    while pending_nodes:
        source_node = pending_nodes.pop()
        for target_node in archive_graph["edges"].get(source_node, set()):
            if target_node not in present_nodes or target_node in retained_nodes:
                continue
            retained_nodes.add(target_node)
            pending_nodes.append(target_node)

    co_retained_rows = []
    discardable_archive_rows = []
    unmapped_rows = []
    for candidate in program_analysis["candidates"]:
        matching_nodes = nodes_by_function_address[candidate["address"]]
        row = {
            "address": candidate["address"],
            "names": candidate["names"],
            "size": candidate["size"],
            "archive_nodes": [
                f"{member_name}:{section_name}"
                for member_name, section_name in sorted(matching_nodes)
            ],
        }
        if not matching_nodes:
            unmapped_rows.append(row)
        elif matching_nodes & retained_nodes:
            co_retained_rows.append(row)
        else:
            discardable_archive_rows.append(row)

    co_retained_bytes = sum(row["size"] for row in co_retained_rows)
    discardable_archive_bytes = sum(row["size"] for row in discardable_archive_rows)
    unmapped_bytes = sum(row["size"] for row in unmapped_rows)
    raw_candidate_bytes = program_analysis["unreachable_function_bytes"]
    classified_bytes = co_retained_bytes + discardable_archive_bytes + unmapped_bytes
    if classified_bytes != raw_candidate_bytes:
        raise RuntimeError(
            f"{program_analysis['program']} classified {classified_bytes} of "
            f"{raw_candidate_bytes} candidate bytes"
        )

    return {
        "program": program_analysis["program"],
        "raw_candidate_bytes": raw_candidate_bytes,
        "co_retained_archive_bytes": co_retained_bytes,
        "discardable_archive_bytes": discardable_archive_bytes,
        "unmapped_candidate_bytes": unmapped_bytes,
        "post_archive_filter_ceiling_bytes": raw_candidate_bytes - co_retained_bytes,
        "co_retained_archive_candidates": co_retained_rows,
        "discardable_archive_candidates": discardable_archive_rows,
        "unmapped_candidates": unmapped_rows,
    }


def main():
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("--analysis", type=pathlib.Path, required=True)
    argument_parser.add_argument("--archive", type=pathlib.Path, required=True)
    argument_parser.add_argument("--output", type=pathlib.Path)
    argument_parser.add_argument("--quiet", action="store_true")
    arguments = argument_parser.parse_args()

    source_analysis = json.loads(arguments.analysis.read_text(encoding="utf-8"))
    archive_graph = read_archive_graph(arguments.archive)
    program_results = [
        analyze_program(program_analysis, archive_graph)
        for program_analysis in source_analysis["programs"]
    ]
    result = {
        "analysis": str(arguments.analysis),
        "archive": str(arguments.archive),
        "archive_sha256": hashlib.sha256(arguments.archive.read_bytes()).hexdigest(),
        "archive_member_count": archive_graph["member_count"],
        "program_count": len(program_results),
        "raw_candidate_bytes": sum(
            result["raw_candidate_bytes"] for result in program_results
        ),
        "co_retained_archive_bytes": sum(
            result["co_retained_archive_bytes"] for result in program_results
        ),
        "discardable_archive_bytes": sum(
            result["discardable_archive_bytes"] for result in program_results
        ),
        "unmapped_candidate_bytes": sum(
            result["unmapped_candidate_bytes"] for result in program_results
        ),
        "post_archive_filter_ceiling_bytes": sum(
            result["post_archive_filter_ceiling_bytes"] for result in program_results
        ),
        "programs": program_results,
    }
    serialized_result = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output is not None:
        arguments.output.write_text(serialized_result, encoding="utf-8")
    if not arguments.quiet:
        print(serialized_result, end="")


if __name__ == "__main__":
    main()
