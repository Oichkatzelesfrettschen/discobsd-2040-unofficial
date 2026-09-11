/*
 * Copyright (c) 2026 DiscoBSD
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

#ifndef	_MACHINE_SCB_H_
#define	_MACHINE_SCB_H_

/*
 * Cortex-M0+ System Control Block and SysTick, as the RP2040 maps them.
 *
 * The STM32 tree reaches these through CMSIS, which ARM now ships under
 * Apache-2.0. This header names the few registers the kernel touches so the
 * tree stays self-contained and BSD; see include/intr.h for the same
 * reasoning applied to the NVIC.
 */

#define	SCB_BASE		0xe000e000UL

#define	SCB_SYST_CSR		(SCB_BASE + 0x010)	/* SysTick control. */
#define	SCB_SYST_RVR		(SCB_BASE + 0x014)	/* Reload value. */
#define	SCB_SYST_CVR		(SCB_BASE + 0x018)	/* Current value. */
#define	SCB_SYST_CALIB		(SCB_BASE + 0x01c)

#define	SCB_ICSR		(SCB_BASE + 0xd04)	/* Int control state. */
#define	SCB_VTOR		(SCB_BASE + 0xd08)	/* Vector table base. */
#define	SCB_AIRCR		(SCB_BASE + 0xd0c)
#define	SCB_SHPR2		(SCB_BASE + 0xd1c)	/* SVCall priority. */
#define	SCB_SHPR3		(SCB_BASE + 0xd20)	/* PendSV, SysTick. */

#define	SCB_ICSR_VECTACTIVE	0x0000003fUL
#define	SCB_ICSR_PENDSTCLR	0x02000000UL
#define	SCB_ICSR_PENDSTSET	0x04000000UL
#define	SCB_ICSR_PENDSVCLR	0x08000000UL
#define	SCB_ICSR_PENDSVSET	0x10000000UL
#define	SCB_ICSR_NMIPENDSET	0x80000000UL

#define	SYST_CSR_ENABLE		0x00000001UL
#define	SYST_CSR_TICKINT	0x00000002UL
#define	SYST_CSR_CLKSOURCE	0x00000004UL	/* Processor clock. */
#define	SYST_CSR_COUNTFLAG	0x00010000UL

#define	SYST_RVR_MAX		0x00ffffffUL	/* 24-bit counter. */

/*
 * Bit 9 of the xPSR stacked on exception entry records whether the hardware
 * inserted a padding word to reach eight-byte alignment. The STM32 tree tests
 * it through SCB_CCR_STKALIGN_Msk, which happens to be the same bit number in
 * a different register; naming it here says what is actually being read. On
 * ARMv6-M the alignment is architectural and the CCR bit is read-only one.
 */
#define	XPSR_STKALIGN		0x00000200UL

/*
 * The IPSR field of xPSR holds the active exception number, or zero in thread
 * mode. ARMv7-M gives it nine bits; ARMv6-M gives it six, which is all the
 * RP2040's 48-entry vector table needs. The STM32 tree reaches this through
 * the CMSIS name IPSR_ISR_Msk.
 */
#define	IPSR_ISR_MASK		0x0000003fUL

#define	SCB_REG32(a)		(*(volatile u_int *)(a))

/*
 * ARMv6-M has four 32-bit instructions beyond branches, and two of them are
 * these barriers. They are spelled out rather than taken from CMSIS.
 */
static __inline void
arm_dsb(void)
{
	__asm__ volatile ("dsb 0xf" ::: "memory");
}

static __inline void
arm_isb(void)
{
	__asm__ volatile ("isb 0xf" ::: "memory");
}

/*
 * System exception priorities live in SHPR2 and SHPR3 rather than the NVIC
 * priority array, so they are set here rather than through
 * arm_intr_set_priority, which addresses external interrupts only. ARMv6-M
 * allows only word access to these registers, and only SVCall, PendSV, and
 * SysTick are configurable; the rest are fixed negative priorities.
 */
#define	EXC_SVCALL	11
#define	EXC_PENDSV	14
#define	EXC_SYSTICK	15

static __inline void
arm_set_exception_priority(int exc, u_int prio_byte)
{
	volatile u_int *shpr;
	u_int shift;

	if (exc == EXC_SVCALL) {
		shpr = &SCB_REG32(SCB_SHPR2);
		shift = 24;			/* SHPR2 byte 3. */
	} else if (exc == EXC_PENDSV) {
		shpr = &SCB_REG32(SCB_SHPR3);
		shift = 16;			/* SHPR3 byte 2. */
	} else if (exc == EXC_SYSTICK) {
		shpr = &SCB_REG32(SCB_SHPR3);
		shift = 24;			/* SHPR3 byte 3. */
	} else {
		return;
	}
	*shpr = (*shpr & ~((u_int)0xff << shift)) |
	    ((prio_byte & 0xff) << shift);
}

#endif	/* !_MACHINE_SCB_H_ */
