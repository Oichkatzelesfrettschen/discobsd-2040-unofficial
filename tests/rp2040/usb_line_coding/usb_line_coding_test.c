#include <stdio.h>
#include <stdlib.h>

#include <usb.h>

_Static_assert(USB_CDC_LINE_CODING_SIZE == 7U,
    "CDC line coding payload must contain seven bytes");

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "usb_line_coding: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

int
main(void)
{
	unsigned int length;

	for (length = 0; length <= USB_BUF_CTRL_LEN_MASK; length++)
		require(usb_cdc_line_coding_length_valid(length) ==
		    (length == USB_CDC_LINE_CODING_SIZE),
		    "length predicate disagrees with the exact seven-byte contract");
	puts("usb_line_coding: PASS (all buffer-control lengths)");
	return 0;
}
