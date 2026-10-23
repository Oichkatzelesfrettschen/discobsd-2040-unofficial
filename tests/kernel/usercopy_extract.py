"""Extract the RP2040 user-copy functions for the ILP32 behavior gate."""

import argparse
import hashlib
from pathlib import Path


def extract_function(source, signature):
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
    raise ValueError("unterminated function: " + signature)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--endpoint-mutant", action="store_true")
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode()
    signatures = []
    span_signature = "int\nbaduaddr_span(caddr_t addr, u_int nbytes)"
    if span_signature in source:
        signatures.append(span_signature)
    signatures.extend(
        (
            "int\nbaduaddr(caddr_t addr)",
            "int\ncopyout(caddr_t from, caddr_t to, u_int nbytes)",
            "int\ncopyin(caddr_t from, caddr_t to, u_int nbytes)",
        )
    )
    functions = [extract_function(source, signature) for signature in signatures]
    extracted = "\n\n".join(functions) + "\n"
    if arguments.endpoint_mutant:
        copyout_check = "if (baduaddr_span(to, nbytes))"
        copyin_check = "if (baduaddr_span(from, nbytes))"
        if copyout_check not in extracted or copyin_check not in extracted:
            raise SystemExit("endpoint mutation does not match production source")
        extracted = extracted.replace(
            copyout_check,
            "if (baduaddr(to) || baduaddr(to + nbytes - 1))",
            1,
        )
        extracted = extracted.replace(
            copyin_check,
            "if (baduaddr(from) || baduaddr(from + nbytes - 1))",
            1,
        )
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + source_hash
        + ". */\n"
        + extracted
    )


if __name__ == "__main__":
    main()
