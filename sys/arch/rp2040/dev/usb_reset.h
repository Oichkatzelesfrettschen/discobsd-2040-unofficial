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

#define USB_RESET_GPIO_COUNT 30U

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
