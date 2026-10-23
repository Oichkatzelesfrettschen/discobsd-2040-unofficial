"""Extract the RP2040 syscall argument-admission block for ILP32 execution."""

import argparse
import hashlib
from pathlib import Path

START = "\tconst volatile u_int dispatch_code = code < nsysent ? (u_int)code : 0;\n"
END = "\n\tif (SYSTRACE_ON(SYSTRACE_SYSCALL)) {"
DISPATCH_GUARD = "\tif (u.u_error == 0 && setjmp(&u.u_qsave) == 0) {"
UNCHECKED_DISPATCH = "\tif (setjmp(&u.u_qsave) == 0) {"


def extract_block(source):
    """Return the uniquely anchored production block without rewriting it."""
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError("syscall argument-admission anchors are not unique")
    start = source.index(START)
    end = source.index(END, start)
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dispatch-after-failure-mutant", action="store_true")
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    block = extract_block(source_bytes.decode())
    if arguments.dispatch_after_failure_mutant:
        if block.count(DISPATCH_GUARD) != 1:
            raise SystemExit("dispatch-after-failure mutation does not match source")
        block = block.replace(DISPATCH_GUARD, UNCHECKED_DISPATCH, 1)

    source_hash = hashlib.sha256(source_bytes).hexdigest()
    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + source_hash
        + ". */\n"
        + block
        + "\n"
    )


if __name__ == "__main__":
    main()
