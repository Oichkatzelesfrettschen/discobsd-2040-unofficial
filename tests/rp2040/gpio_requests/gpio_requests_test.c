#include <stdio.h>
#include <stdlib.h>

#include <gpio.h>

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "gpio_requests: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

/*
 * The pins each board's drivers and board functions hold, written from the
 * Pico datasheet's pin list and the PicoCalc pin map (doc/PICOCALC.md)
 * and not from gpio.h: UART0 on both; the Pico's regulator mode, VBUS
 * sense, LED and VSYS divider; the PicoCalc's PSRAM, keyboard bus, panel,
 * SD card with its detect line, and audio.
 */
static const int pico_held[] = { 0, 1, 23, 24, 25, 29 };
static const int picocalc_held[] = {
	0, 1, 2, 3, 4, 5, 20, 21, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 22, 26, 27, 23, 24, 25, 29
};

static unsigned int
held_mask(const int *pins, unsigned int n)
{
	unsigned int i, mask = 0;

	for (i = 0; i < n; i++)
		mask |= 1U << pins[i];
	return mask;
}

/*
 * The PWM request. The slice table is the datasheet's GPIO function list
 * (GP8 and GP9 PWM4 A and B, GP10 and GP11 PWM5, GP12 PWM6 A, GP26 and GP27
 * PWM5, GP28 and GP29 PWM6) and the PicoCalc's users of it: the bell and
 * /dev/audio's carrier on slice 5 (GP26, GP27) and /dev/audio's sample
 * clock on slice 6, a slice no pin of the PicoCalc's amplifier uses.
 */
static void
pwm_checks(void)
{
	static const struct { unsigned int pin, slice; } map[] = {
		{ 0, 0 }, { 1, 0 }, { 2, 1 }, { 8, 4 }, { 9, 4 }, { 10, 5 },
		{ 11, 5 }, { 12, 6 }, { 13, 6 }, { 14, 7 }, { 15, 7 },
		{ 16, 0 }, { 25, 4 }, { 26, 5 }, { 27, 5 }, { 28, 6 },
		{ 29, 6 }
	};
	const unsigned int gp8 = 1U << 8, gp9 = 1U << 9, gp28 = 1U << 28;
	unsigned int i, duty, word;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		require(GPIO_PWM_SLICE(map[i].pin) == map[i].slice &&
		    (GPIO_PWM_SLICE_PINS(map[i].slice) >> map[i].pin & 1) == 1,
		    "a pin is on another PWM slice than the datasheet gives");
	require(GPIO_PWM_SLICE_PINS(5) == (3U << 10 | 3U << 26) &&
	    GPIO_PWM_SLICE_PINS(6) == (3U << 12 | 3U << 28),
	    "slice 5 or 6 names other pins");

	/* PicoCalc: GP8, GP9 and GP28 only, slice 6 shared with the audio. */
	require(gpio_pwm_check(2, gp8, 0, 0) == 0 &&
	    gpio_pwm_check(2, gp8 | gp9, 128, 0) == 0 &&
	    gpio_pwm_check(2, gp28, 255, 0) == 0,
	    "the PicoCalc refuses a pin of its expansion header");
	require(gpio_pwm_check(2, gp8, 128, 1) == 0 &&
	    gpio_pwm_check(2, gp8 | gp9, 128, 1) == 0,
	    "the PicoCalc's slice 4 is refused while /dev/audio is open");
	require(gpio_pwm_check(2, gp28, 128, 1) == 16 &&
	    gpio_pwm_check(2, gp8 | gp28, 128, 1) == 16,
	    "GP28 is driven while /dev/audio's sample clock runs");
	require(gpio_pwm_check(2, 1U << 26, 128, 0) == 22 &&
	    gpio_pwm_check(2, 1U << 27, 128, 0) == 22 &&
	    gpio_pwm_check(2, 1U << 10, 128, 0) == 22 &&
	    gpio_pwm_check(2, gp8 | 1U << 26, 128, 0) == 22 &&
	    gpio_pwm_check(2, 1U << 2, 128, 0) == 22,
	    "a PicoCalc pin the board holds is driven");
	require(gpio_pwm_slice_held(2, 1U << 26, 0) == 1 &&
	    gpio_pwm_slice_held(2, 1U << 27, 0) == 1 &&
	    gpio_pwm_slice_held(2, 1U << 10, 0) == 1 &&
	    gpio_pwm_slice_held(2, 1U << 11, 1) == 1 &&
	    gpio_pwm_slice_held(2, gp28, 0) == 0 &&
	    gpio_pwm_slice_held(2, gp28, 1) == 1 &&
	    gpio_pwm_slice_held(2, 1U << 12, 1) == 1 &&
	    gpio_pwm_slice_held(2, gp8, 1) == 0,
	    "the PicoCalc's PWM slices are not slice 5 always and slice 6 "
	    "with the audio open");
	require(gpio_pwm_slice_held(1, 1U << 26, 1) == 0 &&
	    gpio_pwm_slice_held(1, gp28, 1) == 0 &&
	    gpio_pwm_slice_held(0, 1U << 26, 1) == 0,
	    "a bare Pico reports a slice the kernel uses");

	/* Bare Pico: its free pins, GP26 and GP27 on slice 5 included. */
	require(gpio_pwm_check(1, 1U << 2, 1, 0) == 0 &&
	    gpio_pwm_check(1, 1U << 26 | 1U << 27, 200, 0) == 0 &&
	    gpio_pwm_check(1, 1U << 10, 200, 0) == 0 &&
	    gpio_pwm_check(1, gp28, 200, 0) == 0,
	    "the Pico refuses a free pin");
	require(gpio_pwm_check(1, 1U << 0, 1, 0) == 22 &&
	    gpio_pwm_check(1, 1U << 1, 1, 0) == 22 &&
	    gpio_pwm_check(1, 1U << 25, 1, 0) == 22 &&
	    gpio_pwm_check(1, 1U << 29, 1, 0) == 22 &&
	    gpio_pwm_check(1, 1U << 2 | 1U << 23, 1, 0) == 22,
	    "the Pico drives UART0, the LED or the VSYS divider");
	require(gpio_pwm_check(0, gp8, 1, 0) == 22 &&
	    gpio_pwm_check(3, gp8, 1, 0) == 22,
	    "an unknown board drives a pin");

	/* Request shape: no mask, a duty past the maximum. */
	require(gpio_pwm_check(1, 0, 1, 0) == 22 &&
	    gpio_pwm_check(2, 0, 1, 0) == 22, "an empty mask is accepted");
	require(gpio_pwm_check(1, 1U << 2, 255, 0) == 0 &&
	    gpio_pwm_check(1, 1U << 2, 256, 0) == 22 &&
	    gpio_pwm_check(1, 1U << 2, 0xffffU, 0) == 22,
	    "the duty range is not 0 to 255");

	/*
	 * The carrier: a pin is high while the counter (0 to TOP) is below
	 * its compare value, so the duty is the number of high counts of
	 * 255, and the top duty exceeds TOP and holds the pin high.
	 */
	require(GPIO_PWM_DUTY_MAX == 255 && GPIO_PWM_TOP == 254,
	    "the PWM period is not 255 counts");
	for (duty = 0; duty <= GPIO_PWM_DUTY_MAX; duty++)
		require((duty > GPIO_PWM_TOP ? GPIO_PWM_TOP + 1 : duty) ==
		    duty, "a duty is not its high count of a period");
	require(GPIO_PWM_DUTY_MAX > GPIO_PWM_TOP,
	    "the top duty does not hold the pin high");
	require(125000000U / GPIO_PWM_DIV_INT / (GPIO_PWM_TOP + 1) > 1900 &&
	    125000000U / GPIO_PWM_DIV_INT / (GPIO_PWM_TOP + 1) < 1950,
	    "the carrier is not 1.9 kHz at 125 MHz");

	/* The compare word: channel A low, channel B high, the other kept. */
	for (duty = 0; duty <= GPIO_PWM_DUTY_MAX; duty++) {
		word = gpio_pwm_compare(8, 0xaaaa5555U, duty);
		require(word == (0xaaaa0000U | duty), "channel A is not low");
		word = gpio_pwm_compare(9, 0xaaaa5555U, duty);
		require(word == (0x00005555U | duty << 16),
		    "channel B is not high");
	}
	require(gpio_pwm_compare(28, 0, 7) == 7 &&
	    gpio_pwm_compare(27, 0, 7) == 7U << 16, "GP28 or GP27 channel");
}

int
main(void)
{
	unsigned int pico = held_mask(pico_held,
	    sizeof(pico_held) / sizeof(pico_held[0]));
	unsigned int calc = held_mask(picocalc_held,
	    sizeof(picocalc_held) / sizeof(picocalc_held[0]));
	unsigned int all = 0x3fffffffU, bit, port, mask;

	/* Each board: no held pin is reachable and every other pin is. */
	require(gpio_board_pins(1) == (all & ~pico),
	    "the Pico's free pins are not the complement of its held pins");
	require(gpio_board_pins(2) == (all & ~calc),
	    "the PicoCalc's free pins are not the complement of its held pins");
	require(gpio_board_pins(2) == 1U << 28,
	    "the PicoCalc leaves a pin other than GP28");
	require((gpio_board_pins(2) & (1U << 8 | 1U << 9)) == 0,
	    "the PicoCalc offers the keyboard controller's UART pins");
	require(gpio_board_pins(0) == 0 && gpio_board_pins(3) == 0,
	    "an unknown board has reachable pins");

	/* The two ports tile GP0-GP29 with no overlap and no pin above. */
	require(gpio_port_bits(0, ~0U) == 0x0000ffffU, "port A is not GP0-GP15");
	require(gpio_port_bits(1, ~0U) == 0x3fff0000U, "port B is not GP16-GP29");
	require(gpio_port_bits(2, ~0U) == 0 && gpio_port_bits(255, ~0U) == 0,
	    "a port past B names pins");
	for (bit = 0; bit < 16; bit++) {
		require(gpio_port_bits(0, 1U << bit) == 1U << bit,
		    "port A bit n is not GPn");
		require(gpio_port_bits(1, 1U << bit) ==
		    (bit < 14 ? 1U << (bit + 16) : 0),
		    "port B bit n is not GP(16+n), or reaches past GP29");
	}
	for (port = 0; port < 2; port++)
		for (mask = 0; mask < 0x10000; mask += 0x101)
			require(gpio_port_value(port, gpio_port_bits(port,
			    mask)) == (port == 0 ? mask : mask & 0x3fff),
			    "a port value does not read back its own bits");

	/* Requests: the nine numbers of the PIC32 header, and what is refused. */
	require(GPIO_CONFIN == 0x20016700U && GPIO_CONFOUT == 0x20026700U &&
	    GPIO_CONFOD == 0x20046700U && GPIO_DECONF == 0x20086700U &&
	    GPIO_STORE == 0x20106700U && GPIO_SET == 0x20206700U &&
	    GPIO_CLEAR == 0x20406700U && GPIO_INVERT == 0x20806700U &&
	    GPIO_POLL == 0x21006700U, "a request number left the PIC32 values");
	require(gpio_request_valid(GPIO_CONFIN) &&
	    gpio_request_valid(GPIO_CONFOUT) && gpio_request_valid(GPIO_DECONF) &&
	    gpio_request_valid(GPIO_STORE) && gpio_request_valid(GPIO_SET) &&
	    gpio_request_valid(GPIO_CLEAR) && gpio_request_valid(GPIO_INVERT) &&
	    gpio_request_valid(GPIO_POLL) && gpio_request_valid(GPIO_PWM),
	    "a single request is refused");
	require(gpio_request_valid(GPIO_CONFOUT | GPIO_SET) &&
	    gpio_request_valid(GPIO_STORE | GPIO_POLL),
	    "combined requests are refused");
	require(GPIO_PWM == 0x24006700U, "GPIO_PWM is not command bit 26");
	require(!gpio_request_valid(GPIO_PWM | GPIO_SET) &&
	    !gpio_request_valid(GPIO_PWM | GPIO_CONFOUT) &&
	    !gpio_request_valid(GPIO_PWM | GPIO_POLL) &&
	    !gpio_request_valid(GPIO_PWM | GPIO_DECONF),
	    "PWM beside another command is accepted");
	require(!gpio_request_valid(GPIO_CONFOD), "open drain is accepted");
	require(!gpio_request_valid(GPIO_CONFOUT | GPIO_CONFOD),
	    "open drain beside output is accepted");
	require(!gpio_request_valid(GPIO_CONFIN | GPIO_CONFOUT),
	    "input and output at once is accepted");
	require(!gpio_request_valid(GPIO_IOC_VOID | 'g' << 8),
	    "a request with no command is accepted");
	require(!gpio_request_valid(GPIO_REQUEST(25)) &&
	    !gpio_request_valid(GPIO_REQUEST(28)),
	    "a command bit past GPIO_POLL is accepted");
	require(!gpio_request_valid(GPIO_SET ^ ('g' << 8) ^ ('t' << 8)),
	    "another ioctl group is accepted");
	require(!gpio_request_valid(GPIO_SET | 0x40000000U) &&
	    !gpio_request_valid(GPIO_SET | 0x80000000U) &&
	    !gpio_request_valid(GPIO_SET & ~GPIO_IOC_VOID),
	    "a request with a parameter direction is accepted");
	require(!gpio_request_valid(GPIO_SET | 1),
	    "a request with its port number left in is accepted");
	pwm_checks();
	puts("gpio_requests: PASS (pins, ports, requests and PWM)");
	return 0;
}
