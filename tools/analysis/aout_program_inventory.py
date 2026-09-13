#!/usr/bin/env python3
"""Inventory every a.out/.dis program pair under a repository tree.

Walks a tree for `*.dis` disassembly listings, pairs each with its a.out
sibling binary, and records size, entry point, PRINTF_FLOAT Makefile
configuration, and whether the expected __doprnt_cvt linker root is present.

Inputs: a repository (or built distribution) tree root containing a.out
binaries with matching `.dis` disassemblies, such as games/*/*.dis under a
built userland tree. Standard library only (reuses aout_reachability's a.out
header parser).
"""

import argparse
import hashlib
import json
import pathlib
import re

from aout_reachability import DEFAULT_TEXT_BASE, parse_aout

PRINTF_FLOAT_PATTERN = re.compile(
    r"^\s*PRINTF_FLOAT\s*(?::|\?|\+)?=\s*yes(?:\s|$)", re.MULTILINE
)
FUNCTION_LABEL_PATTERN = re.compile(r"^\s*[0-9a-fA-F]+\s+<([^>]+)>:\s*$", re.MULTILINE)


def file_sha256(file_path):
    hash_state = hashlib.sha256()
    with file_path.open("rb") as input_file:
        for file_chunk in iter(lambda: input_file.read(1024 * 1024), b""):
            hash_state.update(file_chunk)
    return hash_state.hexdigest()


def has_printf_float(makefile_path):
    if not makefile_path.is_file():
        return False
    return (
        PRINTF_FLOAT_PATTERN.search(
            makefile_path.read_text(encoding="utf-8", errors="replace")
        )
        is not None
    )


def main():
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("--repo", type=pathlib.Path, required=True)
    argument_parser.add_argument("--output", type=pathlib.Path)
    argument_parser.add_argument("--quiet", action="store_true")
    arguments = argument_parser.parse_args()

    repository_root = arguments.repo.resolve()
    programs = []
    exclusions = []
    for disassembly_path in sorted(repository_root.rglob("*.dis")):
        aout_path = disassembly_path.with_suffix("")
        relative_disassembly_path = disassembly_path.relative_to(repository_root)
        program_name = str(relative_disassembly_path.with_suffix(""))
        if not aout_path.is_file():
            exclusions.append(
                {
                    "program": program_name,
                    "disassembly": str(disassembly_path),
                    "reason": "matching binary is absent",
                }
            )
            continue
        try:
            aout = parse_aout(aout_path, DEFAULT_TEXT_BASE)
        except RuntimeError as error:
            exclusions.append(
                {
                    "program": program_name,
                    "aout": str(aout_path),
                    "disassembly": str(disassembly_path),
                    "reason": str(error),
                }
            )
            continue

        disassembly_text = disassembly_path.read_text(
            encoding="utf-8", errors="replace"
        )
        function_names = set(FUNCTION_LABEL_PATTERN.findall(disassembly_text))
        makefile_path = disassembly_path.parent / "Makefile"
        printf_float = has_printf_float(makefile_path)
        linker_roots = []
        root_mismatches = []
        if printf_float:
            if "__doprnt_cvt" in function_names:
                linker_roots.append("__doprnt_cvt")
            else:
                root_mismatches.append(
                    "Makefile sets PRINTF_FLOAT=yes but __doprnt_cvt is absent"
                )

        programs.append(
            {
                "program": program_name,
                "aout": str(aout_path),
                "disassembly": str(disassembly_path),
                "binary_bytes": aout_path.stat().st_size,
                "binary_sha256": file_sha256(aout_path),
                "text_bytes": aout["text_size"],
                "data_bytes": aout["data_size"],
                "bss_bytes": aout["bss_size"],
                "entry_address": aout["entry_address"],
                "makefile": str(makefile_path) if makefile_path.is_file() else None,
                "printf_float": printf_float,
                "linker_roots": linker_roots,
                "root_mismatches": root_mismatches,
            }
        )

    result = {
        "repository_root": str(repository_root),
        "disassembly_count": len(programs) + len(exclusions),
        "program_count": len(programs),
        "excluded_count": len(exclusions),
        "printf_float_program_count": sum(
            bool(program["printf_float"]) for program in programs
        ),
        "linker_root_count": sum(len(program["linker_roots"]) for program in programs),
        "root_mismatch_count": sum(
            len(program["root_mismatches"]) for program in programs
        ),
        "programs": programs,
        "exclusions": exclusions,
    }
    serialized_result = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output is not None:
        arguments.output.write_text(serialized_result, encoding="utf-8")
    if not arguments.quiet:
        print(serialized_result, end="")


if __name__ == "__main__":
    main()
