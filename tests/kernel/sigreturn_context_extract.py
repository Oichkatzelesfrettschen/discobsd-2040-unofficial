"""Extract the RP2040 sigreturn implementation for ILP32 execution."""

import argparse
import hashlib
from pathlib import Path

SIGNATURE = "void\nsigreturn(void)"
HELPER_START = "#define SIGRETURN_EXCEPTION_FRAME_SIZE"
HELPER_END = "/*\n * System call to cleanup state after a signal"
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
SNAPSHOT_PUBLICATION = "\tu.u_error = EJUSTRETURN;"
REREAD_PUBLICATION = """\tscp = (struct sigcontext *)user_context;
\tu.u_error = EJUSTRETURN;"""

MUTATIONS = {
    "direct-user": (SNAPSHOT_DECLARATIONS, DIRECT_DECLARATIONS),
    "reread-user": (SNAPSHOT_PUBLICATION, REREAD_PUBLICATION),
    "pc-admission": (
        """\tif (!machine_user_pc_valid(context->sc_pc))
\t\treturn 0;""",
        """\tif (0 && !machine_user_pc_valid(context->sc_pc))
\t\treturn 0;""",
    ),
    "sp-alignment": (
        """\treturn machine_user_stack_valid(context->sc_sp, frame_size);""",
        """\treturn !baduaddr_span((caddr_t)(u_long)context->sc_sp,
\t    frame_size);""",
    ),
    "frame-width": (
        "#define SIGRETURN_EXCEPTION_FRAME_SIZE (8U * sizeof(u_int))",
        "#define SIGRETURN_EXCEPTION_FRAME_SIZE (7U * sizeof(u_int))",
    ),
    "padding-width": (
        """\tif (context->sc_psr & XPSR_STKALIGN)
\t\tframe_size += sizeof(u_int);""",
        """\tif (context->sc_psr & XPSR_STKALIGN)
\t\tframe_size += 0;""",
    ),
    "psr-policy": (
        """\tif (!sigreturn_psr_valid(context->sc_psr))
\t\treturn 0;""",
        """\tif (0 && !sigreturn_psr_valid(context->sc_psr))
\t\treturn 0;""",
    ),
    "early-publication": (
        """\tif (!sigreturn_context_valid(scp)) {
\t\tu.u_error = EINVAL;""",
        """\tu.u_procp->p_sigmask = scp->sc_mask & ~sigcantmask;
\tif (!sigreturn_context_valid(scp)) {
\t\tu.u_error = EINVAL;""",
    ),
}


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


def extract_helpers(source):
    """Return the private constants and validators called by sigreturn."""
    if source.count(HELPER_START) != 1 or source.count(HELPER_END) != 1:
        raise ValueError("sigreturn helper boundary is not unique")
    start = source.index(HELPER_START)
    end = source.index(HELPER_END, start)
    return source[start:end].rstrip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=sorted(MUTATIONS))
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode()
    unit = extract_helpers(source) + "\n\n" + extract_function(source, SIGNATURE)
    if arguments.mutation == "direct-user":
        if unit.count(SNAPSHOT_DECLARATIONS) != 1:
            raise SystemExit("direct-user declaration mutation does not match source")
        if unit.count(SNAPSHOT_ADMISSION) != 1:
            raise SystemExit("direct-user admission mutation does not match source")
        unit = unit.replace(SNAPSHOT_DECLARATIONS, DIRECT_DECLARATIONS, 1).replace(
            SNAPSHOT_ADMISSION, DIRECT_ADMISSION, 1
        )
    elif arguments.mutation:
        before, after = MUTATIONS[arguments.mutation]
        if unit.count(before) != 1:
            raise SystemExit(arguments.mutation + ": expected one production pattern")
        unit = unit.replace(before, after, 1)

    source_hash = hashlib.sha256(source_bytes).hexdigest()
    arguments.output.write_text(
        "/* Extracted from "
        + arguments.source.as_posix()
        + " SHA-256 "
        + source_hash
        + ". */\n"
        + unit
        + "\n"
    )


if __name__ == "__main__":
    main()
