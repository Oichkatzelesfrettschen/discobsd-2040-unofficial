"""Extract the production RP2040 kernel-span functions for the host gate."""

import argparse
import hashlib
from pathlib import Path

FUNCTIONS = (
    ("badkaddr_span", "int\nbadkaddr_span(caddr_t addr, u_int nbytes)"),
    ("badkaddr", "int\nbadkaddr(caddr_t addr)"),
)


def extract_function(source: str, signature: str, name: str) -> str:
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode()
    extracted = [
        extract_function(source, signature, name)
        for name, signature in FUNCTIONS
    ]
    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + hashlib.sha256(source_bytes).hexdigest()
        + ". */\n"
        + "\n\n".join(extracted)
        + "\n"
    )


if __name__ == "__main__":
    main()
