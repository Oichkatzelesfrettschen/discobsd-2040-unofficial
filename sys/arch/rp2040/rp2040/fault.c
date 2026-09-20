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
 * cause code the choice is a guess either way; the MPU map mpu.c programs
 * turns every user access outside the boot ROM, the process window and the
 * SIO divider into a HardFault (datasheet 2.4.6.1), so the faults that
 * reach here are overwhelmingly bad addresses rather than bad
 * instructions.
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

void usbdrain(void);
void usbpoll(void);

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
 *
 * The body is the entry sequence itself, so the function carries no AAPCS
 * boundary: the compiler emits neither prologue nor epilogue, and the
 * sequence owns r0-r2 and reads r4-r11 as the fault left them. It banks
 * r4-r11 into fault_regs, selects the stack holding the exception frame from
 * bit 2 (SPSEL) of EXC_RETURN in lr (ARMv6-M ARM B1.5.8), and tail-branches
 * into arm_fault() with that frame pointer in r0 and EXC_RETURN in r1. Both
 * branches leave through BX, so control never falls off the end and no
 * return instruction follows.
 */
u_int fault_regs[8];			/* r4-r11 at the fault, for the report. */

__attribute__((naked)) void
HardFault_Handler(void)
{
__asm volatile (
"	.syntax	unified		\n\t"
"	.thumb			\n\t"

"	ldr	r2, =fault_regs	\n\t"	/* Keep the callee-saved registers, */
"	stmia	r2!, {r4-r7}	\n\t"	/*   which no frame records, and */
"	mov	r0, r8		\n\t"	/*   leave them intact: a user fault */
"	mov	r1, r9		\n\t"	/*   returns to the process. */
"	stmia	r2!, {r0-r1}	\n\t"
"	mov	r0, r10		\n\t"
"	mov	r1, r11		\n\t"
"	stmia	r2!, {r0-r1}	\n\t"

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
static int infault;

void
arm_fault(struct faultframe *frame, u_int fault_lr)
{
	int psig = SIGSEGV;
	time_t syst;
	u_int icsr;

	led_control(LED_KERNEL, 1);

	/*
	 * A fault while reporting a fault would escalate to lockup, which
	 * takes the USB console with it. Stay here instead, servicing the
	 * controller, so the host can read what was printed and reach the
	 * reset interface.
	 */
	if (infault++) {
		printf("fault: nested, pc 0x%08x\n", frame->ff_pc);
		for (;;)
#ifdef UARTUSB_ENABLED
			usbpoll();
#else
			continue;
#endif
	}
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
	printf(" r4:\t0x%08x\tr8:\t0x%08x\n", fault_regs[0], fault_regs[4]);
	printf(" r5:\t0x%08x\tr9:\t0x%08x\n", fault_regs[1], fault_regs[5]);
	printf(" r6:\t0x%08x\tr10:\t0x%08x\n", fault_regs[2], fault_regs[6]);
	printf(" r7:\t0x%08x\tr11:\t0x%08x\n", fault_regs[3], fault_regs[7]);
	printf("fault entry EXC_RETURN value:\n");
	printf(" lr:\t0x%08x\n", fault_lr);
	printf("process %d %s\n", u.u_procp ? u.u_procp->p_pid : -1, u.u_comm);
	if ((u_int)u.u_frame > (u_int)&u &&
	    (u_int)u.u_frame < (u_int)&u + USIZE) {
		printf("syscall %d, user frame r4-r7 %08x %08x %08x %08x\n",
		    (u.u_frame->tf_pc > (u_int)__user_data_start + 2 &&
		    u.u_frame->tf_pc < (u_int)USER_TOP(u.u_procp)) ?
		    (*(u_short *)(u.u_frame->tf_pc - 2) & 0xff) : -1,
		    u.u_frame->tf_r4, u.u_frame->tf_r5, u.u_frame->tf_r6,
		    u.u_frame->tf_r7);
		printf("user frame r8-r11 %08x %08x %08x %08x, sp %08x pc %08x\n",
		    u.u_frame->tf_r8, u.u_frame->tf_r9, u.u_frame->tf_r10,
		    u.u_frame->tf_r11, u.u_frame->tf_sp, u.u_frame->tf_pc);
	}

	/*
	 * Only 0xfffffffd returns to a process on its own stack. Any other
	 * value is the kernel faulting, in a handler or on the main stack,
	 * and returning to the faulting address would fault again forever.
	 */
	if (fault_lr != 0xfffffffdUL)
		panic("kernel fault");

	infault = 0;
	arm_intr_enable();

	/*
	 * A fault is synchronous: returning to the instruction repeats it,
	 * so the signal must be delivered now or the process must die. A
	 * process that has blocked or ignored SIGSEGV loses that, and one
	 * whose stack is outside its memory, which no signal frame could be
	 * built on, is killed outright. Without this a wild stack pointer
	 * faulted forever and flooded the console.
	 */
	if ((u_int)frame < (u_int)__user_data_start ||
	    (u_int)frame >= (u_int)USER_TOP(u.u_procp))
		psig = SIGKILL;
	u.u_procp->p_sigmask &= ~sigmask(psig);
	u.u_procp->p_sigignore &= ~sigmask(psig);
	if (u.u_signal[psig] == SIG_IGN)
		u.u_signal[psig] = SIG_DFL;

	psignal(u.u_procp, psig);
	userret(frame->ff_pc, syst);

	led_control(LED_KERNEL, 0);
}
