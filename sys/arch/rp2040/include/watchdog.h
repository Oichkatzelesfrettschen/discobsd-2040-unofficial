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
 * The hardware watchdog's reset report, datasheet 4.7.
 *
 * Every spl level on ARMv6-M is PRIMASK (machine/intr.h), so a section that
 * masks interrupts and never returns stops the clock, the USB device
 * controller's interrupt, and with it the console, the reset interface and
 * every endpoint, while the device stays enumerated. The recovery is a
 * watchdog reset, which needs no interrupt and passes through any mask and
 * through a Cortex-M0+ lockup, and `picotool reboot -f` raises one from the
 * host.
 *
 * What the kernel keeps is the evidence such a reset would otherwise
 * destroy. SCRATCH1 names the section the kernel is inside while interrupts
 * are away and SCRATCH2 carries that section's argument; both survive the
 * soft reset the watchdog raises (datasheet 4.7.6), so the next boot reads
 * them beside REASON and reports where the previous kernel was. SCRATCH0
 * belongs to the swap cursor (machdep.c) and SCRATCH4 to SCRATCH7 to the
 * Boot ROM's reboot state (datasheet 2.8.1), so these two are free.
 *
 * The counter is left disabled. Arming it needs the reload to exceed the
 * longest section the kernel masks on purpose, a 4 KB sector erase through
 * the Boot ROM whose tSE maximum the part gives as 400 ms, and needs a
 * reload on every hardclock; the reach that decides the reload is the
 * counter's rate against the tick machdep.c starts, which
 * WATCHDOG_MEASURE measures and no build enables by default.
 * LOAD holds ticks times two (datasheet 4.7.3, erratum RP2040-E1).
 */
#ifndef _RP2040_WATCHDOG_H_
#define _RP2040_WATCHDOG_H_

#define	WATCHDOG_BASE		0x40058000UL
#define	WATCHDOG_CTRL		0x00
#define	WATCHDOG_LOAD		0x04
#define	WATCHDOG_REASON		0x08
#define	WATCHDOG_SCRATCH0	0x0c
#define	WATCHDOG_SCRATCH1	0x10
#define	WATCHDOG_SCRATCH2	0x14
#define	WATCHDOG_TICK		0x2c

#define	WATCHDOG_CTRL_ENABLE	(1UL << 30)
#define	WATCHDOG_CTRL_PAUSE_DBG1 (1UL << 26)
#define	WATCHDOG_CTRL_PAUSE_DBG0 (1UL << 25)
#define	WATCHDOG_CTRL_PAUSE_JTAG (1UL << 24)
#define	WATCHDOG_REASON_TIMER	(1UL << 0)
#define	WATCHDOG_REASON_FORCE	(1UL << 1)
#define	WATCHDOG_LOAD_MAX	0xffffffUL	/* 8.39 s at 2 counts per us */

/*
 * The power-on state machine resets, on a watchdog fire, every block whose
 * WDSEL bit is set (datasheet 2.13.4); everything but the two oscillators
 * is what the SDK selects, so the chip comes back through the Boot ROM.
 */
#define	PSM_BASE		0x40010000UL
#define	PSM_WDSEL		0x08
#define	PSM_WDSEL_ALL_BUT_OSC	0x1fffcUL

/* Sites a masked section names before it masks, cleared when it returns. */
#define	WD_SITE_NONE		0
#define	WD_SITE_FLASH_ERASE	1	/* arg: flash offset */
#define	WD_SITE_FLASH_PROGRAM	2	/* arg: flash offset */

#ifdef KERNEL
extern u_int watchdog_last_reason;	/* REASON at boot */
extern u_int watchdog_last_site;	/* SCRATCH1 at boot */
extern u_int watchdog_last_arg;		/* SCRATCH2 at boot */

void watchdog_init(void);
void watchdog_site(u_int site, u_int arg);
#endif

#endif /* !_RP2040_WATCHDOG_H_ */
