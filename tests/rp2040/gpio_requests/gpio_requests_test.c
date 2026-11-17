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
	0, 1, 2, 3, 4, 5, 20, 21, 6, 7, 10, 11, 12, 13, 14, 15,
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
	require(gpio_board_pins(2) == (1U << 8 | 1U << 9 | 1U << 28),
	    "the PicoCalc leaves other pins than GP8, GP9 and GP28");
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
	    gpio_request_valid(GPIO_POLL), "a single request is refused");
	require(gpio_request_valid(GPIO_CONFOUT | GPIO_SET) &&
	    gpio_request_valid(GPIO_STORE | GPIO_POLL),
	    "combined requests are refused");
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
	puts("gpio_requests: PASS (pins, ports and requests)");
	return 0;
}
