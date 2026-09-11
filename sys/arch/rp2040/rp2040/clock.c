/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)clock.c	1.1 (2.10BSD Berkeley) 12/1/86
 */

#include <sys/param.h>

#include <machine/scb.h>

/*
 * Setup core timer for `hz' timer interrupts per second.
 *
 * SysTick counts the processor clock, CPU_KHZ cycles per millisecond, so
 * the reload for HZ interrupts a second fits the 24-bit counter up to
 * 16.7 MHz per tick. The STM32 tree arms it through the ST HAL's
 * SysTick_Config; here the three registers are written directly. Writing
 * the current value clears it and the COUNTFLAG, so the first period is a
 * full one.
 */
void
clkstart(void)
{
	SCB_REG32(SCB_SYST_RVR) = ((u_int)CPU_KHZ * 1000 / HZ) - 1;
	SCB_REG32(SCB_SYST_CVR) = 0;
	SCB_REG32(SCB_SYST_CSR) =
	    SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}
