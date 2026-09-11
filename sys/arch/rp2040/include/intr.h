/*
 * Copyright (c) 2020-2026 DiscoBSD
 * Copyright (c) 2014, RetroBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions of the DiscoBSD
 * LICENSE file are met.
 */

/*
 * Interrupt priority levels for the RP2040.
 *
 * Derived from sys/arch/stm32/include/intr.h. Two differences follow from
 * ARMv6-M rather than from the board.
 *
 * ARMv6-M defines no BASEPRI register, so it cannot mask by priority level.
 * PRIMASK is all there is, and it is all or nothing. Every blocking spl
 * therefore collapses to a global disable, which is what the Cortex-M0 branch
 * of the STM32 header already does. The IPL constants survive because the
 * machine-independent kernel names them, and because interrupt priorities
 * still order preemption among enabled interrupts.
 *
 * The register access is written as inline assembly and direct NVIC stores
 * rather than through CMSIS. ARM relicensed CMSIS from a 3-clause BSD license
 * to Apache 2.0, and this tree keeps its sources self-contained and BSD, so
 * the four intrinsics this header needs are spelled out here instead.
 */

#ifndef _MACHINE_INTR_H_
#define _MACHINE_INTR_H_

#ifdef KERNEL

#include <sys/types.h>

/* RP2040 Cortex-M0+ implements 2 NVIC priority bits, in the high bits. */
#define	NVIC_PRIO_BITS		2U

#define	IPL_NONE	0	/* Blocks nothing. */
#define	IPL_SOFTCLOCK	1	/* Blocks low-priority clock processing. */
#define	IPL_NET		2	/* Blocks network protocol processing. */
#define	IPL_BIO		3	/* Blocks disk controllers. */
#define	IPL_TTY		4	/* Blocks terminal multiplexers. */
#define	IPL_CLOCK	5	/* Blocks high-priority clock processing. */
#define	IPL_HIGH	6	/* Blocks all interrupt activity. */

#define	IPL_TOP		(IPL_HIGH + 1)
#define	IPL_BITS	(8U - NVIC_PRIO_BITS)	/* MSB prio shift bits. */

/* Cortex-M core exception/interrupt priority levels. */
#define	IPL_PENDSV	IPL_NONE	/* PendSV exception at lowest prio. */
#define	IPL_SVCALL	IPL_TOP		/* SVC exception at highest prio. */
#define	IPL_SYSTICK	IPL_CLOCK	/* SysTick exception at clock prio. */

#define	IPLTOREG(ipl) \
	(u_char)((ipl) ? (((IPL_TOP - (ipl)) << IPL_BITS) & 0xFFUL) : 0)
#define	REGTOIPL(reg) \
	(u_int)((reg) ? (IPL_TOP - ((reg) >> IPL_BITS)) : 0)

/*
 * RP2040 NVIC, from the RP2040 datasheet Cortex-M0+ register listing.
 * ARMv6-M gives one 32-bit set/clear register each, covering the chip's 32
 * interrupts exactly, and byte-addressable priority registers four to a word.
 */
#define	RP2040_PPB_BASE		0xe0000000UL
#define	RP2040_NVIC_ISER	(RP2040_PPB_BASE + 0xe100UL)
#define	RP2040_NVIC_ICER	(RP2040_PPB_BASE + 0xe180UL)
#define	RP2040_NVIC_IPR0	(RP2040_PPB_BASE + 0xe400UL)

#define	RP2040_REG32(a)		(*(volatile u_int *)(a))

static inline int
arm_get_primask(void)
{
	int s;

	__asm__ volatile ("mrs %0, primask" : "=r" (s) :: "memory");
	return s;
}

static inline void
arm_set_primask(int s)
{
	__asm__ volatile ("msr primask, %0" :: "r" (s) : "memory");
}

static inline int
arm_intr_disable(void)
{
	int s = arm_get_primask();

	__asm__ volatile ("cpsid i" ::: "memory");
	__asm__ volatile ("isb 0xf" ::: "memory");
	return s;
}

static inline int
arm_intr_enable(void)
{
	int s = arm_get_primask();

	__asm__ volatile ("cpsie i" ::: "memory");
	__asm__ volatile ("isb 0xf" ::: "memory");
	return s;
}

static inline void
arm_intr_restore(int s)
{
	arm_set_primask(s);
	__asm__ volatile ("isb 0xf" ::: "memory");
}

static inline void
arm_intr_disable_irq(int irq)
{
	RP2040_REG32(RP2040_NVIC_ICER) = (u_int)1 << ((u_int)irq & 0x1f);
	__asm__ volatile ("dsb 0xf" ::: "memory");
	__asm__ volatile ("isb 0xf" ::: "memory");
}

static inline void
arm_intr_enable_irq(int irq)
{
	RP2040_REG32(RP2040_NVIC_ISER) = (u_int)1 << ((u_int)irq & 0x1f);
}

/*
 * ARMv6-M has no byte-wide priority store, so a priority write is a
 * read-modify-write of the containing word. Priority is inverted on Cortex-M,
 * where zero outranks one, hence IPL_TOP - prio.
 */
static inline void
arm_intr_set_priority(int irq, int prio)
{
	volatile u_int *ipr;
	u_int shift, word;

	ipr = &RP2040_REG32(RP2040_NVIC_IPR0) + ((u_int)irq >> 2);
	shift = (((u_int)irq & 0x3) * 8) + IPL_BITS;
	word = *ipr & ~((u_int)0xff << (((u_int)irq & 0x3) * 8));
	*ipr = word | ((u_int)(IPL_TOP - prio) << shift);
}

/*
 * PRIMASK blocks everything or nothing, so every level that must block
 * maps onto a global disable.
 */
#define	splhigh()	arm_intr_disable()
#define	splclock()	arm_intr_disable()
#define	spltty()	arm_intr_disable()
#define	splnet()	arm_intr_disable()
#define	splbio()	arm_intr_disable()

#define	splsoftclock()	arm_intr_enable()

#define	splx(s)		arm_intr_restore(s)

#define	spl0()		arm_intr_enable()

#endif	/* KERNEL */

#endif	/* !_MACHINE_INTR_H_ */
