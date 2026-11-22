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
 * The RP2040's 12-bit ADC (datasheet 4.9) as two converted readings: the die
 * temperature from the sensor on input 4 and the Pico's VSYS from input 3
 * (GPIO29), which the board feeds with VSYS divided by 3 (Pico datasheet,
 * section 2.1 and the GPIO29 row of its pin list). adc.c holds the register
 * side; the conversions are here so that a host program checks them.
 *
 * The reference is the Pico's 3.3 V ADC_AVDD, so one count is 3300 / 4096
 * millivolts. The sensor's base-emitter voltage is typically 0.706 V at
 * 27 degrees C with a slope of -1.721 mV per degree (datasheet 4.9.5), a
 * typical figure with no per-part calibration.
 */

#ifndef _RP2040_DEV_ADC_H_
#define	_RP2040_DEV_ADC_H_

#define	ADC_INPUT_VSYS		3	/* GPIO29, VSYS / 3 on the Pico. */
#define	ADC_INPUT_TEMP		4	/* The internal sensor. */
#define	ADC_RAW_MAX		4095U

/*
 * The refusals of machdep.adc.N, as the errno values they become (EINVAL,
 * EPERM, EBUSY; sysctl.c asserts the match).
 */
#define	ADC_ERR_RANGE		22	/* no such input */
#define	ADC_ERR_HELD		1	/* the board owns the pin */
#define	ADC_ERR_BUSY		16	/* the GPIO device holds the pin */

/*
 * Zero when a program may read input, else the refusal. Inputs 0-2 are
 * GP26-GP28 and need the pin in free_pins, the board's reachable set
 * (gpio_board_pins()), and out of owned_pins, the pins the GPIO device
 * has configured. Input 3 is the VSYS divider and input 4 the die sensor:
 * neither is a pin the GPIO device reaches, and both have had nodes of
 * their own from the start.
 */
static inline int
adc_input_check(unsigned int free_pins, unsigned int owned_pins,
    unsigned int input)
{
	unsigned int pin;

	if (input > ADC_INPUT_TEMP)
		return ADC_ERR_RANGE;
	if (input >= ADC_INPUT_VSYS)
		return 0;
	pin = 1U << (26 + input);
	if ((free_pins & pin) == 0)
		return ADC_ERR_HELD;
	if ((owned_pins & pin) != 0)
		return ADC_ERR_BUSY;
	return 0;
}

/* Microvolts at the ADC input: 3,300,000 / 4096 reduces to 825,000 / 1024. */
static inline unsigned int
adc_microvolts(unsigned int raw)
{
	return raw * 825000U / 1024U;
}

/* VSYS in millivolts from a reading of input 3. */
static inline unsigned int
adc_vsys_millivolts(unsigned int raw)
{
	return raw * 9900U / 4096U;
}

/*
 * Die temperature in thousandths of a degree C from a reading of input 4:
 * 27 - (V - 0.706) / 0.001721. The quotient and remainder by 1721 are
 * scaled separately so that no intermediate leaves 32 bits.
 */
static inline int
adc_temp_millicelsius(unsigned int raw)
{
	int d = (int)adc_microvolts(raw) - 706000;

	return 27000 - (d / 1721 * 1000 + d % 1721 * 1000 / 1721);
}

#ifdef KERNEL
int	adc_read(unsigned int input, unsigned int *raw);
#endif

#endif /* !_RP2040_DEV_ADC_H_ */
