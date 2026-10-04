/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _RP2040_DEV_USB_RESET_H_
#define _RP2040_DEV_USB_RESET_H_

#define USB_RESET_INTERFACE 2U
#define USB_RESET_REQUEST_BOOTSEL 0x01U
#define USB_RESET_REQUEST_FLASH 0x02U
#define USB_RESET_REQUEST_TYPE 0x21U
#define USB_RESET_INTERFACE_DISABLE_MASK 0x0003U
#define USB_RESET_BOOTSEL_VALUE_MASK 0xff83U
#define USB_RESET_GPIO_COUNT 30U

/* Reject reserved reset selectors before dispatch can reach the ROM. */
static inline int
usb_reset_bootsel_value_valid(unsigned int value)
{
	if (value > 0xffffU || (value & ~USB_RESET_BOOTSEL_VALUE_MASK) != 0U)
		return 0;
	if ((value & 0x0100U) == 0U &&
	    (value & (0x0080U | 0xfe00U)) != 0U)
		return 0;
	if ((value & 0x0100U) != 0U &&
	    (value >> 9) >= USB_RESET_GPIO_COUNT)
		return 0;
	return 1;
}

/* picotool sends reset requests after configuring interface 2. */
static inline int
usb_reset_request_valid(unsigned int request_type, unsigned int request,
	unsigned int value, unsigned int index, unsigned int length,
	int configured)
{
	if (!configured || request_type != USB_RESET_REQUEST_TYPE ||
	    request > 0xffU || index != USB_RESET_INTERFACE || length != 0U)
		return 0;
	switch (request) {
	case USB_RESET_REQUEST_BOOTSEL:
		return usb_reset_bootsel_value_valid(value);
	case USB_RESET_REQUEST_FLASH:
		return value == 0U;
	default:
		return 0;
	}
}

/* Convert the SDK reset request's activity-pin selector without shifting
 * outside the RP2040 GPIO mask. */
static inline int
usb_reset_gpio_mask(unsigned int value, unsigned int *mask)
{
	unsigned int pin;

	if ((value & 0x100U) == 0) {
		*mask = 0;
		return 1;
	}
	pin = value >> 9;
	if (pin >= USB_RESET_GPIO_COUNT)
		return 0;
	*mask = 1U << pin;
	return 1;
}

#endif	/* !_RP2040_DEV_USB_RESET_H_ */
