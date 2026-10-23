"""Extract the RP2040 sigreturn implementation for ILP32 execution."""

import argparse
import hashlib
from pathlib import Path

SIGNATURE = "void\nsigreturn(void)"
SNAPSHOT_DECLARATIONS = """\tstruct sigcontext context;
\tstruct sigcontext *scp = &context;
\tu_int user_context;"""
DIRECT_DECLARATIONS = """\tstruct sigcontext *scp;
\tu_int user_context;"""
SNAPSHOT_ADMISSION = """\tif (regs->tf_sp > ~0U - 32U) {
\t\tu.u_error = EFAULT;
\t\treturn;
\t}
\tuser_context = regs->tf_sp + 32U;
\tif (copyin((caddr_t)user_context, (caddr_t)scp,
\t    sizeof(*scp)) != 0) {"""
DIRECT_ADMISSION = """\tuser_context = regs->tf_sp + 32U;
\tscp = (struct sigcontext *)user_context;
\tif (baduaddr((caddr_t)scp) ||
\t    baduaddr((caddr_t)scp + sizeof(*scp))) {"""


def extract_function(source, signature):
    """Return one uniquely named C function, including its closing brace."""
    if source.count(signature) != 1:
        raise ValueError("sigreturn signature is not unique")
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
    raise ValueError("unterminated sigreturn function")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--direct-user-mutant", action="store_true")
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    function = extract_function(source_bytes.decode(), SIGNATURE)
    if arguments.direct_user_mutant:
        if function.count(SNAPSHOT_DECLARATIONS) != 1:
            raise SystemExit("direct-user declaration mutation does not match source")
        if function.count(SNAPSHOT_ADMISSION) != 1:
            raise SystemExit("direct-user admission mutation does not match source")
        function = function.replace(
            SNAPSHOT_DECLARATIONS, DIRECT_DECLARATIONS, 1
        ).replace(SNAPSHOT_ADMISSION, DIRECT_ADMISSION, 1)

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
