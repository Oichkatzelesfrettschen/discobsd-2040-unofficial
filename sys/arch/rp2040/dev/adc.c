/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#define	IO_BANK0_CTRL(pin)	(0x40014004UL + 8 * (pin))
#define	IO_FUNC_NULL		0x1f
#define	PADS_BANK0_GPIO(pin)	(0x4001c004UL + 4 * (pin))
#define	PAD_OD			(1UL << 7)	/* Output disable. */
#define	ADC_FIRST_PIN		26		/* Input 0 is GP26. */

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
	AREG32(ADC_CS) = ADC_CS_EN | ADC_CS_TS_EN;
	return adc_wait_ready();
}

/*
 * A pin input becomes an analog input: no function, digital input and
 * output off and both pulls off, because the reset pull-down would load
 * the source (the VSYS divider on input 3). Each reading repeats it, so a
 * pin that the GPIO device configured since the last reading is taken back.
 */
static void
adc_pad(unsigned int input)
{
	AREG32(IO_BANK0_CTRL(ADC_FIRST_PIN + input)) = IO_FUNC_NULL;
	AREG32(PADS_BANK0_GPIO(ADC_FIRST_PIN + input)) = PAD_OD;
}

/*
 * Convert one input, 0-4, and store the 12-bit result, after preparing its
 * pad. Returns 0, or -1 when the block does not come up or never finishes.
 * Process context; nothing else uses the block.
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
	if (input <= ADC_INPUT_VSYS)
		adc_pad(input);
	AREG32(ADC_CS) = ADC_CS_EN | ADC_CS_TS_EN |
	    (input << ADC_CS_AINSEL_SHIFT) | ADC_CS_START_ONCE;
	if (adc_wait_ready() != 0)
		return -1;
	*raw = AREG32(ADC_RESULT) & ADC_RAW_MAX;
	return 0;
}
