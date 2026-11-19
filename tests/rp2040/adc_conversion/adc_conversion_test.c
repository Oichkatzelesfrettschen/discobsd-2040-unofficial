#include <stdio.h>
#include <stdlib.h>

#include <adc.h>

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "adc_conversion: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

/*
 * The reference values are the datasheet's formula in floating point,
 * T = 27 - (V - 0.706) / 0.001721 with V = raw * 3.3 / 4096, and
 * VSYS = 3 * V. The integer forms must stay within two millidegrees (the
 * microvolt truncation moves the result by up to 0.58 and the division by
 * up to 1) and one millivolt of them for every 12-bit reading.
 */
int
main(void)
{
	unsigned int raw;
	double volts, temp, vsys, diff;
	int previous = 0;

	for (raw = 0; raw <= ADC_RAW_MAX; raw++) {
		volts = raw * 3.3 / 4096.0;
		temp = (27.0 - (volts - 0.706) / 0.001721) * 1000.0;
		diff = adc_temp_millicelsius(raw) - temp;
		require(diff > -2.0 && diff < 2.0,
		    "temperature is off the datasheet formula");
		vsys = volts * 3.0 * 1000.0;
		diff = adc_vsys_millivolts(raw) - vsys;
		require(diff > -1.0 && diff < 1e-6,
		    "VSYS is not the truncated divider voltage");
		if (raw != 0)
			require(adc_temp_millicelsius(raw) <= previous,
			    "temperature rises with the reading");
		previous = adc_temp_millicelsius(raw);
	}
	/* 0.706 V is reading 876.3; 876 sits just above 27 degrees. */
	require(adc_temp_millicelsius(876) > 27000 &&
	    adc_temp_millicelsius(877) < 27000,
	    "the 27 degree point is not between readings 876 and 877");
	/* A 5 V VSYS reads 5000 / 3 mV at the pin, reading 2068. */
	require(adc_vsys_millivolts(2068) == 4998,
	    "reading 2068 is not 4998 mV");

	/*
	 * Which inputs a program may read. The reachable sets are written from
	 * the Pico datasheet's pin list (GP2-GP22 and GP26-GP28 free) and the
	 * PicoCalc pin map (GP8, GP9 and GP28 free; GP26 and GP27 drive the
	 * audio amplifier), not from gpio.h.
	 */
	{
		const unsigned int pico = 0x1c7ffffcU;
		const unsigned int calc = 1U << 8 | 1U << 9 | 1U << 28;
		unsigned int input;

		require(ADC_ERR_RANGE == 22 && ADC_ERR_HELD == 1 &&
		    ADC_ERR_BUSY == 16, "a refusal left its errno value");
		for (input = 0; input <= 4; input++)
			require(adc_input_check(pico, 0, input) == 0,
			    "the Pico refuses an input");
		require(adc_input_check(calc, 0, 0) == ADC_ERR_HELD &&
		    adc_input_check(calc, 0, 1) == ADC_ERR_HELD,
		    "the PicoCalc lets a program read an amplifier pin");
		require(adc_input_check(calc, 0, 2) == 0 &&
		    adc_input_check(calc, 0, 3) == 0 &&
		    adc_input_check(calc, 0, 4) == 0,
		    "the PicoCalc refuses GP28, VSYS or the sensor");
		require(adc_input_check(0, 0, 0) == ADC_ERR_HELD &&
		    adc_input_check(0, 0, 2) == ADC_ERR_HELD &&
		    adc_input_check(0, 0, 3) == 0 &&
		    adc_input_check(0, 0, 4) == 0,
		    "an unknown board lets a program read a pin");
		require(adc_input_check(calc, 1U << 28, 2) == ADC_ERR_BUSY &&
		    adc_input_check(pico, 1U << 26, 0) == ADC_ERR_BUSY &&
		    adc_input_check(pico, 1U << 26, 1) == 0 &&
		    adc_input_check(calc, 1U << 8, 2) == 0,
		    "a pin the GPIO device holds is read, or another is refused");
		require(adc_input_check(pico, 0, 5) == ADC_ERR_RANGE &&
		    adc_input_check(pico, 0, 0xffffffffU) == ADC_ERR_RANGE &&
		    adc_input_check(calc, 0, 6) == ADC_ERR_RANGE,
		    "an input past the sensor is accepted");
	}
	puts("adc_conversion: PASS (all 4096 readings, input refusals)");
	return 0;
}
