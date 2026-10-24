"""Create calibrated mutations of the tty local-mode helpers."""

import argparse
from pathlib import Path

MUTATIONS = {
    "lost-local-high-bit": (
        "return flags | ((local_mode & UINT32_C(0xffff)) << 16);",
        "return flags | ((local_mode & UINT32_C(0x7fff)) << 16);",
    ),
    "bis-and": (
        "return flags | ((local_mode & UINT32_C(0xffff)) << 16);",
        "return flags & ((local_mode & UINT32_C(0xffff)) << 16);",
    ),
    "bic-or": (
        "return flags & ~((local_mode & UINT32_C(0xffff)) << 16);",
        "return flags | ((local_mode & UINT32_C(0xffff)) << 16);",
    ),
    "set-retains-high": (
        "return (flags & UINT32_C(0xffff)) |\n"
        "        ((local_mode & UINT32_C(0xffff)) << 16);",
        "return flags | ((local_mode & UINT32_C(0xffff)) << 16);",
    ),
    "signed-get": (
        "return flags >> 16;",
        "return (uint32_t)((int32_t)flags >> 16);",
    ),
    "signed-shift": (
        "return flags | ((local_mode & UINT32_C(0xffff)) << 16);",
        "return flags | (uint32_t)((long)(local_mode & UINT32_C(0xffff)) << 16);",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=MUTATIONS, required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text()
    original, replacement = MUTATIONS[arguments.mutation]
    if source.count(original) != 1:
        raise ValueError(arguments.mutation + " mutation site is not unique")
    arguments.output.write_text(source.replace(original, replacement, 1))


if __name__ == "__main__":
    main()
