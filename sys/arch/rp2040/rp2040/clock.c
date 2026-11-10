/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)clock.c	1.1 (2.10BSD Berkeley) 12/1/86
 */

#include <sys/param.h>
#include <sys/kernel.h>

#include <machine/scb.h>

#include <rp2040/rp2040/clock_catchup.h>
#ifdef CORE1
#include <rp2040/rp2040/core1.h>
#endif

/*
 * The free-running microsecond timer, datasheet 4.6. machdep.c leaves it out
 * of reset and never stops it, so TIMERAWL reads with no side effect.
 */
#define	TIMER_BASE		0x40054000UL
#define	TIMER_TIMERAWL		0x28

#define	CLOCK_TICK_US		(1000000U / HZ)

_Static_assert(1000000U % HZ == 0, "HZ must divide one second in microseconds");

static struct clock_catchup clkcatch;

static unsigned int
timer_us(void)
{
	return *(volatile u_int *)(TIMER_BASE + TIMER_TIMERAWL);
}

static int
systick_pending(void)
{
	return (SCB_REG32(SCB_ICSR) & SCB_ICSR_PENDSTSET) != 0;
}

/*
 * Setup core timer for `hz' timer interrupts per second.
 *
 * SysTick counts the processor clock, CPU_KHZ cycles per millisecond, so
 * the reload for HZ interrupts a second fits the 24-bit counter up to
 * 16.7 MHz per tick. The STM32 tree arms it through the ST HAL's
 * SysTick_Config; here the three registers are written directly. Writing
 * the current value clears it and the COUNTFLAG, so the first period is a
 * full one. The catch-up record starts at the same instant, so the first
 * tick falls one period after it.
 */
void
clkstart(void)
{
	clock_catchup_tick(&clkcatch, timer_us());
	clkcatch.armed = 1;
	SCB_REG32(SCB_SYST_RVR) = ((u_int)CPU_KHZ * 1000 / HZ) - 1;
	SCB_REG32(SCB_SYST_CVR) = 0;
	SCB_REG32(SCB_SYST_CSR) =
	    SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
#ifdef CORE1
	core1_init();
#endif
}

#ifdef CORE1
/*
 * The tick record, for core 1 to read: it moves at every delivered tick
 * and after every masked window, so a record that stands still is a core 0
 * that takes no clock interrupt. See rp2040/core1.h.
 */
const volatile u_int *
clock_beat(void)
{
	return &clkcatch.stamp_us;
}
#endif

/*
 * Record the tick the SysTick handler is delivering; see clock_catchup.h.
 */
void
clock_tick_stamp(void)
{
	clock_catchup_tick(&clkcatch, timer_us());
}

/*
 * Account the ticks lost while interrupts were masked. The caller holds
 * PRIMASK, which keeps SysTick from delivering between the pending-bit read
 * and the queue update. Callouts and ITIMER_REAL deadlines that become due
 * run from softclock on the tick after the window closes; the monotonic
 * clock that orders the deadlines takes the owed ticks as well.
 */
void
clock_catchup(void)
{
	unsigned int owed;

	owed = clock_catchup_sample(&clkcatch, timer_us, systick_pending,
	    CLOCK_TICK_US);
	if (owed != 0) {
		clock_catchup_apply(owed, hz, &lbolt, &time.tv_sec, &calltodo);
		monoclock_advance(owed);
	}
}
