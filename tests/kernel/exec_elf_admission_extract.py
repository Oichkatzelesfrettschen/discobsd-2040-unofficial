"""Extract the kernel ELF loader for an exact-source ILP32 fixture."""

import argparse
import hashlib
from pathlib import Path

SIGNATURE = "int\nexec_elf_check(struct exec_params *epp)"
SEGMENT_SPAN_CHECK = """    if (ph->p_offset > UINT_MAX - ph->p_filesz ||
        (unsigned long)ph->p_offset + ph->p_filesz >
        (unsigned long)epp->ip->i_size)
        return ENOEXEC;"""
SEGMENT_SPAN_MUTANT = """    if (ph->p_offset > UINT_MAX - ph->p_filesz)
        return ENOEXEC;"""
READ_FAILURE_CHECK = """    if (error != 0 || resid != 0) {
        psignal(u.u_procp, SIGKILL);
        return error != 0 ? error : EIO;
    }"""
READ_FAILURE_MUTANT = """    (void)error;
    (void)resid;"""


def extract_function(source: str, signature: str) -> str:
    """Return one uniquely named C function, including its closing brace."""
    if source.count(signature) != 1:
        raise ValueError("ELF loader signature is not unique")
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
    raise ValueError("unterminated ELF loader function")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    mutations = parser.add_mutually_exclusive_group()
    mutations.add_argument("--segment-span-mutant", action="store_true")
    mutations.add_argument("--read-failure-mutant", action="store_true")
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    function = extract_function(source_bytes.decode(), SIGNATURE)
    if arguments.segment_span_mutant:
        if function.count(SEGMENT_SPAN_CHECK) != 1:
            raise SystemExit("segment-span mutation does not match source")
        function = function.replace(SEGMENT_SPAN_CHECK, SEGMENT_SPAN_MUTANT, 1)
    elif arguments.read_failure_mutant:
        if function.count(READ_FAILURE_CHECK) != 1:
            raise SystemExit("read-failure mutation does not match source")
        function = function.replace(READ_FAILURE_CHECK, READ_FAILURE_MUTANT, 1)
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + source_hash
        + ". */\n"
        + function
        + "\n"
    )


if __name__ == "__main__":
    main()
