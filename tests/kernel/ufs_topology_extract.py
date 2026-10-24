#!/usr/bin/env python3
"""Extract the production link, unlink, and rename syscall definitions."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

FUNCTIONS = (
    ("link", "void\nlink(void)"),
    ("unlink", "void\nunlink(void)"),
    ("rename", "void\nrename(void)"),
)

INCLUDES = """#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/namei.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kernel.h>
#include <sys/proc.h>
"""


def extract_function(source: str, signature: str, name: str) -> str:
    if source.count(signature) != 1:
        raise ValueError(f"{name} signature is not unique")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for offset in range(opening, len(source)):
        character = source[offset]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start : offset + 1]
    raise ValueError(f"unterminated {name} function")


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
