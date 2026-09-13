#!/usr/bin/env python3
"""Find PRIMASK saves (mrs ... primask) whose result is discarded.

Flags every `mrs`-then-`cpsid`/`cpsie` pair in the kernel's Thumb code whose
call site discards the saved PRIMASK value (an `spl()`-style save that is
never restored), by resolving each address back to its C source line through
DWARF inline-frame info.

Inputs: the built kernel ELF (sys/arch/rp2040/compile/PICO/unix after
`bmake MACHINE=rp2040 kernel`), the repository tree root, and the compiler's
recorded build directory (DW_AT_comp_dir), used to map compiled source paths
back onto the current checkout. Third-party modules: capstone and lief (via
reconcile_disassembly) and pyelftools (elftools, via reconcile_disassembly).
Requires arm-none-eabi-addr2line on PATH.
"""

import argparse
import pathlib
import re
import subprocess

from thumb_peepholes import decode_all
from reconcile_disassembly import load_elf_metadata, mapping_ranges


LOCATION_PATTERN = re.compile(r"^(.*):(\d+)(?: \(discriminator \d+\))?$")


def inline_chain(elf_path, address):
    completed_process = subprocess.run(
        [
            "arm-none-eabi-addr2line",
            "-f",
            "-i",
            "-e",
            str(elf_path),
            f"0x{address:x}",
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    output_lines = completed_process.stdout.splitlines()
    return [
        (output_lines[index], output_lines[index + 1])
        for index in range(0, len(output_lines) - 1, 2)
    ]


def current_source_line(repository_path, debug_location, compiled_prefix):
    location_match = LOCATION_PATTERN.match(debug_location)
    if location_match is None:
        return None, None, None
    compiled_path = pathlib.Path(location_match.group(1))
    source_line_number = int(location_match.group(2))
    try:
        relative_path = compiled_path.relative_to(compiled_prefix)
    except ValueError:
        return compiled_path, source_line_number, None
    current_path = repository_path / relative_path
    if not current_path.is_file():
        return current_path, source_line_number, None
    source_lines = current_path.read_text(encoding="utf-8").splitlines()
    if source_line_number < 1 or source_line_number > len(source_lines):
        return current_path, source_line_number, None
    return current_path, source_line_number, source_lines[source_line_number - 1].strip()


def main():
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("elf", type=pathlib.Path, help="kernel ELF")
    argument_parser.add_argument(
        "repository", type=pathlib.Path, help="repository tree root"
    )
    argument_parser.add_argument(
        "--compiled-prefix",
        type=pathlib.Path,
        default=None,
        help=(
            "build directory recorded in DW_AT_comp_dir when the ELF was "
            "compiled (default: REPOSITORY/sys/arch/rp2040/compile/PICO)"
        ),
    )
    arguments = argument_parser.parse_args()
    elf_path = arguments.elf
    repository_path = arguments.repository
    compiled_prefix = (
        arguments.compiled_prefix
        or repository_path / "sys/arch/rp2040/compile/PICO"
    )
    executable_sections, mapping_symbols_by_section = load_elf_metadata(elf_path)
    thumb_ranges = [
        item
        for item in mapping_ranges(executable_sections, mapping_symbols_by_section)
        if item["kind"] == "t"
    ]
    instructions = decode_all(executable_sections, thumb_ranges)
    dead_save_rows = []
    paired_save_count = 0

    for instruction_index, instruction in enumerate(instructions[:-1]):
        if instruction.mnemonic != "mrs" or "primask" not in instruction.op_str:
            continue
        next_instruction = instructions[instruction_index + 1]
        if next_instruction.address != instruction.address + instruction.size:
            continue
        if next_instruction.mnemonic not in {"cpsid", "cpsie"}:
            continue
        paired_save_count += 1
        chain = inline_chain(elf_path, instruction.address)
        caller_function, caller_location = chain[-1]
        current_path, source_line_number, source_text = current_source_line(
            repository_path, caller_location, compiled_prefix
        )
        if source_text is None:
            continue
        call_text = source_text.split("/*", 1)[0].strip()
        assignment_prefix = call_text.split("spl", 1)[0].split("arm_intr", 1)[0]
        result_is_discarded = "(void)" in call_text or "=" not in assignment_prefix
        if result_is_discarded:
            dead_save_rows.append(
                {
                    "address": instruction.address,
                    "transition": next_instruction.mnemonic,
                    "function": caller_function,
                    "path": current_path,
                    "line": source_line_number,
                    "source": source_text,
                }
            )

    print(f"paired PRIMASK saves: {paired_save_count}")
    print(f"discarded PRIMASK saves: {len(dead_save_rows)}")
    print(f"candidate instruction bytes: {4 * len(dead_save_rows)}")
    for row in dead_save_rows:
        print(
            f"0x{row['address']:08x} | {row['transition']:5s} | "
            f"{row['function']} | {row['path']}:{row['line']} | {row['source']}"
        )


if __name__ == "__main__":
    main()
