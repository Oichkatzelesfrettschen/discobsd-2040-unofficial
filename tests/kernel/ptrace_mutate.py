"""Create calibrated ptrace admission mutants from extracted production code."""

import argparse
from pathlib import Path

MUTATIONS = {
    "negative-trace": (
        """    if (arguments->request == PT_TRACE_ME) {
""",
        """    if (arguments->request <= PT_TRACE_ME) {
""",
    ),
    "short-read": (
        """        if (copyin((caddr_t)ipc.ip_addr, (caddr_t)&data,
            sizeof data) != 0)
""",
        """        if (copyin((caddr_t)ipc.ip_addr, (caddr_t)&data, 1) != 0)
""",
    ),
    "partial-read-publication": (
        """        if (copyin((caddr_t)ipc.ip_addr, (caddr_t)&data,
            sizeof data) != 0)
            goto error;
        ipc.ip_data = data;
""",
        """        if (copyin((caddr_t)ipc.ip_addr, (caddr_t)&ipc.ip_data,
            sizeof ipc.ip_data) != 0)
            goto error;
        (void)data;
""",
    ),
    "misaligned-u-offset": (
        """    if (offset < 0 || offset % (int)sizeof(int) != 0)
        return 0;
""",
        """    if (offset < 0)
        return 0;
""",
    ),
    "u-upper-endpoint": (
        """    return (u_int)offset <= USIZE - sizeof(int);
""",
        """    return (u_int)offset <= USIZE;
""",
    ),
    "pc-misalignment": (
        """    if (program_counter % INSN_SZ != 0)
        return 0;
""",
        """    if (program_counter % INSN_SZ != 0 && program_counter == 0)
        return 0;
""",
    ),
    "sp-misalignment": (
        """    if (stack_pointer % 8U != 0)
        return 0;
""",
        """    if (stack_pointer % 8U != 0 && stack_pointer == 0)
        return 0;
""",
    ),
    "arbitrary-u-write": (
        """    if (word == (int *)&frame->tf_psr) {
        frame->tf_psr = (frame->tf_psr & ~PSR_NZCV) |
            ((u_int)data & PSR_NZCV);
        return 0;
    }
    return 1;
""",
        """    if (word == (int *)&frame->tf_psr) {
        frame->tf_psr = (frame->tf_psr & ~PSR_NZCV) |
            ((u_int)data & PSR_NZCV);
        return 0;
    }
    *word = data;
    return 0;
""",
    ),
    "psr-control-overwrite": (
        """        frame->tf_psr = (frame->tf_psr & ~PSR_NZCV) |
            ((u_int)data & PSR_NZCV);
""",
        """        frame->tf_psr = (u_int)data;
""",
    ),
    "continue-early-pc": (
        """        if (ipc.ip_data < 0 || ipc.ip_data >= NSIG)
            goto error;
        if ((u_int)(u_long)ipc.ip_addr != 1U &&
            !ptrace_pc_valid((u_int)(u_long)ipc.ip_addr))
            goto error;
        if ((u_int)(u_long)ipc.ip_addr != 1U)
            u.u_frame->tf_pc = (u_int)(u_long)ipc.ip_addr;
""",
        """        if ((u_int)(u_long)ipc.ip_addr != 1U)
            u.u_frame->tf_pc = (u_int)(u_long)ipc.ip_addr;
        if (ipc.ip_data < 0 || ipc.ip_data >= NSIG)
            goto error;
        if ((u_int)(u_long)ipc.ip_addr != 1U &&
            !ptrace_pc_valid((u_int)(u_long)ipc.ip_addr))
            goto error;
""",
    ),
    "signal-upper-endpoint": (
        """        if (ipc.ip_data < 0 || ipc.ip_data >= NSIG)
""",
        """        if (ipc.ip_data < 0 || ipc.ip_data > NSIG)
""",
    ),
    "step-fallthrough": (
        """    case PT_STEP:
        /* Cortex-M single-step needs a separate debug-monitor contract. */
        goto error;

    case PT_CONTINUE:
""",
        """    case PT_STEP:
        /* Mutant restores the inherited fallthrough. */

    case PT_CONTINUE:
""",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=sorted(MUTATIONS), required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text()
    before, after = MUTATIONS[arguments.mutation]
    if source.count(before) != 1:
        raise SystemExit(arguments.mutation + ": expected one production pattern")
    arguments.output.write_text(source.replace(before, after, 1))


if __name__ == "__main__":
    main()
