/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include "SYS.h"

/*
 * error = sigaction(sig, vec, ovec)
 *	int		error, sig;
 *	struct sigaction *vec, *ovec;
 *
 * The fourth system-call argument supplies sigtramp, which returns from a
 * user signal handler through sigreturn.
 */
ENTRY(sigaction)
	ldr	r3, =sigtramp		/* arg4: address of sigtramp. */
	svc	SYS_sigaction
	bcs	1f			/* Error if carry set. */
	bx	lr			/* Success return. */
1:	ldr	r1, =errno
	str	r0, [r1]
	mov	r0, #0
	mvn	r0, r0
	bx	lr			/* Error return. */
END(sigaction)

/*
 * sigtramp - Signal "Trampoline Code"
 *
 * This code is transfered to by the kernel when a signal is delivered to a
 * process.  In general, the idea is that sigtramp saves the process' register
 * context and then vectors on to the real signal action routine.  Upon return
 * from the signal action routine sigtramp restores the process' register
 * context and performs a sigreturn.
 *
 * On the Cortex-M ports the kernel builds a struct sigframe (see
 * sys/arch/rp2040/rp2040/sig_machdep.c) below the interrupted stack
 * pointer: eight words the exception return consumes as the hardware
 * frame that enters the handler, then the sigcontext. The handler runs
 * with sp just above those eight words, with r0 = signal number,
 * r1 = code, r2 = pointer to the sigcontext, and lr = sigtramp. When the
 * handler returns here, sp is back at the handler entry value; the
 * sigreturn SVC pushes its own hardware frame into the same eight words
 * and the kernel finds the sigcontext at tf_sp + 32. sigtramp itself
 * keeps no state: r0 and r1 are already in the sigcontext, and C
 * handlers preserve every other register.
 */
ENTRY(sigtramp)
	svc	SYS_sigreturn		/* Attempt the sigreturn. */
	svc	SYS_exit		/* Die if the sigreturn fails ... */
END(sigtramp)
