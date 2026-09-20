/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/proc_asm.h>

/*
 * The context switch in each port's locore stores the u-area address at
 * P_ADDR_OFFSET, so the constant follows the struct or the kernel fails here.
 * The constant is the ILP32 layout every port shares; a host gate that
 * compiles this file at LP64 lays the struct out differently and is not the
 * layout the assembler stores through.
 */
#if __SIZEOF_POINTER__ == 4
_Static_assert(__builtin_offsetof(struct proc, p_un.p_alive.P_addr) == P_ADDR_OFFSET,
    "P_ADDR_OFFSET in sys/proc_asm.h must equal offsetof(struct proc, p_addr)");
#endif

struct proc *pidhash[PIDHSZ];
struct proc *freeproc, *zombproc, *allproc, *qs;

/*
 * Is p an inferior of the current process?
 */
int
inferior(p)
    register struct proc *p;
{
    for (; p != u.u_procp; p = p->p_pptr)
        if (p->p_ppid == 0)
            return (0);
    return (1);
}

/*
 * Find a process by pid.
 */
struct proc *
pfind (pid)
    register int pid;
{
    register struct proc *p = pidhash [PIDHASH(pid)];

    for (; p; p = p->p_hash)
        if (p->p_pid == pid)
            return (p);
    return ((struct proc *)0);
}

/*
 * init the process queues
 */
void
pqinit()
{
    register struct proc *p;

    /*
     * most procs are initially on freequeue
     *  nb: we place them there in their "natural" order.
     */

    freeproc = NULL;
    for (p = proc+NPROC; --p > proc; freeproc = p)
        p->p_nxt = freeproc;

    /*
     * but proc[0] is special ...
     */

    allproc = p;
    p->p_nxt = NULL;
    p->p_prev = &allproc;

    zombproc = NULL;
}
