#include <stdio.h>
#include <stdlib.h>

#include <usb.h>

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "usb_get_descriptor: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

static int
expected_descriptor(unsigned int value, unsigned int index)
{
	unsigned int descriptor_index, descriptor_type;

	descriptor_type = value >> 8;
	descriptor_index = value & 0xffU;
	switch (descriptor_type) {
	case USB_DESC_DEVICE:
	case USB_DESC_CONFIGURATION:
	case USB_DESC_BOS:
		return descriptor_index == 0U && index == 0U;
	case USB_DESC_STRING:
		return descriptor_index < USB_STRING_DESCRIPTOR_COUNT &&
		    (index == 0U || index == USB_STRING_LANGID_EN_US);
	default:
		return 0;
	}
}

static void
check_request_type(void)
{
	unsigned int request_type;

	for (request_type = 0; request_type <= 0xffU; request_type++)
		require(usb_get_descriptor_request_valid(request_type, 0x0301U,
		    USB_STRING_LANGID_EN_US) == (request_type == 0x80U),
		    "GET_DESCRIPTOR requires device-recipient IN standard geometry");
}

static void
check_value_field(void)
{
	unsigned int value;

	for (value = 0; value <= 0xffffU; value++)
		require(usb_get_descriptor_request_valid(0x80U, value, 0U) ==
		    expected_descriptor(value, 0U),
		    "wValue must select an implemented descriptor and index");
}

static void
check_index_field(void)
{
	unsigned int index;

	for (index = 0; index <= 0xffffU; index++) {
		require(usb_get_descriptor_request_valid(0x80U,
		    USB_DESC_DEVICE << 8, index) == (index == 0U),
		    "non-string descriptors require wIndex zero");
		require(usb_get_descriptor_request_valid(0x80U,
		    (USB_DESC_STRING << 8) | 1U, index) ==
		    (index == 0U || index == USB_STRING_LANGID_EN_US),
		    "string descriptors require zero or a supported LANGID");
	}
}

int
main(void)
{
	check_request_type();
	check_value_field();
	check_index_field();
	puts("usb_get_descriptor: PASS (all request types and 16-bit fields)");
	return 0;
}
