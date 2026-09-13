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
 * We pass one additional parameter to the sigaction sys call: the address of
 * the "Trampoline Code", sigtramp - the code that handles saving and restoring
 * register context and so on for signals.  On the VAX-11 under BSD4.3 it isn't
 * necessary to pass this address since the trampoline code is stored in the
 * user structure in u.u_pcb.pcb_sigc at a known address in user space.  It
 * really doesn't introduce much extra overhead, so our method for doing it on
 * a PDP-11 is alright too.
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
 * In the case of the PDP-11, the kernel will have already saved r0 and r1 for
 * sigtramp in a sigcontext structure it passes to us.  Sigtramp vectors onto
 * the signal action routine whose address has been left in r0 by the kernel
 * (sigtramp assumes the signal action routine will save any other registers
 * it uses (as all C routines will)).  Upon return from the signal action
 * routine, sigtramp will execute a sigreturn with the sigcontext structure
 * given to us by the kernel.
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
