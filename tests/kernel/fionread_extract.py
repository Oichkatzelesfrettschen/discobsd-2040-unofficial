"""Extract the production FIONREAD functions for the host behavior gate."""

import argparse
import hashlib
from pathlib import Path

SIGNATURES = {
    "ioctl": "void\nioctl(void)",
    "ino_ioctl": (
        "int\nino_ioctl(register struct file *fp, register u_int com, caddr_t data)"
    ),
}
INCLUDES = """#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/file.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/swap.h>
#include <sys/kernel.h>
#include <sys/systm.h>
"""


def extract_function(source, signature, name):
    """Return one unique function definition, including its closing brace."""
    if source.count(signature) != 1:
        raise ValueError(name + " signature is not unique")
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
    raise ValueError("unterminated " + name + " function")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--function", choices=SIGNATURES, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    function = extract_function(
        source_bytes.decode(), SIGNATURES[arguments.function], arguments.function
    )
    marker = "case FIONREAD:" if arguments.function == "ioctl" else "com == FIONREAD"
    if function.count(marker) != 1:
        raise SystemExit(
            "FIONREAD marker does not occur exactly once in " + arguments.function
        )
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + source_hash
        + ". */\n"
        + INCLUDES
        + function
        + "\n"
    )


if __name__ == "__main__":
    main()
