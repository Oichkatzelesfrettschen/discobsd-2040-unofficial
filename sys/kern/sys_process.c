/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/inode.h>
#include <sys/vm.h>
#include <sys/ptrace.h>
#include <machine/frame.h>

struct ipc ipc;

struct ptrace_args {
    int request;
    int pid;
    int *address;
    int data;
};

/* The exception return reads eight words from the process stack. */
#define PTRACE_EXCEPTION_FRAME_SIZE (8U * sizeof(u_int))

static int
ptrace_user_word_offset_valid(int offset)
{
    if (offset < 0 || offset % (int)sizeof(int) != 0)
        return 0;
    return (u_int)offset <= USIZE - sizeof(int);
}

static int
ptrace_pc_valid(u_int program_counter)
{
    if (program_counter % INSN_SZ != 0)
        return 0;
    return !baduaddr_span((caddr_t)(u_long)program_counter, INSN_SZ);
}

static int
ptrace_sp_valid(u_int stack_pointer)
{
    if (stack_pointer % 8U != 0)
        return 0;
    return !baduaddr_span((caddr_t)(u_long)stack_pointer,
        PTRACE_EXCEPTION_FRAME_SIZE);
}

/*
 * PT_WRITE_U addresses a word by its byte offset from the user-area base.
 * Only saved user registers are writable. The stack and program counters
 * receive their own admission checks, while xPSR keeps every architectural
 * field except the user-visible condition flags.
 */
static int
ptrace_write_frame_word(int *word, int data)
{
    struct trapframe *frame = u.u_frame;

    if (word == (int *)&frame->tf_r0 ||
        word == (int *)&frame->tf_r1 ||
        word == (int *)&frame->tf_r2 ||
        word == (int *)&frame->tf_r3 ||
        word == (int *)&frame->tf_lr ||
        word == (int *)&frame->tf_r4 ||
        word == (int *)&frame->tf_r5 ||
        word == (int *)&frame->tf_r6 ||
        word == (int *)&frame->tf_r7 ||
        word == (int *)&frame->tf_r8 ||
        word == (int *)&frame->tf_r9 ||
        word == (int *)&frame->tf_r10 ||
        word == (int *)&frame->tf_r11) {
        *word = data;
        return 0;
    }
    if (word == (int *)&frame->tf_sp) {
        if (!ptrace_sp_valid((u_int)data))
            return 1;
        frame->tf_sp = (u_int)data;
        return 0;
    }
    if (word == (int *)&frame->tf_pc) {
        if (!ptrace_pc_valid((u_int)data))
            return 1;
        frame->tf_pc = (u_int)data;
        return 0;
    }
    if (word == (int *)&frame->tf_psr) {
        frame->tf_psr = (frame->tf_psr & ~PSR_NZCV) |
            ((u_int)data & PSR_NZCV);
        return 0;
    }
    return 1;
}

/*
 * sys-trace system call.
 */
void
ptrace(void)
{
    struct proc *process;
    struct ptrace_args *arguments = (struct ptrace_args *)u.u_arg;

    if (arguments->request == PT_TRACE_ME) {
        u.u_procp->p_flag |= P_TRACED;
        return;
    }
    if (arguments->request < PT_TRACE_ME) {
        u.u_error = EIO;
        return;
    }
    process = pfind(arguments->pid);
    if (process == 0 || process->p_stat != SSTOP ||
        process->p_ppid != u.u_procp->p_pid ||
        !(process->p_flag & P_TRACED)) {
        u.u_error = ESRCH;
        return;
    }
    while (ipc.ip_lock)
        sleep((caddr_t)&ipc, PZERO);
    ipc.ip_lock = process->p_pid;
    ipc.ip_data = arguments->data;
    ipc.ip_addr = arguments->address;
    ipc.ip_req = arguments->request;
    process->p_flag &= ~P_WAITED;
    setrun(process);
    while (ipc.ip_req > 0)
        sleep((caddr_t)&ipc, PZERO);
    u.u_rval = ipc.ip_data;
    if (ipc.ip_req < 0)
        u.u_error = EIO;
    ipc.ip_lock = 0;
    wakeup((caddr_t)&ipc);
}

/*
 * Code that the child process
 * executes to implement the command
 * of the parent process in tracing.
 */
int
procxmt(void)
{
    int request;
    int offset;
    int data;
    int *word;

    if (ipc.ip_lock != u.u_procp->p_pid)
        return(0);
    u.u_procp->p_slptime = 0;
    request = ipc.ip_req;
    ipc.ip_req = 0;
    wakeup ((caddr_t)&ipc);
    switch (request) {

    /* read user I */
    case PT_READ_I:

    /* read user D */
    case PT_READ_D:
        if (copyin((caddr_t)ipc.ip_addr, (caddr_t)&data,
            sizeof data) != 0)
            goto error;
        ipc.ip_data = data;
        break;

    /* read u */
    case PT_READ_U:
        offset = (int)(u_long)ipc.ip_addr;
        if (!ptrace_user_word_offset_valid(offset))
            goto error;
        word = (int *)((u_long)&u + (u_int)offset);
        bcopy((caddr_t)word, (caddr_t)&ipc.ip_data,
            sizeof ipc.ip_data);
        break;

    /* write user I */
    case PT_WRITE_I:
    /* write user D */
    case PT_WRITE_D:
        if (copyout((caddr_t)&ipc.ip_data, (caddr_t)ipc.ip_addr,
            sizeof ipc.ip_data) != 0)
            goto error;
        break;

    /* write u */
    case PT_WRITE_U:
        offset = (int)(u_long)ipc.ip_addr;
        if (!ptrace_user_word_offset_valid(offset))
            goto error;
        word = (int *)((u_long)&u + (u_int)offset);
        if (ptrace_write_frame_word(word, ipc.ip_data) != 0)
            goto error;
        break;

    /* set signal and continue */
    case PT_STEP:
        /* Cortex-M single-step needs a separate debug-monitor contract. */
        goto error;

    case PT_CONTINUE:
        if (ipc.ip_data < 0 || ipc.ip_data >= NSIG)
            goto error;
        if ((u_int)(u_long)ipc.ip_addr != 1U &&
            !ptrace_pc_valid((u_int)(u_long)ipc.ip_addr))
            goto error;
        if ((u_int)(u_long)ipc.ip_addr != 1U)
            u.u_frame->tf_pc = (u_int)(u_long)ipc.ip_addr;
        u.u_procp->p_ptracesig = ipc.ip_data;
        return(1);

    /* force exit */
    case PT_KILL:
        exit(u.u_procp->p_ptracesig);
        return(0); /* exit does not return. */

    default:
error:
        ipc.ip_req = -1;
    }
    return(0);
}
