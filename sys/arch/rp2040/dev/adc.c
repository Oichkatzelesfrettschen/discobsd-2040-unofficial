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
 * One-shot conversions on the RP2040 ADC, datasheet 4.9. The block stays in
 * reset with clk_adc stopped until the first reading, so a kernel nobody
 * asks pays nothing for it.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <rp2040/dev/adc.h>

#define	AREG32(a)		(*(volatile u_int *)(a))

#define	RESETS_BASE		0x4000c000UL
#define	RESETS_RESET_CLR	0x3000		/* Atomic clear alias. */
#define	RESETS_DONE		0x8
#define	RESETS_ADC		(1UL << 0)

#define	CLOCKS_CLK_ADC_CTRL	0x40008060UL
#define	CLK_ADC_ENABLE		(1UL << 11)	/* AUXSRC 0 is pll_usb. */

#define	IO_BANK0_GPIO29_CTRL	0x400140ecUL
#define	IO_FUNC_NULL		0x1f
#define	PADS_BANK0_GPIO29	0x4001c078UL
#define	PAD_OD			(1UL << 7)	/* Output disable. */

#define	ADC_CS			0x4004c000UL
#define	ADC_RESULT		0x4004c004UL
#define	ADC_CS_EN		(1UL << 0)
#define	ADC_CS_TS_EN		(1UL << 1)
#define	ADC_CS_START_ONCE	(1UL << 2)
#define	ADC_CS_READY		(1UL << 8)
#define	ADC_CS_AINSEL_SHIFT	12

/*
 * A conversion takes 96 cycles of the 48 MHz clk_adc, 2 us (4.9); the
 * bound is far past that and past the block's start-up, and exists so
 * that a block that never reports ready returns an error.
 */
#define	ADC_SPINS		100000

static u_char adc_up;

static int
adc_wait_ready(void)
{
	u_int spins = ADC_SPINS;

	while ((AREG32(ADC_CS) & ADC_CS_READY) == 0)
		if (--spins == 0)
			return -1;
	return 0;
}

/*
 * Start clk_adc from pll_usb (48 MHz, the frequency the block requires),
 * release the block and enable it with the temperature sensor's bias on.
 * GPIO29 becomes an analog input: no function, digital input and output
 * off and both pulls off, because the reset pull-down would load the
 * VSYS divider.
 */
static int
adc_start(void)
{
	u_int spins = ADC_SPINS;

	AREG32(CLOCKS_CLK_ADC_CTRL) = CLK_ADC_ENABLE;
	AREG32(RESETS_BASE + RESETS_RESET_CLR) = RESETS_ADC;
	while ((AREG32(RESETS_BASE + RESETS_DONE) & RESETS_ADC) == 0)
		if (--spins == 0)
			return -1;
	AREG32(IO_BANK0_GPIO29_CTRL) = IO_FUNC_NULL;
	AREG32(PADS_BANK0_GPIO29) = PAD_OD;
	AREG32(ADC_CS) = ADC_CS_EN | ADC_CS_TS_EN;
	return adc_wait_ready();
}

/*
 * Convert one input, 0-4, and store the 12-bit result. Returns 0, or -1
 * when the block does not come up or never finishes. Process context;
 * nothing else uses the block.
 */
int
adc_read(unsigned int input, unsigned int *raw)
{
	if (input > ADC_INPUT_TEMP)
		return -1;
	if (!adc_up) {
		if (adc_start() != 0)
			return -1;
		adc_up = 1;
	}
	AREG32(ADC_CS) = ADC_CS_EN | ADC_CS_TS_EN |
	    (input << ADC_CS_AINSEL_SHIFT) | ADC_CS_START_ONCE;
	if (adc_wait_ready() != 0)
		return -1;
	*raw = AREG32(ADC_RESULT) & ADC_RAW_MAX;
	return 0;
}
