#include <stdio.h>
#include <stdlib.h>

#include <usb.h>

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "usb_set_configuration: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

int
main(void)
{
	unsigned int index, length, request_type, value;

	for (request_type = 0; request_type <= 0xffU; request_type++)
		require(usb_set_configuration_request_valid(request_type, 1U,
		    0U, 0U) == (request_type == 0U),
		    "request type must be host-to-device standard device");

	for (value = 0; value <= 0xffffU; value++)
		require(usb_set_configuration_request_valid(0U, value, 0U, 0U) ==
		    (value == 0U || value == USB_CONFIGURATION_VALUE),
		    "configuration value must match the descriptor or be zero");

	for (index = 0; index <= 0xffffU; index++)
		require(usb_set_configuration_request_valid(0U,
		    USB_CONFIGURATION_VALUE, index, 0U) == (index == 0U),
		    "configuration request index must be zero");

	for (length = 0; length <= 0xffffU; length++)
		require(usb_set_configuration_request_valid(0U,
		    USB_CONFIGURATION_VALUE, 0U, length) == (length == 0U),
		    "configuration request length must be zero");

	puts("usb_set_configuration: PASS (all request-type and 16-bit field values)");
	return 0;
}
