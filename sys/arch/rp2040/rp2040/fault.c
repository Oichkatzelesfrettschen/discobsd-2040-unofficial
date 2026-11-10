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
void usbabandon(void);
void lcdabandon(void);

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
 * sequence owns r0-r3 and reads r4-r11 as the fault left them. It banks
 * r4-r11 into fault_regs, selects the stack holding the exception frame from
 * bit 2 (SPSEL) of EXC_RETURN in lr (ARMv6-M ARM B1.5.8), keeps EXC_RETURN
 * on MSP, and calls arm_fault() with that frame pointer in r0 and EXC_RETURN
 * in r1. On return it loads r4-r11 from fault_regs, which fault_frame_out
 * refreshed from the trapframe, so a debugger's change to a process stopped
 * in the fault path takes effect, and leaves through the saved EXC_RETURN.
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
"	b	2f		\n\t"
"1:	mrs	r0, MSP		\n\t"	/* Kernel mode; fault frame on MSP. */
"2:	push	{r1, r2}	\n\t"	/* Keep EXC_RETURN; r2 pads to 8 bytes. */
"	bl	arm_fault	\n\t"	/* arm_fault(frame, lr) */
"	ldr	r0, =fault_regs	\n\t"	/* r4-r11 as the trapframe holds them. */
"	ldmia	r0!, {r4-r7}	\n\t"
"	ldmia	r0!, {r1-r2}	\n\t"
"	mov	r8, r1		\n\t"
"	mov	r9, r2		\n\t"
"	ldmia	r0!, {r1-r2}	\n\t"
"	mov	r10, r1		\n\t"
"	mov	r11, r2		\n\t"
"	pop	{r1, r2}	\n\t"
"	bx	r1		\n\t"	/* Exception return through EXC_RETURN. */
/* Thumb-1 literal loads must reach the pool even when C code grows. */
"	.ltorg			\n\t"
);
}

/*
 * The signal machinery (psignal, userret, sendsig in sig_machdep.c) works
 * on the struct trapframe that u.u_frame names, the layout PendSV_Handler
 * builds for a system call: the eight words of the hardware frame, with
 * tf_sp in the ip slot holding the PSP address of that frame, then r4-r11.
 * A fault enters through a different frame, so arm_fault builds the same
 * trapframe from the stacked faultframe and fault_regs before it signals,
 * and points u.u_frame at it; otherwise sendsig would rewrite the trapframe
 * of the process's last system call, the exception return would replay
 * the faulting instruction, and a caught fault would re-enter here forever
 * at HardFault priority, which no interrupt preempts.
 */
static void
fault_trapframe(struct trapframe *tf, const struct faultframe *frame)
{
	tf->tf_r0 = frame->ff_r0;
	tf->tf_r1 = frame->ff_r1;
	tf->tf_r2 = frame->ff_r2;
	tf->tf_r3 = frame->ff_r3;
	tf->tf_sp = (u_int)frame;
	tf->tf_lr = frame->ff_lr;
	tf->tf_pc = frame->ff_pc;
	tf->tf_psr = frame->ff_psr;
	tf->tf_r4 = fault_regs[0];
	tf->tf_r5 = fault_regs[1];
	tf->tf_r6 = fault_regs[2];
	tf->tf_r7 = fault_regs[3];
	tf->tf_r8 = fault_regs[4];
	tf->tf_r9 = fault_regs[5];
	tf->tf_r10 = fault_regs[6];
	tf->tf_r11 = fault_regs[7];
}

/*
 * Write the trapframe back as the hardware frame the exception return will
 * pop, at tf_sp, and move PSP there. With no handler entered tf_sp is the
 * original frame and r12 keeps its stacked value; sendsig moves tf_sp to
 * the handler's frame, where r12 is scratch. r4-r11 go to fault_regs, which
 * HardFault_Handler loads before the exception return. The copy happens
 * here, after userret and so after any process switch, because a process
 * that slept in the fault path (stopped under ptrace, say) may have seen
 * another process's fault overwrite fault_regs meanwhile.
 */
static void
fault_frame_out(const struct trapframe *tf, struct faultframe *frame)
{
	struct faultframe *out = (struct faultframe *)tf->tf_sp;

	out->ff_ip = out == frame ? frame->ff_ip : 0;
	out->ff_r0 = tf->tf_r0;
	out->ff_r1 = tf->tf_r1;
	out->ff_r2 = tf->tf_r2;
	out->ff_r3 = tf->tf_r3;
	out->ff_lr = tf->tf_lr;
	out->ff_pc = tf->tf_pc;
	out->ff_psr = tf->tf_psr;
	fault_regs[0] = tf->tf_r4;
	fault_regs[1] = tf->tf_r5;
	fault_regs[2] = tf->tf_r6;
	fault_regs[3] = tf->tf_r7;
	fault_regs[4] = tf->tf_r8;
	fault_regs[5] = tf->tf_r9;
	fault_regs[6] = tf->tf_r10;
	fault_regs[7] = tf->tf_r11;
	__asm volatile ("msr psp, %0" : : "r" (out) : "memory");
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
	struct trapframe tf;
	int psig = SIGSEGV;
	time_t syst;
	u_int icsr;

	led_control(LED_KERNEL, 1);

#ifdef UARTUSB_ENABLED
	/*
	 * Only 0xfffffffd returns to a process; any other EXC_RETURN is the
	 * kernel faulting, which ends in the panic below and never resumes
	 * what it interrupted. Release the console driver's re-entry guard
	 * before printing, because the interrupted call will not release it
	 * and the report would otherwise reach nobody.
	 */
	if (fault_lr != 0xfffffffdUL)
		usbabandon();
#endif
#ifdef PICOCALC_ENABLED
	/* The panel console's drawer is abandoned the same way. */
	if (fault_lr != 0xfffffffdUL)
		lcdabandon();
#endif

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

	fault_trapframe(&tf, frame);
	u.u_frame = &tf;
	psignal(u.u_procp, psig);
	userret(tf.tf_pc, syst);
	fault_frame_out(&tf, frame);

	led_control(LED_KERNEL, 0);
}
