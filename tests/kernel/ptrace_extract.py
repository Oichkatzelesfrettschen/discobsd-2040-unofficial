"""Extract the production ptrace child-side admission unit for host gates."""

import argparse
import hashlib
from pathlib import Path

HELPER_START = "struct ptrace_args {"
HELPER_END = "/*\n * sys-trace system call."
PTRACE_SIGNATURE = "void\nptrace(void)"
PROCXMT_SIGNATURE = "int\nprocxmt(void)"


def extract_function(source: str, signature: str) -> str:
    """Return one unique function definition, including its closing brace."""
    if source.count(signature) != 1:
        raise ValueError("procxmt signature is not unique")
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
    raise ValueError("unterminated procxmt function")


def extract_helpers(source: str) -> str:
    """Return the private policy helpers that procxmt calls."""
    if source.count(HELPER_START) != 1 or source.count(HELPER_END) != 1:
        raise ValueError("ptrace helper boundary is not unique")
    start = source.index(HELPER_START)
    end = source.index(HELPER_END, start)
    return source[start:end].rstrip()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode()
    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + hashlib.sha256(source_bytes).hexdigest()
        + ". */\n"
        + extract_helpers(source)
        + "\n\n"
        + extract_function(source, PTRACE_SIGNATURE)
        + "\n\n"
        + extract_function(source, PROCXMT_SIGNATURE)
        + "\n"
    )


if __name__ == "__main__":
    main()
