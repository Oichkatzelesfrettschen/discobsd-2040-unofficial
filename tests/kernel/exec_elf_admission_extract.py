"""Extract exact kernel ELF admission and completion functions."""

import argparse
import hashlib
from pathlib import Path

SIGNATURE = "int\nexec_elf_check(struct exec_params *epp)"
FINISH_SIGNATURE = (
    "static void\nexec_finish(struct exec_params *epp, struct inode *ip)"
)
ESTABLISH_SIGNATURE = "int exec_estab(struct exec_params *epp)"
SETUPSTACK_SIGNATURE = (
    "void exec_setupstack(unsigned entryaddr, struct exec_params *epp)"
)
SWAPTEXT_SIGNATURE = (
    "static size_t\nswaptext (p)\n    register struct proc *p"
)
TEXT_RELEASE_SIGNATURE = (
    "void\nexec_text_release(struct proc *p, struct inode *next)"
)
SEGMENT_SPAN_CHECK = """    if (ph->p_offset > UINT_MAX - ph->p_filesz ||
        (unsigned long)ph->p_offset + ph->p_filesz >
        (unsigned long)epp->ip->i_size)
        return ENOEXEC;"""
SEGMENT_SPAN_MUTANT = """    if (ph->p_offset > UINT_MAX - ph->p_filesz)
        return ENOEXEC;"""
READ_FAILURE_CHECK = """    if (error != 0 || resid != 0) {
        epp->flags |= EXEC_COMMITTED_FAILURE;
        return error != 0 ? error : EIO;
    }"""
READ_FAILURE_MUTANT = """    if (error != 0 || resid != 0)
        return error != 0 ? error : EIO;"""
FINISH_TERMINATION = """    if (terminate)
        exit(SIGKILL);"""
FINISH_SIGNAL_MUTANT = """    if (terminate)
        psignal(u.u_procp, SIGKILL);"""
STACK_ARGUMENT_GEOMETRY = (
    "    argp = (char **)((char *)envp - (epp->argc+1)*NBPW); "
    "/* argv and NULL */"
)
STACK_ARGUMENT_GEOMETRY_MUTANT = (
    "    argp = envp - (epp->argc+1)*NBPW;"
)
STACK_POINTER_GEOMETRY = (
    "    u.u_frame->tf_sp = (int)((char *)argp - sizeof *u.u_frame);"
)
STACK_POINTER_GEOMETRY_MUTANT = (
    "    u.u_frame->tf_sp = (int)(argp-0x40);"
)
TEXT_OWNERSHIP = """    exec_text_release(u.u_procp, NULL);
    u.u_procp->p_tsize = 0;"""


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


def select_undefined_branch(source: str, signature: str, symbol: str) -> str:
    """Select one simple #ifdef branch after a named function signature."""
    signature_offset = source.index(signature)
    directive = f"#ifdef {symbol}\n"
    conditional_offset = source.index(directive, signature_offset)
    alternative_offset = source.index("#else\n", conditional_offset)
    end_offset = source.index("#endif\n", alternative_offset)
    return (
        source[:conditional_offset]
        + source[alternative_offset + len("#else\n") : end_offset]
        + source[end_offset + len("#endif\n") :]
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    mutations = parser.add_mutually_exclusive_group()
    mutations.add_argument("--segment-span-mutant", action="store_true")
    mutations.add_argument("--read-failure-mutant", action="store_true")
    mutations.add_argument("--finish", action="store_true")
    mutations.add_argument("--finish-signal-mutant", action="store_true")
    mutations.add_argument("--establish", action="store_true")
    mutations.add_argument("--setupstack", action="store_true")
    mutations.add_argument("--setupstack-geometry-mutant", action="store_true")
    mutations.add_argument("--swaptext", action="store_true")
    mutations.add_argument("--text-ownership-mutant", action="store_true")
    mutations.add_argument("--text-release", action="store_true")
    arguments = parser.parse_args()

    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode()
    if arguments.finish or arguments.finish_signal_mutant:
        signature = FINISH_SIGNATURE
    elif arguments.establish:
        signature = ESTABLISH_SIGNATURE
        source = select_undefined_branch(source, signature, "SWAPRAM")
    elif arguments.setupstack or arguments.setupstack_geometry_mutant:
        signature = SETUPSTACK_SIGNATURE
    elif arguments.swaptext:
        signature = SWAPTEXT_SIGNATURE
    elif arguments.text_release:
        signature = TEXT_RELEASE_SIGNATURE
    else:
        signature = SIGNATURE
    function = extract_function(source, signature)
    if arguments.segment_span_mutant:
        if function.count(SEGMENT_SPAN_CHECK) != 1:
            raise SystemExit("segment-span mutation does not match source")
        function = function.replace(SEGMENT_SPAN_CHECK, SEGMENT_SPAN_MUTANT, 1)
    elif arguments.read_failure_mutant:
        if function.count(READ_FAILURE_CHECK) != 1:
            raise SystemExit("read-failure mutation does not match source")
        function = function.replace(READ_FAILURE_CHECK, READ_FAILURE_MUTANT, 1)
    elif arguments.finish_signal_mutant:
        if function.count(FINISH_TERMINATION) != 1:
            raise SystemExit("exec-finish mutation does not match source")
        function = function.replace(
            FINISH_TERMINATION, FINISH_SIGNAL_MUTANT, 1
        )
    elif arguments.setupstack_geometry_mutant:
        if function.count(STACK_ARGUMENT_GEOMETRY) != 1:
            raise SystemExit("stack argument-geometry mutation does not match source")
        if function.count(STACK_POINTER_GEOMETRY) != 1:
            raise SystemExit("stack pointer-geometry mutation does not match source")
        function = function.replace(
            STACK_ARGUMENT_GEOMETRY, STACK_ARGUMENT_GEOMETRY_MUTANT, 1
        )
        function = function.replace(
            STACK_POINTER_GEOMETRY, STACK_POINTER_GEOMETRY_MUTANT, 1
        )
    elif arguments.text_ownership_mutant:
        if function.count(TEXT_OWNERSHIP) != 1:
            raise SystemExit("ELF text-ownership mutation does not match source")
        function = function.replace(TEXT_OWNERSHIP, "", 1)
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
