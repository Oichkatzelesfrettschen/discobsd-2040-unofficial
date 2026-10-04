#include <stdio.h>
#include <stdlib.h>

#include <usb.h>

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "usb_set_address_interface: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

static void
check_set_address(void)
{
	unsigned int index, length, request_type, value;

	for (request_type = 0; request_type <= 0xffU; request_type++)
		require(usb_set_address_request_valid(request_type, 1U, 0U,
		    0U) == (request_type == 0U),
		    "SET_ADDRESS requires host-to-device standard device type");

	for (value = 0; value <= 0xffffU; value++)
		require(usb_set_address_request_valid(0U, value, 0U, 0U) ==
		    (value <= 0x7fU), "SET_ADDRESS value must fit seven bits");

	for (index = 0; index <= 0xffffU; index++)
		require(usb_set_address_request_valid(0U, 1U, index, 0U) ==
		    (index == 0U), "SET_ADDRESS index must be zero");

	for (length = 0; length <= 0xffffU; length++)
		require(usb_set_address_request_valid(0U, 1U, 0U, length) ==
		    (length == 0U), "SET_ADDRESS length must be zero");
}

static void
check_set_interface(void)
{
	unsigned int index, length, request_type, value;

	for (request_type = 0; request_type <= 0xffU; request_type++)
		require(usb_set_interface_request_valid(request_type, 0U, 0U,
		    0U) == (request_type == 1U),
		    "SET_INTERFACE requires host-to-device standard interface type");

	for (value = 0; value <= 0xffffU; value++)
		require(usb_set_interface_request_valid(1U, value, 0U, 0U) ==
		    (value == 0U), "only alternate setting zero is supported");

	for (index = 0; index <= 0xffffU; index++)
		require(usb_set_interface_request_valid(1U, 0U, index, 0U) ==
		    (index < USB_INTERFACE_COUNT),
		    "SET_INTERFACE requires an existing interface number");

	for (length = 0; length <= 0xffffU; length++)
		require(usb_set_interface_request_valid(1U, 0U, 0U, length) ==
		    (length == 0U), "SET_INTERFACE length must be zero");
}

int
main(void)
{
	check_set_address();
	check_set_interface();
	puts("usb_set_address_interface: PASS (all request types and 16-bit fields)");
	return 0;
}
