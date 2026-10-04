#include <stdio.h>
#include <stdlib.h>

#include <usb.h>

#define	REQ_GET_STATUS		0U
#define	REQ_CLEAR_FEATURE	1U
#define	REQ_SET_FEATURE		3U

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "usb_standard_requests: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

static int
status_endpoint_expected(unsigned int index)
{
	return index == 0U || index == 0x80U || index == 0x81U ||
	    index == 0x02U || index == 0x82U;
}

static int
feature_endpoint_expected(unsigned int index)
{
	return index == 0x81U || index == 0x02U || index == 0x82U;
}

static void
test_configuration_and_interface(void)
{
	unsigned int field, request_type;

	for (request_type = 0U; request_type <= 0xffU; request_type++) {
		require(usb_get_configuration_request_valid(request_type,
		    0U, 0U, 1U) == (request_type == 0x80U),
		    "GET_CONFIGURATION requires device-to-host device type");
		require(usb_get_interface_request_valid(request_type,
		    0U, 0U, 1U) == (request_type == 0x81U),
		    "GET_INTERFACE requires device-to-host interface type");
	}
	for (field = 0U; field <= 0xffffU; field++) {
		require(usb_get_configuration_request_valid(0x80U,
		    field, 0U, 1U) == (field == 0U),
		    "GET_CONFIGURATION wValue must be zero");
		require(usb_get_configuration_request_valid(0x80U,
		    0U, field, 1U) == (field == 0U),
		    "GET_CONFIGURATION wIndex must be zero");
		require(usb_get_configuration_request_valid(0x80U,
		    0U, 0U, field) == (field == 1U),
		    "GET_CONFIGURATION wLength must be one");
		require(usb_get_interface_request_valid(0x81U,
		    field, 0U, 1U) == (field == 0U),
		    "GET_INTERFACE wValue must be zero");
		require(usb_get_interface_request_valid(0x81U,
		    0U, field, 1U) == (field < USB_INTERFACE_COUNT),
		    "GET_INTERFACE wIndex must name an advertised interface");
		require(usb_get_interface_request_valid(0x81U,
		    0U, 0U, field) == (field == 1U),
		    "GET_INTERFACE wLength must be one");
	}
}

static void
test_get_status(void)
{
	unsigned int field, request_type;

	for (request_type = 0U; request_type <= 0xffU; request_type++) {
		require(usb_get_status_request_valid(request_type, 0U, 0U, 2U) ==
		    (request_type == 0x80U || request_type == 0x81U ||
		    request_type == 0x82U),
		    "GET_STATUS accepts only exact device, interface or endpoint type");
	}
	for (field = 0U; field <= 0xffffU; field++) {
		require(usb_get_status_request_valid(0x80U, field, 0U, 2U) ==
		    (field == 0U), "device GET_STATUS wValue must be zero");
		require(usb_get_status_request_valid(0x80U, 0U, field, 2U) ==
		    (field == 0U), "device GET_STATUS wIndex must be zero");
		require(usb_get_status_request_valid(0x80U, 0U, 0U, field) ==
		    (field == 2U), "GET_STATUS wLength must be two");
		require(usb_get_status_request_valid(0x81U, 0U, field, 2U) ==
		    (field < USB_INTERFACE_COUNT),
		    "interface GET_STATUS must name an advertised interface");
		require(usb_get_status_request_valid(0x82U, 0U, field, 2U) ==
		    (field <= 0xffU && status_endpoint_expected(field)),
		    "endpoint GET_STATUS must name an advertised endpoint address");
	}
	for (request_type = 0U; request_type <= 0xffU; request_type++) {
		require(usb_get_status_request_valid(request_type, 0U, 1U, 2U) ==
		    (request_type == 0x81U),
		    "interface GET_STATUS requires device-to-host interface type");
		require(usb_get_status_request_valid(request_type, 0U, 0x82U, 2U) ==
		    (request_type == 0x82U),
		    "endpoint GET_STATUS requires device-to-host endpoint type");
	}
	require(!usb_get_status_request_valid(0x80U, 0x10000U, 0U, 2U),
	    "GET_STATUS rejects values outside setup-field width");
	require(!usb_get_status_request_valid(0x80U, 0U, 0x10000U, 2U),
	    "GET_STATUS rejects indexes outside setup-field width");
	require(!usb_get_status_request_valid(0x80U, 0U, 0U, 0x10000U),
	    "GET_STATUS rejects lengths outside setup-field width");
}

static void
test_endpoint_feature_requests(void)
{
	unsigned int field, request, request_type;

	for (request_type = 0U; request_type <= 0xffU; request_type++)
		require(usb_endpoint_feature_request_valid(request_type,
		    REQ_CLEAR_FEATURE, 0U, 0x82U, 0U) ==
		    (request_type == 0x02U),
		    "endpoint feature requests require exact host-to-device type");
	for (request = 0U; request <= 0xffU; request++)
		require(usb_endpoint_feature_request_valid(0x02U, request,
		    0U, 0x82U, 0U) ==
		    (request == REQ_CLEAR_FEATURE || request == REQ_SET_FEATURE),
		    "only CLEAR_FEATURE and SET_FEATURE request codes are valid");
	for (field = 0U; field <= 0xffffU; field++) {
		require(usb_endpoint_feature_request_valid(0x02U,
		    REQ_CLEAR_FEATURE, field, 0x82U, 0U) == (field == 0U),
		    "endpoint feature selector must be ENDPOINT_HALT");
		require(usb_endpoint_feature_request_valid(0x02U,
		    REQ_CLEAR_FEATURE, 0U, field, 0U) ==
		    (field <= 0xffU && feature_endpoint_expected(field)),
		    "endpoint feature index must name a non-control endpoint");
		require(usb_endpoint_feature_request_valid(0x02U,
		    REQ_CLEAR_FEATURE, 0U, 0x82U, field) == (field == 0U),
		    "endpoint feature request wLength must be zero");
	}
	require(usb_endpoint_feature_request_valid(0x02U, REQ_SET_FEATURE,
	    0U, 0x02U, 0U), "SET_FEATURE geometry accepts bulk OUT halt selector");
	require(!usb_endpoint_feature_request_valid(0x02U, REQ_SET_FEATURE,
	    1U, 0x02U, 0U), "unsupported endpoint feature selectors are rejected");
	require(!usb_endpoint_feature_request_valid(0x00U, REQ_SET_FEATURE,
	    1U, 0U, 0U), "unadvertised remote-wakeup feature is rejected");
	require(!usb_endpoint_feature_request_valid(0x00U, REQ_SET_FEATURE,
	    2U, 0x0200U, 0U), "unsupported device test mode is rejected");
	require(!usb_endpoint_feature_request_valid(0x02U, REQ_SET_FEATURE,
	    0U, 0x102U, 0U), "endpoint feature rejects reserved wIndex byte");
	require(!usb_endpoint_feature_request_valid(0x02U, REQ_CLEAR_FEATURE,
	    0U, 0x82U, 0x10000U),
	    "endpoint feature rejects lengths outside setup-field width");
}

int
main(void)
{
	test_configuration_and_interface();
	test_get_status();
	test_endpoint_feature_requests();
	puts("usb_standard_requests: PASS (exhaustive standard request fields)");
	return EXIT_SUCCESS;
}
