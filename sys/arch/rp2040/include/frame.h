/*
 * Copyright (c) 2021, 2022 Christopher Hettrick <chris@structfoo.com>
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

#ifndef _MACHINE_FRAME_H_
#define _MACHINE_FRAME_H_

#define	INSN_SZ		2	/* Thumb are 2 bytes; some are 4 bytes. */

#define	PSR_C		(1UL << 29U)	/* Carry bit. */
#define	PSR_NZCV	(0xfUL << 28U)	/* User-visible condition flags. */

struct	trapframe {
/* The following 8 registers are pushed on stack by hardware in an SVCall. */
	u_int	tf_r0;		/* Argument / Scratch Register 1 */
	u_int	tf_r1;		/* Argument / Scratch Register 2 */
	u_int	tf_r2;		/* Argument / Scratch Register 3 */
	u_int	tf_r3;		/* Argument / Scratch Register 4 */
	u_int	tf_ip;		/* Stack Pointer (as passed by IP) */
	u_int	tf_lr;		/* Link Register */
	u_int	tf_pc;		/* Program Counter */
	u_int	tf_psr;		/* Program Status Register */
/* The following 8 registers are manually pushed on stack in SVC_Handler. */
	u_int	tf_r4;		/* Variable Register 1 */
	u_int	tf_r5;		/* Variable Register 2 */
	u_int	tf_r6;		/* Variable Register 3 */
	u_int	tf_r7;		/* Variable Register 4 */
	u_int	tf_r8;		/* Variable Register 5 */
	u_int	tf_r9;		/* Variable Register 6 */
	u_int	tf_r10;		/* Variable Register 7 */
	u_int	tf_r11;		/* Variable Register 8 or Frame Pointer */
};

#define	tf_sp	tf_ip		/* Stack Pointer (as passed by IP) */
#define	tf_r14	tf_lr		/* Link Register */
#define	tf_r15	tf_pc		/* Program Counter */

/*
 * The SIO hardware divider state of a process (RP2040 datasheet 2.3.1.5),
 * saved by divider_save() at an entry from the process and written back by
 * divider_restore() on the way out. ds_csr is DIV_CSR as the entry found
 * it; its DIRTY bit (DS_CSR_DIRTY) marks a sequence between a divider write
 * and its quotient read, the only state worth carrying, and the other words
 * are written, and meaningful, only then.
 */
struct	divider_state {
	u_int	ds_dividend;	/* DIV_UDIVIDEND */
	u_int	ds_divisor;	/* DIV_UDIVISOR */
	u_int	ds_remainder;	/* DIV_REMAINDER */
	u_int	ds_quotient;	/* DIV_QUOTIENT */
	u_int	ds_csr;		/* DIV_CSR at entry */
};

#define	DS_CSR_DIRTY	0x2U	/* DIV_CSR.DIRTY: a result is pending */

/*
 * The kernel-stack frame of an entry from a process, built by
 * PendSV_Handler (system calls and asynchronous system traps) and by
 * arm_fault: the trapframe that u.u_frame names and that the signal
 * machinery, ptrace and exec read, then the process state an entry at an
 * arbitrary instruction must carry back and a system call never needed.
 * tf_sp occupies the trapframe word the hardware frame gives r12, so r12
 * lives in uf_r12. sendsig and sigreturn (sig_machdep.c) carry both across
 * a handler. 88 bytes, a multiple of eight, so MSP stays aligned.
 */
struct	userframe {
	struct	trapframe uf_tf;	/* first: u.u_frame points here */
	u_int	uf_r12;			/* Intra-Procedure-Call Scratch Register */
	struct	divider_state uf_div;
};

#define	USERFRAME(tf)	((struct userframe *)(void *)(tf))

#ifdef KERNEL
void	divider_save(struct divider_state *);
void	divider_restore(const struct divider_state *);
#endif

/*
 * Exception stack frame of a SysTick exception.
 * A clockframe encapsulates the machine state when the SysTick
 * exception occured.
 * The clockframe is used by hardclock(), softclock(), and gatherstats().
 */
struct	clockframe {
/* These 8 registers are pushed on stack by hardware in an exception. */
	u_int	cf_r0;		/* Argument / Scratch Register 1 */
	u_int	cf_r1;		/* Argument / Scratch Register 2 */
	u_int	cf_r2;		/* Argument / Scratch Register 3 */
	u_int	cf_r3;		/* Argument / Scratch Register 4 */
	u_int	cf_ip;		/* Stack Pointer (as passed by IP) */
	u_int	cf_lr;		/* Link Register */
	u_int	cf_pc;		/* Program Counter */
	u_int	cf_psr;		/* Program Status Register */
};

#define	cf_sp	cf_ip		/* Stack Pointer (as passed by IP) */

/*
 * Exception stack frame of a fault exception.
 * A faultframe encapsulates the machine state when the fault occured.
 *
 * ARMv6-M fault types: HardFault
 * ARMv7-M fault types: HardFault, MemManage, BusFault, UsageFault
 */
struct	faultframe {
/* These 8 registers are pushed on stack by hardware in a fault exception. */
	u_int	ff_r0;		/* Argument / Scratch Register 1 */
	u_int	ff_r1;		/* Argument / Scratch Register 2 */
	u_int	ff_r2;		/* Argument / Scratch Register 3 */
	u_int	ff_r3;		/* Argument / Scratch Register 4 */
	u_int	ff_ip;		/* Stack Pointer (as passed by IP) */
	u_int	ff_lr;		/* Link Register */
	u_int	ff_pc;		/* Program Counter */
	u_int	ff_psr;		/* Program Status Register */
};

#define	ff_sp	ff_ip		/* Stack Pointer (as passed by IP) */

#endif /* !_MACHINE_FRAME_H_ */
