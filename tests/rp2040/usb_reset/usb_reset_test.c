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

static int
bootsel_value_expected(unsigned int value)
{
	if ((value & ~0xfe83U) != 0U)
		return 0;
	if ((value & 0x0100U) == 0U &&
	    (value & (0x0080U | 0xfe00U)) != 0U)
		return 0;
	return (value & 0x0100U) == 0U ||
	    (value >> 9) < USB_RESET_GPIO_COUNT;
}

static void
test_bootsel_value_geometry(void)
{
	unsigned int value;

	for (value = 0; value <= 0xffffU; value++)
		require(usb_reset_bootsel_value_valid(value) ==
		    bootsel_value_expected(value),
		    "BOOTSEL accepts only documented selector bits and GPIOs");
	require(!usb_reset_bootsel_value_valid(0x10000U),
	    "BOOTSEL rejects values wider than the setup field");
}

static void
test_reset_request_geometry(void)
{
	unsigned int field;

	for (field = 0; field <= 0xffU; field++) {
		require(usb_reset_request_valid(field,
		    USB_RESET_REQUEST_BOOTSEL, 0U, USB_RESET_INTERFACE,
		    0U, 1) == (field == USB_RESET_REQUEST_TYPE),
		    "reset requests require exact host-to-device class/interface type");
		require(usb_reset_request_valid(USB_RESET_REQUEST_TYPE, field,
		    0U, USB_RESET_INTERFACE, 0U, 1) ==
		    (field == USB_RESET_REQUEST_BOOTSEL ||
		    field == USB_RESET_REQUEST_FLASH),
		    "only BOOTSEL and flash reset request codes are supported");
	}
	for (field = 0; field <= 0xffffU; field++) {
		require(usb_reset_request_valid(USB_RESET_REQUEST_TYPE,
		    USB_RESET_REQUEST_BOOTSEL, 0U, field, 0U, 1) ==
		    (field == USB_RESET_INTERFACE),
		    "reset request wIndex must be the complete interface number");
		require(usb_reset_request_valid(USB_RESET_REQUEST_TYPE,
		    USB_RESET_REQUEST_BOOTSEL, 0U, USB_RESET_INTERFACE,
		    field, 1) == (field == 0U),
		    "reset request has no data stage");
		require(usb_reset_request_valid(USB_RESET_REQUEST_TYPE,
		    USB_RESET_REQUEST_FLASH, field, USB_RESET_INTERFACE,
		    0U, 1) == (field == 0U),
		    "flash reset requires wValue zero");
		require(usb_reset_request_valid(USB_RESET_REQUEST_TYPE,
		    USB_RESET_REQUEST_BOOTSEL, field, USB_RESET_INTERFACE,
		    0U, 1) == bootsel_value_expected(field),
		    "BOOTSEL applies the documented wValue grammar");
	}
	require(!usb_reset_request_valid(USB_RESET_REQUEST_TYPE,
	    USB_RESET_REQUEST_BOOTSEL, 0U, USB_RESET_INTERFACE, 0U, 0),
	    "reset interface requests require configured state");
	require(!usb_reset_request_valid(USB_RESET_REQUEST_TYPE,
	    USB_RESET_REQUEST_BOOTSEL, 0U, 0x10002U, 0U, 1),
	    "reset request rejects wIndex wider than the setup field");
	require(!usb_reset_request_valid(USB_RESET_REQUEST_TYPE,
	    USB_RESET_REQUEST_BOOTSEL, 0U, USB_RESET_INTERFACE, 0x10000U, 1),
	    "reset request rejects wLength wider than the setup field");
	require(!usb_reset_request_valid(USB_RESET_REQUEST_TYPE,
	    0x101U, 0U, USB_RESET_INTERFACE, 0U, 1),
	    "reset request rejects request codes wider than one byte");
}

int
main(void)
{
	unsigned int mask, pin;

	test_bootsel_value_geometry();
	test_reset_request_geometry();

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
	puts("usb_reset: PASS (request geometry and all encoded GPIO selectors)");
	return 0;
}
