#!/usr/bin/env python3
"""Cross-check Capstone against cstool, radare2, and rizin on named paths.

Decodes the Thumb byte ranges belonging to a fixed list of security-relevant
functions (Reset_Handler, copyin/copyout, signal delivery, interrupt
handlers, USB and swap paths) with Capstone, then re-decodes the same bytes
with cstool, r2, and rizin and reports any instruction-stream disagreement.

Inputs: one ELF with Thumb code, mapping symbols, and sized STT_FUNC symbols
for the named targets (the built kernel at
sys/arch/rp2040/compile/PICO/unix), and a scratch directory for the
extracted per-function byte segments. Third-party module: capstone and
pyelftools (elftools, via reconcile_disassembly). Requires cstool, r2, and
rizin on PATH.
"""

import json
import itertools
import pathlib
import re
import subprocess
import sys

import capstone
from elftools.elf.elffile import ELFFile

from reconcile_disassembly import bytes_for_range, load_elf_metadata, mapping_ranges


CSTOOL_LINE_PATTERN = re.compile(
    r"^([0-9a-fA-F]+)\s+((?:[0-9a-fA-F]{2}(?:\s+|$))+)", re.MULTILINE
)
TARGET_FUNCTIONS = (
    "Reset_Handler",
    "longjmp",
    "bzero",
    "copyout",
    "copyin",
    "sendsig",
    "sigreturn",
    "SVC_Handler",
    "PendSV_Handler",
    "uartintr",
    "usb_tx_kick",
    "usb_service",
    "swapram_out",
    "swapram_put",
    "swapram_in",
)


def run_command(command):
    completed_process = subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return completed_process.stdout


def load_function_bounds(elf_path):
    function_bounds = {}
    with elf_path.open("rb") as elf_stream:
        elf_file = ELFFile(elf_stream)
        symbol_table = elf_file.get_section_by_name(".symtab")
        for symbol in symbol_table.iter_symbols():
            if symbol.name not in TARGET_FUNCTIONS:
                continue
            symbol_size = int(symbol["st_size"])
            if symbol_size == 0:
                continue
            symbol_start = int(symbol["st_value"]) & ~1
            function_bounds[symbol.name] = (symbol_start, symbol_start + symbol_size)
    missing_names = sorted(set(TARGET_FUNCTIONS) - set(function_bounds))
    if missing_names:
        raise RuntimeError(f"missing sized function symbols: {missing_names}")
    return function_bounds


def capstone_tuples(segment_bytes, segment_start):
    decoder = capstone.Cs(
        capstone.CS_ARCH_ARM,
        capstone.CS_MODE_THUMB
        | capstone.CS_MODE_LITTLE_ENDIAN
        | capstone.CS_MODE_MCLASS,
    )
    return [
        (instruction.address, instruction.size, instruction.bytes.hex())
        for instruction in decoder.disasm(segment_bytes, segment_start)
    ]


def radare_tuples(tool_name, segment_path, segment_start, segment_size):
    output = run_command(
        [
            tool_name,
            "-N",
            "-q",
            "-a",
            "arm",
            "-b",
            "16",
            "-m",
            f"0x{segment_start:x}",
            "-c",
            f"pDj {segment_size}",
            str(segment_path),
        ]
    )
    decoded_rows = json.loads(output)
    address_key = "addr" if tool_name == "r2" else "offset"
    return [
        (int(row[address_key]), int(row["size"]), row["bytes"].lower())
        for row in decoded_rows
    ]


def cstool_tuples(segment_bytes, segment_start):
    output = run_command(
        ["cstool", "cortexm", segment_bytes.hex(), f"0x{segment_start:x}"]
    )
    decoded_rows = []
    for line_match in CSTOOL_LINE_PATTERN.finditer(output):
        instruction_bytes = bytes.fromhex(line_match.group(2))
        decoded_rows.append(
            (
                int(line_match.group(1), 16),
                len(instruction_bytes),
                instruction_bytes.hex(),
            )
        )
    return decoded_rows


def function_thumb_segments(function_bounds, thumb_ranges):
    segments = []
    for function_name, (function_start, function_end) in function_bounds.items():
        for thumb_range in thumb_ranges:
            segment_start = max(function_start, thumb_range["start"])
            segment_end = min(function_end, thumb_range["end"])
            if segment_start >= segment_end:
                continue
            segments.append(
                {
                    "function": function_name,
                    "section": thumb_range["section"],
                    "start": segment_start,
                    "end": segment_end,
                }
            )
    return sorted(segments, key=lambda item: (item["start"], item["end"]))


def summarize(elf_path, analysis_directory):
    executable_sections, mapping_symbols_by_section = load_elf_metadata(elf_path)
    thumb_ranges = [
        item
        for item in mapping_ranges(executable_sections, mapping_symbols_by_section)
        if item["kind"] == "t"
    ]
    function_bounds = load_function_bounds(elf_path)
    segments = function_thumb_segments(function_bounds, thumb_ranges)
    decoder_mismatches = []
    total_instructions = 0

    for segment in segments:
        segment_range = {
            "section": segment["section"],
            "start": segment["start"],
            "end": segment["end"],
        }
        segment_bytes = bytes_for_range(executable_sections, segment_range)
        segment_filename = (
            f"{segment['function']}-{segment['start']:08x}-{segment['end']:08x}.bin"
        )
        segment_path = analysis_directory / segment_filename
        segment_path.write_bytes(segment_bytes)

        expected_rows = capstone_tuples(segment_bytes, segment["start"])
        total_instructions += len(expected_rows)
        observed_by_decoder = {
            "cstool": cstool_tuples(segment_bytes, segment["start"]),
            "radare2": radare_tuples(
                "r2", segment_path, segment["start"], len(segment_bytes)
            ),
            "rizin": radare_tuples(
                "rizin", segment_path, segment["start"], len(segment_bytes)
            ),
        }
        for decoder_name, observed_rows in observed_by_decoder.items():
            if observed_rows != expected_rows:
                differing_rows = [
                    (row_index, expected_row, observed_row)
                    for row_index, (expected_row, observed_row) in enumerate(
                        itertools.zip_longest(expected_rows, observed_rows)
                    )
                    if expected_row != observed_row
                ]
                decoder_mismatches.append(
                    {
                        "function": segment["function"],
                        "start": f"0x{segment['start']:08x}",
                        "end": f"0x{segment['end']:08x}",
                        "decoder": decoder_name,
                        "expected_count": len(expected_rows),
                        "observed_count": len(observed_rows),
                        "difference_count": len(differing_rows),
                        "examples": [
                            {
                                "row": row_index,
                                "expected": expected_row,
                                "observed": observed_row,
                            }
                            for row_index, expected_row, observed_row in differing_rows[:8]
                        ],
                    }
                )

    return {
        "function_count": len(function_bounds),
        "segment_count": len(segments),
        "instruction_count": total_instructions,
        "decoder_mismatches": decoder_mismatches,
        "functions": {
            function_name: {
                "start": f"0x{bounds[0]:08x}",
                "end": f"0x{bounds[1]:08x}",
                "size": bounds[1] - bounds[0],
                "thumb_segments": sum(
                    1 for segment in segments if segment["function"] == function_name
                ),
                "thumb_bytes": sum(
                    segment["end"] - segment["start"]
                    for segment in segments
                    if segment["function"] == function_name
                ),
            }
            for function_name, bounds in sorted(function_bounds.items())
        },
    }


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: crosscheck_named_paths.py ELF ANALYSIS_DIRECTORY")
    elf_path = pathlib.Path(sys.argv[1])
    analysis_directory = pathlib.Path(sys.argv[2])
    analysis_directory.mkdir(parents=True, exist_ok=True)
    print(json.dumps(summarize(elf_path, analysis_directory), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
