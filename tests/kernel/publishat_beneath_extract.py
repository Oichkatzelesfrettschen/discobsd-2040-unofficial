#!/usr/bin/env python3
"""Extract the production conditional-publication syscall and descriptor helper."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from unlinkat_beneath_extract import INCLUDES, extract_function

FUNCTIONS = (
    (
        "publish_target_error",
        "static int\npublish_target_error(struct inode *expected_staged,",
    ),
    ("publishat_beneath", "void\npublishat_beneath(void)"),
    ("getinode", "struct inode *\ngetinode(int fdes)"),
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode("utf-8")
    functions = [
        extract_function(source, signature, name)
        for name, signature in FUNCTIONS
    ]
    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + hashlib.sha256(source_bytes).hexdigest()
        + ". */\n"
        + INCLUDES
        + "\n"
        + "\n\n".join(functions)
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
