"""Extract the production tty local-mode arithmetic for the host gate."""

import argparse
import hashlib
from pathlib import Path

FUNCTIONS = (
    ("tty_local_mode_bis", "static uint32_t\ntty_local_mode_bis("),
    ("tty_local_mode_bic", "static uint32_t\ntty_local_mode_bic("),
    ("tty_local_mode_set", "static uint32_t\ntty_local_mode_set("),
    ("tty_local_mode_get", "static uint32_t\ntty_local_mode_get("),
)

CALLS = (
    "tp->t_flags = tty_local_mode_bis(tp->t_flags, (uint32_t)*(int *)data);",
    "tp->t_flags = tty_local_mode_bic(tp->t_flags, (uint32_t)*(int *)data);",
    "tp->t_flags = tty_local_mode_set(tp->t_flags, (uint32_t)*(int *)data);",
    "*(int *)data = (int)tty_local_mode_get(tp->t_flags);",
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
    extracted = []
    for name, signature in FUNCTIONS:
        extracted.append(extract_function(source, signature, name))
    for call in CALLS:
        if source.count(call) != 1:
            raise ValueError("ttioctl integration is not unique: " + call)

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
