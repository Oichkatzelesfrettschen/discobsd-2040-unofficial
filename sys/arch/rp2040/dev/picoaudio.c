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
 * The PicoCalc's bell: a short square-wave tone on PWM slice 5.
 *
 * The board's audio amplifier takes the left channel on GP26 and the right
 * on GP27 (clockwork_Mainboard_V2.0_Schematic.pdf; PICOCALC.md), which the
 * RP2040 routes to PWM5 A and PWM5 B through function 4 (datasheet 1.4.3).
 * The slice counts clk_sys divided by BELL_DIV_INT, so BELL_COUNT counts
 * make one period of BELL_HZ, and the compare value holds the output high
 * for BELL_DUTY_PCT percent of it on both channels (datasheet 4.5.2.1,
 * registers 4.5.3). lcd_bell() starts the tone and arms a callout that
 * stops it, so the terminal's output path neither waits for the tone nor
 * holds a timer; a BEL that arrives while the tone sounds is absorbed.
 *
 * A disabled slice stops counting and its outputs hold the level they had.
 * CC is double buffered (datasheet 4.5.2.3): a store reaches the compare
 * logic only when the counter wraps, so zeroing it and disabling the slice
 * at once leaves the old duty cycle in force. Stopping therefore forces both
 * pins low through the GPIO output override (OUTOVER in GPIOn_CTRL, datasheet 2.19.6)
 * before it disables the slice, which holds in every phase of the period
 * and needs no wait. Starting a tone routes the pins back to the slice,
 * which removes the override.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include <rp2040/dev/lcdterm.h>

#define	PWM_SLICE5		(0x40050000UL + 5 * 0x14)
#define	PWM_CSR			0x00
#define	PWM_DIV			0x04
#define	PWM_CC			0x0c
#define	PWM_TOP			0x10
#define	PWM_CSR_EN		(1UL << 0)
#define	PWM_DIV_INT(n)		((u_int)(n) << 4)
#define	PWM_REG(off)		PC_REG32(PWM_SLICE5 + (off))

#define	PC_RESET_PWM		(1UL << 14)
#define	PC_FUNC_PWM		4
#define	GPIO_OUTOVER_LOW	(2UL << 8)	/* GPIOn_CTRL bits 9:8. */

#define	BELL_HZ			1000
#define	BELL_MS			100
#define	BELL_DUTY_PCT		25
#define	BELL_DIV_INT		(CPU_KHZ / 1000)	/* 1 MHz counter. */
#define	BELL_COUNT		(1000000 / BELL_HZ)
#define	BELL_TICKS		((BELL_MS * HZ + 999) / 1000)

_Static_assert(BELL_DIV_INT >= 1 && BELL_DIV_INT <= 255,
    "DIV.INT is eight bits");
_Static_assert(BELL_COUNT >= 2 && BELL_COUNT <= 65536,
    "TOP is sixteen bits");

static int bell_ready;		/* The slice and the pin banks are out of reset. */
static int bell_on;		/* The tone sounds or its start is under way. */

static void
bell_stop(caddr_t arg __unused)
{
	PC_REG32(PC_IO_BANK0_CTRL(PICOCALC_AUDIO_L)) =
	    PC_FUNC_PWM | GPIO_OUTOVER_LOW;
	PC_REG32(PC_IO_BANK0_CTRL(PICOCALC_AUDIO_R)) =
	    PC_FUNC_PWM | GPIO_OUTOVER_LOW;
	PWM_REG(PWM_CSR) = 0;
	bell_on = 0;
}

void
lcd_bell(void)
{
	u_int cc = BELL_COUNT * BELL_DUTY_PCT / 100;
	int s;

	if (panicstr != NULL)
		return;		/* Callouts no longer run. */
	s = splhigh();
	if (bell_on) {
		splx(s);
		return;
	}
	bell_on = 1;
	splx(s);

	if (!bell_ready) {
		pc_unreset(PC_RESET_IO_BANK0 | PC_RESET_PADS_BANK0 |
		    PC_RESET_PWM);
		bell_ready = 1;
	}
	PWM_REG(PWM_TOP) = BELL_COUNT - 1;
	PWM_REG(PWM_DIV) = PWM_DIV_INT(BELL_DIV_INT);
	PWM_REG(PWM_CC) = cc | (cc << 16);
	PWM_REG(PWM_CSR) = PWM_CSR_EN;
	pc_pin(PICOCALC_AUDIO_L, PC_FUNC_PWM, 0);
	pc_pin(PICOCALC_AUDIO_R, PC_FUNC_PWM, 0);
	timeout(bell_stop, (caddr_t)0, BELL_TICKS);
}
