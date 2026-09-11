/*
 * Copyright (c) 2022, 2023 Christopher Hettrick <chris@structfoo.com>
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

/*
 * Fault handling on ARMv6-M.
 *
 * The STM32 edition of this file carries four handlers and decodes HFSR,
 * CFSR, MMFAR, and BFAR to name the cause and the faulting address. ARMv6-M
 * raises one fault exception and defines none of those registers, so the
 * decode has nothing to read and is gone rather than translated. What remains
 * is the exception stack frame and the EXC_RETURN value.
 *
 * The signal is SIGSEGV rather than the STM32 default of SIGILL. With no
 * cause code the choice is a guess either way, and on a target with neither
 * an MMU nor an MPU the faults that reach here are overwhelmingly bad
 * addresses rather than bad instructions.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>

#include <machine/fault.h>
#include <machine/frame.h>
#include <machine/intr.h>
#include <machine/scb.h>

/*
 * HardFault_Handler()
 *
 * The only fault exception ARMv6-M raises. Escalation folds every other
 * fault condition into it.
 *
 * The STM32 version selects the stack pointer with a TST against an immediate
 * inside an IT block. ARMv6-M has neither: TST takes two registers, and IT
 * does not exist. This is the branch-based form already used by
 * SysTick_Handler in systick.c.
 */
void
HardFault_Handler(void)
{
__asm volatile (
"	.syntax	unified		\n\t"
"	.thumb			\n\t"

"	mov	r1, lr		\n\t"	/* Value of lr when fault occurred. */
"	movs	r0, #0x4	\n\t"	/* Test bit 2 (SPSEL).. */
"	tst	r0, r1		\n\t"	/*   of EXC_RETURN in lr. */
"	beq	1f		\n\t"	/* Came from user or kernel mode? */
"	mrs	r0, PSP		\n\t"	/* User mode; fault frame on PSP. */
"	ldr	r2, =arm_fault	\n\t"	/* Call arm_fault(frame, lr); */
"	bx	r2		\n\t"
"1:	mrs	r0, MSP		\n\t"	/* Kernel mode; fault frame on MSP. */
"	ldr	r2, =arm_fault	\n\t"	/* Call arm_fault(frame, lr); */
"	bx	r2		\n\t"
);
}

/*
 * arm_fault(frame, fault_lr)
 *
 * Report what the hardware preserved, signal the process, and return through
 * userret. There is no fault cause to report and no faulting address: see the
 * comment at the head of this file.
 */
void
arm_fault(struct faultframe *frame, u_int fault_lr)
{
	int psig = SIGSEGV;
	time_t syst;
	u_int icsr;

	led_control(LED_KERNEL, 1);
	syst = u.u_ru.ru_stime;
#ifdef UCB_METER
	cnt.v_trap++;
#endif

	icsr = SCB_REG32(SCB_ICSR);

	printf("fault: HardFault, exception %d\n",
	    (int)(icsr & SCB_ICSR_VECTACTIVE));
	printf("fault trap frame:\n");
	printf(" r0:\t0x%08x\tip:\t0x%08x\n", frame->ff_r0, frame->ff_ip);
	printf(" r1:\t0x%08x\tlr:\t0x%08x\n", frame->ff_r1, frame->ff_lr);
	printf(" r2:\t0x%08x\tpc:\t0x%08x\n", frame->ff_r2, frame->ff_pc);
	printf(" r3:\t0x%08x\tpsr:\t0x%08x\n", frame->ff_r3, frame->ff_psr);
	printf("fault entry EXC_RETURN value:\n");
	printf(" lr:\t0x%08x\n", fault_lr);

	/*
	 * Only 0xfffffffd returns to a process on its own stack. Any other
	 * value is the kernel faulting, in a handler or on the main stack,
	 * and returning to the faulting address would fault again forever.
	 */
	if (fault_lr != 0xfffffffdUL)
		panic("kernel fault");

	arm_intr_enable();

	psignal(u.u_procp, psig);
	userret(frame->ff_pc, syst);

	led_control(LED_KERNEL, 0);
}
