/*
 * The struct proc offset the context switch reaches without the C type.
 *
 * Each port's locore stores the u-area address into u.u_procp->p_addr while
 * it exchanges u. and u0., and an assembler source cannot include sys/proc.h
 * for the field, so the offset is a constant. A field added or widened
 * ahead of p_addr moves it, and a stale constant makes every switch write the
 * u-area address over p_link, the run-queue link, until wakeup() finds a
 * process on a sleep queue that is not asleep and panics.
 *
 * kern_proc.c holds P_ADDR_OFFSET to __builtin_offsetof(struct proc, p_addr)
 * with a _Static_assert, so a layout change fails the kernel build at the
 * constant rather than at the first context switch.
 */
#ifndef _SYS_PROC_ASM_H_
#define _SYS_PROC_ASM_H_

#define P_ADDR_OFFSET   60

#endif /* !_SYS_PROC_ASM_H_ */
