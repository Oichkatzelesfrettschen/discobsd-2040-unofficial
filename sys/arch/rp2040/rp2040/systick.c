/*
 * Copyright (c) 2022 Christopher Hettrick <chris@structfoo.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/tty.h>

#include <machine/frame.h>

#include <machine/scb.h>

#include <rp2040/rp2040/clock_catchup.h>

/*
 * SysTick_Handler()
 *
 * Exception entry for the system time base. The body is the entry sequence
 * itself, so the function carries no AAPCS boundary: the compiler emits
 * neither prologue nor epilogue, and the sequence selects the stack that
 * holds the exception frame from bit 2 (SPSEL) of EXC_RETURN in lr (ARMv6-M
 * ARM B1.5.8), places that pointer in r0 as the struct clockframe argument
 * of machine/frame.h, passes EXC_RETURN itself in r1, and tail-branches
 * into systick(). Both branches leave through BX, so control never falls
 * off the end and no return instruction follows.
 */
__attribute__((naked)) void
SysTick_Handler(void)
{
__asm volatile (
"	.syntax	unified		\n\t"
"	.thumb			\n\t"

#ifdef __thumb2__
"	mov	r1, lr		\n\t"	/* EXC_RETURN is the second argument. */
"	tst	lr, #0x4	\n\t"	/* Test bit 2 (SPSEL) of EXC_RETURN. */
"	ite	eq		\n\t"	/* Came from user or kernel mode? */
"	mrseq	r0, MSP		\n\t"	/* Kernel mode; stack frame on MSP. */
"	mrsne	r0, PSP		\n\t"	/* User mode; stack frame on PSP. */
"	b	systick		\n\t"	/* Call systick(frame, lr); */
#else /* __thumb__ */
"	movs	r0, #0x4	\n\t"	/* Test bit 2 (SPSEL).. */
"	mov	r1, lr		\n\t"	/*   of EXC_RETURN in lr, which is */
"	tst	r0, r1		\n\t"	/*   also the second argument. */
"	beq	1f		\n\t"
"	mrs	r0, PSP		\n\t"	/* User mode; stack frame on PSP. */
"	ldr	r2, =systick	\n\t"	/* Call systick(frame, lr); */
"	bx	r2		\n\t"
"1:	mrs	r0, MSP		\n\t"	/* Kernel mode; stack frame on MSP. */
"	ldr	r2, =systick	\n\t"	/* Call systick(frame, lr); */
"	bx	r2		\n\t"
#endif
);
}

/*
 * EXC_RETURN of an exception taken from thread mode on the process stack:
 * the only return that resumes a process (ARMv6-M ARM B1.5.8). The stacked
 * xPSR cannot tell a process from the kernel, whose thread-mode code before
 * the first process switch runs with IPSR zero as well.
 */
#define	EXC_RETURN_THREAD_PSP	0xfffffffdU

/*
 * Default system time base for Cortex-M.
 * Internal hardware SysTick interrupt every 1 millisecond.
 */
void
systick(struct clockframe *frame, u_int exc_return)
{
	struct proc *p;

	/*
	 * The STM32 tree advanced its HAL millisecond counter here.
	 * Nothing on this target keeps one. The tick time feeds
	 * clock_catchup(), which restores the ticks a masked flash
	 * operation drops.
	 */
	clock_tick_stamp();

	hardclock((caddr_t)frame->cf_pc, frame->cf_psr);

	/*
	 * The asynchronous system trap. A tick that interrupted a process
	 * which has a signal waiting, or must yield (runrun, which schedcpu
	 * sets once a second and setrun and wakeup set for a better
	 * process), pends PendSV. PendSV has the lowest priority, so it runs
	 * once this handler and any other active exception have returned,
	 * and PendSV_Handler, finding no SVC behind it, runs userret from
	 * ast() in syscall.c. A tick that interrupted the kernel needs no
	 * trap: every return from the kernel to a process passes userret.
	 * The terminal's interrupt character and every other psignal() from
	 * an interrupt handler wait at most one tick here. A masked signal
	 * needs no trap even for a traced process: CURSIG() passes a traced
	 * process to issignal(), but issignal() drops p_sigmask's signals
	 * before it stops for the tracer, so the trap would deliver nothing.
	 */
	if (exc_return != EXC_RETURN_THREAD_PSP)
		return;
	p = u.u_procp;
	if (runrun || (p->p_sig & ~p->p_sigmask))
		SCB_REG32(SCB_ICSR) = SCB_ICSR_PENDSVSET;
}
