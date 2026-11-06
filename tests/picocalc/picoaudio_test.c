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
 * Host test of the PicoCalc bell (sys/arch/rp2040/dev/picoaudio.c),
 * compiled unchanged against the register model in regmodel.h.
 *
 * The test stops the tone at every counter phase of the period and reads
 * the level each amplifier pin takes from the registers the driver left,
 * with the rules of the PWM slice (datasheet 4.5.2): the output is high
 * while the counter is below the latched CC, CC is latched from the
 * register only at a counter wrap (double buffering, 4.5.2.3), a disabled
 * slice keeps the counter and the latched CC where they stopped, and
 * OUTOVER in GPIOn_CTRL (2.19.6) replaces the peripheral's level with a
 * constant. After the stop callout both pins must read low at every phase,
 * the slice must be disabled, and the next tone must route the pins back
 * to the slice.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/param.h>

#include "regmodel.h"

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/lcdterm.h>

#define	PWM_SLICE5		(0x40050000UL + 5 * 0x14)
#define	PWM_CSR			0x00
#define	PWM_DIV			0x04
#define	PWM_CC			0x0c
#define	PWM_TOP			0x10
#define	GPIO_CTRL(n)		(0x40014000UL + 0x004 + 8 * (n))
#define	FUNC_PWM		4
#define	OUTOVER_SHIFT		8
#define	OUTOVER_LOW		2
#define	OUTOVER_HIGH		3

const char *panicstr;
static void (*pending)(char *);
static char *pending_arg;
static int pending_ticks;
static int ntimeout;
static int failures;

void
timeout(void (*fn)(char *), char *arg, int ticks)
{
	pending = fn;
	pending_arg = arg;
	pending_ticks = ticks;
	ntimeout++;
}

static void
check(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

/*
 * Level of pin n when the slice stopped, or would stop, with the counter
 * at phase and the compare value already latched: -1 when the pin is not
 * driven by the slice or an override.
 */
static int
pin_level(int pin, unsigned phase, unsigned latched_cc)
{
	unsigned ctrl = model_reg(GPIO_CTRL(pin));
	unsigned over = (ctrl >> OUTOVER_SHIFT) & 3;
	unsigned cc = pin == PICOCALC_AUDIO_L ? latched_cc & 0xffff :
	    latched_cc >> 16;

	if (over == OUTOVER_LOW)
		return 0;
	if (over == OUTOVER_HIGH)
		return 1;
	if ((ctrl & 0x1f) != FUNC_PWM || over != 0)
		return -1;
	return phase < cc;
}

int
main(void)
{
	unsigned top, cc, phase;
	char what[96];

	/* A tone starts: pins routed to the slice, no override, slice on. */
	lcd_bell();
	check("first tone arms one stop callout", ntimeout, 1);
	check("stop after 100 ms", pending_ticks, 100);
	top = model_reg(PWM_SLICE5 + PWM_TOP);
	cc = model_reg(PWM_SLICE5 + PWM_CC);
	check("period is 1 ms at a 1 MHz count", top + 1, 1000);
	check("both channels hold the 25% compare value", cc,
	    250U | (250U << 16));
	check("slice enabled", model_reg(PWM_SLICE5 + PWM_CSR) & 1, 1);
	check("left pin on the slice, no override",
	    model_reg(GPIO_CTRL(PICOCALC_AUDIO_L)), FUNC_PWM);
	check("right pin on the slice, no override",
	    model_reg(GPIO_CTRL(PICOCALC_AUDIO_R)), FUNC_PWM);

	/* A BEL during the tone is absorbed. */
	lcd_bell();
	check("second BEL while sounding arms nothing", ntimeout, 1);

	/*
	 * Stop at each counter phase. The latched compare value is the one
	 * the tone started with; the zero a stop might write has not reached
	 * the compare logic if no wrap has followed it.
	 */
	for (phase = 0; phase <= top; phase++) {
		unsigned csr, saved_cc = cc;

		/* Rewind to a running tone, then stop it at this phase. */
		model_reg(PWM_SLICE5 + PWM_CSR) = 1;
		model_reg(GPIO_CTRL(PICOCALC_AUDIO_L)) = FUNC_PWM;
		model_reg(GPIO_CTRL(PICOCALC_AUDIO_R)) = FUNC_PWM;
		model_reg(PWM_SLICE5 + PWM_CC) = saved_cc;
		pending(pending_arg);
		csr = model_reg(PWM_SLICE5 + PWM_CSR);
		if ((csr & 1) != 0) {
			snprintf(what, sizeof(what),
			    "phase %u: slice left enabled", phase);
			check(what, csr & 1, 0);
		}
		if (pin_level(PICOCALC_AUDIO_L, phase, saved_cc) != 0) {
			snprintf(what, sizeof(what),
			    "phase %u: left pin not low after the stop", phase);
			check(what, pin_level(PICOCALC_AUDIO_L, phase,
			    saved_cc), 0);
		}
		if (pin_level(PICOCALC_AUDIO_R, phase, saved_cc) != 0) {
			snprintf(what, sizeof(what),
			    "phase %u: right pin not low after the stop", phase);
			check(what, pin_level(PICOCALC_AUDIO_R, phase,
			    saved_cc), 0);
		}
		/* The stop clears the busy flag; restore it for the next. */
		if (phase < top) {
			ntimeout = 0;
			lcd_bell();
			check("restart after a stop arms a callout", ntimeout,
			    1);
		}
	}

	/* The next tone gives the pins back to the slice. */
	ntimeout = 0;
	model_reg(PWM_SLICE5 + PWM_CSR) = 0;
	lcd_bell();
	check("tone after a stop arms a callout", ntimeout, 1);
	check("restart enables the slice",
	    model_reg(PWM_SLICE5 + PWM_CSR) & 1, 1);
	check("restart releases the left override",
	    model_reg(GPIO_CTRL(PICOCALC_AUDIO_L)), FUNC_PWM);
	check("restart releases the right override",
	    model_reg(GPIO_CTRL(PICOCALC_AUDIO_R)), FUNC_PWM);
	pending(pending_arg);

	/* After a panic callouts no longer run, so no tone starts. */
	panicstr = "test";
	ntimeout = 0;
	lcd_bell();
	check("no tone after a panic", ntimeout, 0);

	if (failures != 0) {
		fprintf(stderr, "picoaudio_test: %d failures\n", failures);
		return 1;
	}
	printf("picoaudio_test: ok\n");
	return 0;
}
