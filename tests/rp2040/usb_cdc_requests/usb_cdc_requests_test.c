#include <stdio.h>
#include <stdlib.h>

#include <usb.h>

struct cdc_request_case {
	unsigned int request_type;
	unsigned int request;
	unsigned int value;
	unsigned int index;
	unsigned int length;
	unsigned int maximum_value;
};

static const struct cdc_request_case request_cases[] = {
	{ 0x21U, USB_CDC_REQ_SET_LINE_CODING, 0U,
	    USB_CDC_COMM_INTERFACE, USB_CDC_LINE_CODING_SIZE, 0U },
	{ 0xa1U, USB_CDC_REQ_GET_LINE_CODING, 0U,
	    USB_CDC_COMM_INTERFACE, USB_CDC_LINE_CODING_SIZE, 0U },
	{ 0x21U, USB_CDC_REQ_SET_CONTROL_LINE_STATE, 0x0003U,
	    USB_CDC_COMM_INTERFACE, 0U, 0x0003U },
	{ 0x21U, USB_CDC_REQ_SEND_BREAK, 0xffffU,
	    USB_CDC_COMM_INTERFACE, 0U, 0xffffU }
};

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "usb_cdc_requests: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

static int
expected_value(const struct cdc_request_case *request_case,
    unsigned int value)
{
	return value <= request_case->maximum_value;
}

static int
expected_request(const struct cdc_request_case *request_case,
    unsigned int request)
{
	if (request_case->index != USB_CDC_COMM_INTERFACE)
		return 0;
	switch (request) {
	case USB_CDC_REQ_SET_LINE_CODING:
		return request_case->request_type == 0x21U &&
		    request_case->value == 0U &&
		    request_case->length == USB_CDC_LINE_CODING_SIZE;
	case USB_CDC_REQ_GET_LINE_CODING:
		return request_case->request_type == 0xa1U &&
		    request_case->value == 0U &&
		    request_case->length == USB_CDC_LINE_CODING_SIZE;
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		return request_case->request_type == 0x21U &&
		    request_case->value <= 0x0003U && request_case->length == 0U;
	case USB_CDC_REQ_SEND_BREAK:
		return request_case->request_type == 0x21U &&
		    request_case->length == 0U;
	default:
		return 0;
	}
}

static void
check_request_type(const struct cdc_request_case *request_case)
{
	unsigned int request_type;

	for (request_type = 0; request_type <= 0xffU; request_type++)
		require(usb_cdc_request_valid(request_type,
		    request_case->request, request_case->value,
		    request_case->index, request_case->length) ==
		    (request_type == request_case->request_type),
		    "bmRequestType must match the direction, class and interface recipient");
}

static void
check_request_code(const struct cdc_request_case *request_case)
{
	unsigned int request;

	for (request = 0; request <= 0xffU; request++) {
		if (usb_cdc_request_valid(request_case->request_type, request,
		    request_case->value, request_case->index,
		    request_case->length) !=
		    expected_request(request_case, request)) {
			fprintf(stderr,
			    "usb_cdc_requests: bmRequestType 0x%x request 0x%x expected %d\n",
			    request_case->request_type, request,
			    expected_request(request_case, request));
			exit(EXIT_FAILURE);
		}
	}
}

static void
check_value(const struct cdc_request_case *request_case)
{
	unsigned int value;

	for (value = 0; value <= 0xffffU; value++)
		require(usb_cdc_request_valid(request_case->request_type,
		    request_case->request, value, request_case->index,
		    request_case->length) == expected_value(request_case, value),
		    "wValue must match the CDC request's allowed fields");
	require(!usb_cdc_request_valid(request_case->request_type,
	    request_case->request, 0x10000U, request_case->index,
	    request_case->length), "wValue must fit the USB setup field");
}

static void
check_index(const struct cdc_request_case *request_case)
{
	unsigned int index;

	for (index = 0; index <= 0xffffU; index++)
		require(usb_cdc_request_valid(request_case->request_type,
		    request_case->request, request_case->value, index,
		    request_case->length) ==
		    (index == USB_CDC_COMM_INTERFACE),
		    "wIndex must name the CDC communications interface");
	require(!usb_cdc_request_valid(request_case->request_type,
	    request_case->request, request_case->value, 0x10000U,
	    request_case->length), "wIndex must fit the USB setup field");
}

static void
check_length(const struct cdc_request_case *request_case)
{
	unsigned int length;

	for (length = 0; length <= 0xffffU; length++)
		require(usb_cdc_request_valid(request_case->request_type,
		    request_case->request, request_case->value,
		    request_case->index, length) ==
		    (length == request_case->length),
		    "wLength must match the CDC request's transfer contract");
	require(!usb_cdc_request_valid(request_case->request_type,
	    request_case->request, request_case->value, request_case->index,
	    0x10000U), "wLength must fit the USB setup field");
}

int
main(void)
{
	size_t index;

	for (index = 0; index < sizeof(request_cases) / sizeof(request_cases[0]);
	    index++) {
		check_request_type(&request_cases[index]);
		check_request_code(&request_cases[index]);
		check_value(&request_cases[index]);
		check_index(&request_cases[index]);
		check_length(&request_cases[index]);
	}
	require(!usb_cdc_request_valid(0x21U, 0x24U, 0U,
	    USB_CDC_COMM_INTERFACE, 0U), "unsupported CDC requests must stall");
	puts("usb_cdc_requests: PASS (all request types, codes and 16-bit fields)");
	return EXIT_SUCCESS;
}
