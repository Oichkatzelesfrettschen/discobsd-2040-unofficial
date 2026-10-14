"""Reject libc heap ownership in the target Bourne shell image."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

FORBIDDEN_SYMBOLS = (
    "calloc",
    "closedir",
    "free",
    "getcwd",
    "getwd",
    "malloc",
    "opendir",
    "readdir",
    "realloc",
)
SYMBOL_PATTERN = re.compile(r"^[0-9a-f]+ <([^>]+)>:$", re.MULTILINE)


def forbidden_symbols(disassembly: str) -> list[str]:
    linked_symbols = set(SYMBOL_PATTERN.findall(disassembly))
    return [name for name in FORBIDDEN_SYMBOLS if name in linked_symbols]


def self_test() -> None:
    bad = """\
20000000 <alloc>:
20000100 <malloc>:
20000200 <getwd>:
"""
    good = """\
20000000 <alloc>:
20000100 <afree>:
20000200 <getdir>:
"""
    if forbidden_symbols(bad) != ["getwd", "malloc"]:
        raise SystemExit("self-test failed to reject heap-backed shell symbols")
    if forbidden_symbols(good):
        raise SystemExit("self-test rejected shell-owned allocation symbols")
    print("shell target heap boundary self-test: PASS")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("disassembly", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    if arguments.self_test:
        self_test()
        return
    if arguments.disassembly is None:
        parser.error("disassembly is required without --self-test")

    offenders = forbidden_symbols(arguments.disassembly.read_text())
    if offenders:
        joined = ", ".join(offenders)
        raise SystemExit(f"target shell links libc heap owners: {joined}")
    print("shell target heap boundary: PASS")


if __name__ == "__main__":
    main()
