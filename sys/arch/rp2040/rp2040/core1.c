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

/*
 * Core 1 as the kernel's observer, selected by "options CORE1". core1.h
 * states what each core decides about the other; this file is the registers
 * those decisions read and write.
 *
 * The kernel itself stays on core 0. sys/arch/rp2040/doc/CORE1.md is the
 * authority for this mechanism, its cost and the evidence behind it.
 *
 * What core 1 executes and reads lies in SRAM, in peripherals or in the
 * boot ROM. A flash erase or program takes execute-in-place down for both
 * cores (dev/flash.c), so one instruction or literal fetched from flash
 * during that window would be undefined. Every function core 1 runs is
 * therefore in .ramfunc, including this unit's private copy of corering.c,
 * its vector table and stack are in .bss, and it takes no interrupt: it
 * masks them, sleeps in WFI and is woken by its own SysTick becoming
 * pending. tools/verify_rp2040_core1_image.py holds each linked kernel to
 * that rule, and tests/core1 executes the linked code with no flash mapped.
 *
 * Word ownership, since ARMv6-M has nothing to build a lock from: core 1
 * stores core1.c1_obs, c1_faults, the reply ring's producer words, the
 * request ring's consumer words and core1_probe[0]; core 0 stores the rest,
 * and re-initializes the rings and vector table only while core 1 is in
 * reset or parked in the boot ROM.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/sysctl.h>

#include <machine/scb.h>
#include <machine/watchdog.h>

#include <rp2040/rp2040/core1.h>

#define	C1_RAM		__attribute__((noinline, section(".ramfunc")))

/*
 * One copy of the transport. The producer and consumer, which core 1 also
 * executes, are in SRAM; the requester side, core 0's alone, is in flash.
 */
#define	CRING_SLOTS	8U
#define	CRING_FN	static C1_RAM __attribute__((unused))
#define	CRING_REQ_FN	static
#include "corering.c"

#define	REG32(a)		(*(volatile u_int *)(a))
#define	ALIAS_SET		0x2000		/* atomic bit set, 2.1.2 */
#define	ALIAS_CLR		0x3000		/* atomic bit clear */

#define	RESETS_BASE		0x4000c000UL
#define	RESETS_RESET		0x00
#define	RESETS_DONE		0x08
#define	RESETS_SYSCFG		(1UL << 18)

#define	SYSCFG_BASE		0x40004000UL
#define	SYSCFG_PROC0_NMI_MASK	0x00

#define	TIMER_BASE		0x40054000UL
#define	TIMER_TIMERAWL		0x28
#define	TIMER_INTF		0x3c

#define	SCB_SCR			(SCB_BASE + 0xd10)

/*
 * The probe interrupt is timer alarm 3, IRQ 3, which no driver arms or
 * enables. Core 1 forces it through TIMER.INTF, the line reaches both
 * NVICs, and SYSCFG.PROC0_NMI_MASK bit 3 turns it into core 0's NMI.
 */
#define	C1_PROBE_BIT		(1UL << 3)

/* Core 1's SysTick reload for one sample period. */
#define	C1_PERIOD_CYCLES	((u_int)CPU_KHZ * (C1_PERIOD_US / 1000U))
_Static_assert(C1_PERIOD_CYCLES - 1 <= 0xffffffU,
    "the sample period fits the 24-bit SysTick reload");

/*
 * Budget for the boot ROM's side of a launch, which takes microseconds
 * when core 1 answers. The launch runs in a callout, inside the clock
 * interrupt, so a launch that fails holds off that many clock ticks.
 */
#define	C1_LAUNCH_US		10000U

/*
 * tests/core1/emulate.py measures the deepest use, 88 bytes, and fails
 * when fewer than 64 bytes stay untouched: one exception frame for
 * core1_fault and as much again.
 */
#define	C1_STACK_WORDS		40

/*
 * VTOR holds bits 31:8 of the table address on a Cortex-M0+, so the table
 * leads a 256-byte aligned object, and the rest of that object is the
 * state the cores share.
 */
static struct core1_shared {
	u_int		c1_vec[16];
	struct cring	c1_req;		/* core 0 produces */
	struct cring	c1_rep;		/* core 1 produces */
	struct core1_obs c1_obs;
	u_int		c1_faults;	/* exceptions core 1 took */
} __attribute__((aligned(256))) core1;

_Static_assert(sizeof(struct core1_shared) == 256,
    "the shared block is one VTOR alignment unit");

/*
 * [0] probes core 1 raised; [1] the count NMI_Handler last answered;
 * [2] and [3] the PC and LR it found stacked. [1] is stored last, so a
 * reader that finds it equal to [0] holds the answer to the newest probe.
 */
u_int		core1_probe[4];
u_int		core1_nmi_mask;

static u_int	core1_stack[C1_STACK_WORDS];
static const volatile u_int *core1_beat;	/* core 0's tick record */
static struct cring_chan core1_chan;
static struct core1_sup core1_sup;

extern const volatile u_int *clock_beat(void);
extern void	clock_catchup(void);

/*
 * Core 1, from here to core1_main().
 */

static C1_RAM void
core1_fault(void)
{
	core1.c1_faults++;
	for (;;)
		continue;
}

static C1_RAM void
core1_op(void *ctx __attribute__((unused)), const struct cring_entry *e)
{
	struct cring_entry r;

	r.ce_op = e->ce_op == C1_OP_PING ? C1_OP_PONG : C1_OP_BAD;
	r.ce_slot = e->ce_slot;
	r.ce_arg = e->ce_arg;
	(void)cring_put(&core1.c1_rep, &r);
}

/*
 * The watchdog's scratch registers survive the reset its trigger raises
 * (datasheet 4.7.6), and watchdog_init() reports them at the next boot. A
 * declared section already names itself there. Otherwise the record is the
 * PC and LR of core 0's answer to the last probe, or zeros when that probe
 * went unanswered.
 */
static C1_RAM __attribute__((noreturn)) void
core1_reset(u_int act)
{
	u_int answered;

	if (act & C1_ACT_RECORD) {
		answered = core1_probe[1] == core1_probe[0];
		REG32(WATCHDOG_BASE + WATCHDOG_SCRATCH1) = WD_SITE_STALL;
		REG32(WATCHDOG_BASE + WATCHDOG_SCRATCH2) =
		    answered ? core1_probe[2] : 0;
		REG32(WATCHDOG_BASE + WATCHDOG_SCRATCH3) =
		    answered ? core1_probe[3] : 0;
	}
	REG32(PSM_BASE + PSM_WDSEL) = PSM_WDSEL_ALL_BUT_OSC;
	REG32(WATCHDOG_BASE + ALIAS_SET + WATCHDOG_CTRL) =
	    WATCHDOG_CTRL_TRIGGER;
	for (;;)
		continue;
}

/*
 * Entered from the boot ROM's launch with the stack and vector table
 * core1_launch() named. WFI returns when an exception that PRIMASK alone
 * holds off becomes pending (ARMv6-M ARM B1.5.19), so the pending SysTick
 * is the period and no handler runs. The boot ROM parks core 1 with
 * SCR.SLEEPDEEP set; the write to SCR selects plain sleep.
 */
static C1_RAM __attribute__((noreturn)) void
core1_main(void)
{
	u_int site, stamp, act;

	__asm__ volatile ("cpsid i" ::: "memory");
	REG32(SCB_SCR) = 0;
	REG32(SCB_SYST_RVR) = C1_PERIOD_CYCLES - 1;
	REG32(SCB_SYST_CVR) = 0;
	REG32(SCB_SYST_CSR) =
	    SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
	for (;;) {
		__asm__ volatile ("wfi" ::: "memory");
		REG32(SCB_ICSR) = SCB_ICSR_PENDSTCLR;
		(void)cring_service(&core1.c1_req, core1_op, NULL);
		site = REG32(WATCHDOG_BASE + WATCHDOG_SCRATCH1);
		stamp = *core1_beat;
		act = core1_observe(&core1.c1_obs,
		    REG32(TIMER_BASE + TIMER_TIMERAWL), stamp, site);
		if (act & C1_ACT_PROBE) {
			core1_probe[0]++;
			REG32(TIMER_BASE + ALIAS_SET + TIMER_INTF) =
			    C1_PROBE_BIT;
		}
		if (act & C1_ACT_RESET)
			core1_reset(act);
	}
}

/*
 * Core 0, from here down.
 */

/*
 * The probe. NMI preempts every priority PRIMASK and HardFault can hold, so
 * the frame under it is where core 0 was executing. The handler stores the
 * stacked PC and LR, then the probe count it answers, and withdraws the
 * forced interrupt. It takes the frame from the stack EXC_RETURN bit 2
 * selects, as SysTick_Handler does.
 */
__attribute__((naked)) void
NMI_Handler(void)
{
__asm volatile (
"	.syntax	unified		\n\t"
"	.thumb			\n\t"
"	movs	r0, #0x4	\n\t"
"	mov	r1, lr		\n\t"
"	tst	r0, r1		\n\t"
"	beq	1f		\n\t"
"	mrs	r0, PSP		\n\t"
"	b	2f		\n\t"
"1:	mrs	r0, MSP		\n\t"
"2:	ldr	r3, =core1_probe \n\t"
"	ldr	r1, [r0, #24]	\n\t"	/* Stacked PC. */
"	str	r1, [r3, #8]	\n\t"
"	ldr	r1, [r0, #20]	\n\t"	/* Stacked LR. */
"	str	r1, [r3, #12]	\n\t"
"	ldr	r1, [r3, #0]	\n\t"
"	str	r1, [r3, #4]	\n\t"
"	ldr	r3, =0x4005703c	\n\t"	/* TIMER.INTF, clear alias. */
"	movs	r1, #0x8	\n\t"
"	str	r1, [r3]	\n\t"
"	bx	lr		\n\t"
);
}

_Static_assert(TIMER_BASE + ALIAS_CLR + TIMER_INTF == 0x4005703cUL &&
    C1_PROBE_BIT == 0x8, "NMI_Handler names the probe's clear register");

/*
 * Reset core 1 into the boot ROM, where it waits for a launch and touches
 * no kernel memory. PSM.FRCE_OFF bit 16 holds the core in reset; the read
 * back confirms the bit before it is released (datasheet 2.13). Safe from
 * any context and whatever core 1 was doing, which is why boot(), the
 * kernel fault path and the USB reset requests call it first: a reset of
 * core 0 through AIRCR.SYSRESETREQ leaves core 1 running (datasheet
 * 2.4.2.9).
 */
void
core1_park(void)
{
	u_int n;

	if (core1_nmi_mask != 0) {
		core1_nmi_mask = 0;
		REG32(SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK) = 0;
	}
	REG32(PSM_BASE + ALIAS_SET + PSM_FRCE_OFF) = PSM_FRCE_OFF_PROC1;
	for (n = 0; n < 64; n++)
		if (REG32(PSM_BASE + PSM_FRCE_OFF) & PSM_FRCE_OFF_PROC1)
			break;
	REG32(PSM_BASE + ALIAS_CLR + PSM_FRCE_OFF) = PSM_FRCE_OFF_PROC1;
	REG32(TIMER_BASE + ALIAS_CLR + TIMER_INTF) = C1_PROBE_BIT;
}

/* Wait for FIFO_ST to show bit, until the launch budget from t0 runs out. */
static int
core1_fifo_wait(u_int bit, u_int t0)
{
	while ((REG32(SIO_BASE + SIO_FIFO_ST) & bit) == 0)
		if (REG32(TIMER_BASE + TIMER_TIMERAWL) - t0 >= C1_LAUNCH_US)
			return 0;
	return 1;
}

static void
core1_fifo_drain(void)
{
	while (REG32(SIO_BASE + SIO_FIFO_ST) & FIFO_ST_VLD)
		(void)REG32(SIO_BASE + SIO_FIFO_RD);
}

/*
 * The boot ROM's launch protocol, datasheet 2.8.2: core 1 pushes a zero
 * when it has reached its wait loop, then echoes each word of the sequence
 * 0, 0, 1, vector table, stack pointer, entry, and a wrong echo restarts
 * the sequence. Every wait and the sequence as a whole are bounded, so a
 * core 1 that never answers, or never echoes correctly, costs C1_LAUNCH_US
 * and reports failure. Both FIFOs are empty when the
 * last echo has been read, which is the state the rings start from.
 */
static int
core1_launch(void)
{
	u_int cmd[6], seq, i, t0;

	core1_park();
	cring_init(&core1.c1_req);
	cring_init(&core1.c1_rep);
	cring_chan_init(&core1_chan, &core1.c1_req, &core1.c1_rep);
	for (i = 2; i < 16; i++)
		core1.c1_vec[i] = (u_int)core1_fault;
	cmd[0] = 0;
	cmd[1] = 0;
	cmd[2] = 1;
	cmd[3] = (u_int)core1.c1_vec;
	cmd[4] = (u_int)&core1_stack[C1_STACK_WORDS];
	cmd[5] = (u_int)core1_main;

	t0 = REG32(TIMER_BASE + TIMER_TIMERAWL);
	if (!core1_fifo_wait(FIFO_ST_VLD, t0))
		return 0;
	seq = 0;
	do {
		if (REG32(TIMER_BASE + TIMER_TIMERAWL) - t0 >= C1_LAUNCH_US)
			return 0;
		if (cmd[seq] == 0) {
			core1_fifo_drain();
			__asm__ volatile ("sev");
		}
		if (!core1_fifo_wait(FIFO_ST_RDY, t0))
			return 0;
		REG32(SIO_BASE + SIO_FIFO_WR) = cmd[seq];
		__asm__ volatile ("sev");
		if (!core1_fifo_wait(FIFO_ST_VLD, t0))
			return 0;
		seq = REG32(SIO_BASE + SIO_FIFO_RD) == cmd[seq] ? seq + 1 : 0;
	} while (seq < 6);

	core1_nmi_mask = C1_PROBE_BIT;
	REG32(SYSCFG_BASE + SYSCFG_PROC0_NMI_MASK) = C1_PROBE_BIT;
	return 1;
}

static void
core1_pong(void *ctx __attribute__((unused)), const struct cring_entry *e)
{
	core1_sup_reply(&core1_sup, e->ce_op, e->ce_slot, e->ce_arg);
}

/*
 * The supervisor, a callout once a second. Every ring access core 0 makes
 * happens here, so the channel has the one caller corering.h requires.
 */
static void
core1_poll(caddr_t arg __attribute__((unused)))
{
	struct cring_entry e;
	u_int act;

	if (core1_sup.cs_state == C1_RUN)
		(void)cring_replies(&core1_chan, core1_pong, NULL);
	act = core1_sup_tick(&core1_sup);
	if (act == C1_SUP_LAUNCH)
		act = core1_sup_launched(&core1_sup, core1_launch());
	if (act == C1_SUP_PARK) {
		core1_park();
	} else if (act == C1_SUP_PING) {
		e.ce_op = C1_OP_PING;
		e.ce_slot = (unsigned short)core1_sup.cs_seq;
		e.ce_arg = core1_sup.cs_epoch;
		(void)cring_request(&core1_chan, &e);
	}
	if (core1_sup.cs_state == C1_RUN || core1_sup.cs_state == C1_WAIT)
		timeout(core1_poll, NULL, hz);
}

/*
 * Called from clkstart(), so the tick record core 1 samples is live before
 * the first launch. SYSCFG leaves reset here because the launch and every
 * later watchdog_site() write its NMI mask.
 */
void
core1_init(void)
{
	REG32(RESETS_BASE + ALIAS_CLR + RESETS_RESET) = RESETS_SYSCFG;
	while ((REG32(RESETS_BASE + RESETS_DONE) & RESETS_SYSCFG) == 0)
		continue;
	core1_beat = clock_beat();
	core1_sup_start(&core1_sup);
	timeout(core1_poll, NULL, hz);
}

/*
 * Hold interrupts off for us microseconds with no section declared: the
 * fault the observer exists to report, raised on request so that a board
 * test can check the report. machdep.core1_stall_ms reaches it.
 */
__attribute__((noinline)) void
core1_stall(u_int us)
{
	u_int t0;
	int s;

	s = splhigh();
	t0 = REG32(TIMER_BASE + TIMER_TIMERAWL);
	while (REG32(TIMER_BASE + TIMER_TIMERAWL) - t0 < us)
		continue;
	clock_catchup();
	splx(s);
}

/*
 * machdep.core1 and the nodes after it, in the order of CPU_CORE1 in
 * machine/cpu.h. Writing machdep.core1 parks core 1 (0) or launches it
 * again with a fresh restart budget (nonzero); a debugger that halts core
 * 0 for longer than C1_HANG_US needs it parked first.
 */
int
core1_sysctl(int which, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	static const u_int *const word[] = {
		&core1_sup.cs_state,
		&core1_sup.cs_restarts,
		&core1.c1_obs.co_stalls,
		&core1.c1_obs.co_gap_us,
		&core1_probe[2],
		&core1_probe[3],
		&core1.c1_faults,
	};
	int v, error, s;

	v = which < (int)(sizeof word / sizeof word[0]) ?
	    (int)*(const volatile u_int *)word[which] : 0;
	error = sysctl_int(oldp, oldlenp, newp, newlen, &v);
	if (error != 0 || newp == NULL)
		return error;
	if (which == 0) {
		s = splhigh();
		if (v == 0) {
			core1_sup.cs_state = C1_OFF;
			core1_park();
		} else if (core1_sup.cs_state != C1_RUN &&
		    core1_sup.cs_state != C1_WAIT) {
			core1_sup_start(&core1_sup);
			untimeout(core1_poll, NULL);
			timeout(core1_poll, NULL, hz);
		}
		splx(s);
		return 0;
	}
	if (which == (int)(sizeof word / sizeof word[0])) {
		if (v < 0 || v > 10000)
			return EINVAL;
		core1_stall((u_int)v * 1000U);
		return 0;
	}
	return EPERM;
}
