#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <usb_reset.h>

_Static_assert(sizeof(unsigned int) * CHAR_BIT >= 30,
    "host unsigned int must hold the RP2040 GPIO mask");

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "usb_reset: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

int
main(void)
{
	unsigned int mask, pin;

	mask = 0;
	require(usb_reset_gpio_mask(127U << 9, &mask),
	    "disabled activity LED accepted");
	require(mask == 0, "disabled activity LED produced a nonzero mask");

	for (pin = 0; pin < 128; pin++) {
		unsigned int value = 0x100U | (pin << 9);

		mask = 0x5a5a5a5aU;
		if (pin < USB_RESET_GPIO_COUNT) {
			require(usb_reset_gpio_mask(value, &mask),
			    "valid GPIO selector was rejected");
			require(mask == (1U << pin),
			    "valid GPIO selector produced the wrong mask");
		} else {
			require(!usb_reset_gpio_mask(value, &mask),
			    "invalid GPIO selector was accepted");
			require(mask == 0x5a5a5a5aU,
			    "invalid GPIO selector modified the output mask");
		}
	}
	puts("usb_reset: PASS (all encoded pins and disabled activity LED)");
	return 0;
}
