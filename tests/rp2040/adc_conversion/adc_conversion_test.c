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
	puts("adc_conversion: PASS (all 4096 readings)");
	return 0;
}
