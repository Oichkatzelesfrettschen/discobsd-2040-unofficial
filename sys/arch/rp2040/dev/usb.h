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

#ifndef	_RP2040_DEV_USB_H_
#define	_RP2040_DEV_USB_H_

/*
 * RP2040 USB device controller, datasheet section 4.1. Register offsets are
 * from 4.1.4 and the DPSRAM layout from table 394; the controller runs from
 * clk_usb at 48 MHz, which machdep.c derives from the USB PLL.
 */

#define	USB_REGS_BASE		0x50110000UL
#define	USB_DPRAM_BASE		0x50100000UL
#define	USB_DPRAM_SIZE		4096UL

/* Atomic register aliases, the same convention as every RP2040 block. */
#define	USB_REG_SET		0x2000UL
#define	USB_REG_CLR		0x3000UL

#define	USB_ADDR_ENDP		0x00
#define	USB_MAIN_CTRL		0x40
#define	USB_SOF_RD		0x48
#define	USB_SIE_CTRL		0x4c
#define	USB_SIE_STATUS		0x50
#define	USB_BUFF_STATUS		0x58
#define	USB_EP_STALL_ARM	0x68
#define	USB_MUXING		0x74
#define	USB_PWR			0x78
#define	USB_INTE		0x90
#define	USB_INTS		0x98

#define	USB_MAIN_CTRL_CONTROLLER_EN	(1UL << 0)
#define	USB_SIE_CTRL_EP0_INT_1BUF	(1UL << 29)
#define	USB_SIE_CTRL_PULLUP_EN		(1UL << 16)
#define	USB_SIE_STATUS_SETUP_REC	(1UL << 17)
#define	USB_SIE_STATUS_BUS_RESET	(1UL << 19)
#define	USB_INTS_BUFF_STATUS		(1UL << 4)
#define	USB_INTS_BUS_RESET		(1UL << 12)
#define	USB_INTS_SETUP_REQ		(1UL << 16)
#define	USB_INTS_DEV_SOF		(1UL << 17)
#define	USB_MUXING_TO_PHY		(1UL << 0)
#define	USB_MUXING_SOFTCON		(1UL << 3)
#define	USB_PWR_VBUS_DETECT		(1UL << 2)
#define	USB_PWR_VBUS_DETECT_OVERRIDE_EN	(1UL << 3)
#define	USB_EP_STALL_ARM_EP0_IN		(1UL << 0)
#define	USB_EP_STALL_ARM_EP0_OUT	(1UL << 1)

/*
 * DPSRAM: the setup packet, then an endpoint control word per endpoint and
 * direction from EP1 up, then a buffer control word per endpoint and
 * direction from EP0 up, then the EP0 buffer, then the other buffers.
 */
#define	USB_DPRAM_SETUP		0x00
#define	USB_DPRAM_EP_CTRL(ep, in)	(0x00 + (ep) * 8 + ((in) ? 0 : 4))
#define	USB_DPRAM_BUF_CTRL(ep, in)	(0x80 + (ep) * 8 + ((in) ? 0 : 4))
#define	USB_DPRAM_EP0_BUF	0x100
#define	USB_DPRAM_BUFFERS	0x180

/* Endpoint control word, table 395. */
#define	USB_EP_CTRL_ENABLE		(1UL << 31)
#define	USB_EP_CTRL_INT_PER_BUF		(1UL << 29)
#define	USB_EP_CTRL_TYPE_CONTROL	(0UL << 26)
#define	USB_EP_CTRL_TYPE_ISO		(1UL << 26)
#define	USB_EP_CTRL_TYPE_BULK		(2UL << 26)
#define	USB_EP_CTRL_TYPE_INTERRUPT	(3UL << 26)

/* Buffer control word, buffer 0 half, table 396. */
#define	USB_BUF_CTRL_FULL		(1UL << 15)
#define	USB_BUF_CTRL_LAST		(1UL << 14)
#define	USB_BUF_CTRL_DATA1		(1UL << 13)
#define	USB_BUF_CTRL_SEL		(1UL << 12)
#define	USB_BUF_CTRL_STALL		(1UL << 11)
#define	USB_BUF_CTRL_AVAIL		(1UL << 10)
#define	USB_BUF_CTRL_LEN_MASK		0x3ffUL

/* BUFF_STATUS: one bit per endpoint and direction, IN even, OUT odd. */
#define	USB_BUFF_STATUS_BIT(ep, in)	(1UL << ((ep) * 2 + ((in) ? 0 : 1)))

#define	USB_PACKET_MAX		64		/* Full speed bulk and control. */
#define	USB_CONFIGURATION_VALUE	1U
#define	USB_INTERFACE_COUNT	3U
#define	USB_DESC_DEVICE		1U
#define	USB_DESC_CONFIGURATION	2U
#define	USB_DESC_STRING		3U
#define	USB_DESC_BOS		15U
#define	USB_STRING_DESCRIPTOR_COUNT	6U
#define	USB_STRING_LANGID_EN_US	0x0409U
#define	USB_CDC_COMM_INTERFACE	0U
#define	USB_CDC_LINE_CODING_SIZE	7U
#define	USB_CDC_REQ_SET_LINE_CODING	0x20U
#define	USB_CDC_REQ_GET_LINE_CODING	0x21U
#define	USB_CDC_REQ_SET_CONTROL_LINE_STATE	0x22U
#define	USB_CDC_REQ_SEND_BREAK	0x23U

/* USB 2.0 9.4.3 permits a caller-selected descriptor transfer length. */
static inline int
usb_get_descriptor_request_valid(unsigned int request_type,
    unsigned int value, unsigned int index)
{
	unsigned int descriptor_index, descriptor_type;

	if (request_type != 0x80U)
		return 0;
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

static inline int
usb_set_address_request_valid(unsigned int request_type,
    unsigned int value, unsigned int index, unsigned int length)
{
	return request_type == 0U && value <= 0x7fU &&
	    index == 0U && length == 0U;
}

static inline int
usb_set_configuration_request_valid(unsigned int request_type,
    unsigned int value, unsigned int index, unsigned int length)
{
	return request_type == 0U &&
	    (value == 0U || value == USB_CONFIGURATION_VALUE) &&
	    index == 0U && length == 0U;
}

static inline int
usb_set_interface_request_valid(unsigned int request_type,
    unsigned int value, unsigned int interface_number, unsigned int length)
{
	return request_type == 1U && value == 0U &&
	    interface_number < USB_INTERFACE_COUNT && length == 0U;
}

static inline int
usb_get_configuration_request_valid(unsigned int request_type,
	unsigned int value, unsigned int index, unsigned int length)
{
	return request_type == 0x80U && value == 0U && index == 0U &&
	    length == 1U;
}

static inline int
usb_get_interface_request_valid(unsigned int request_type,
	unsigned int value, unsigned int interface_number, unsigned int length)
{
	return request_type == 0x81U && value == 0U &&
	    interface_number < USB_INTERFACE_COUNT && length == 1U;
}

static inline int
usb_status_endpoint_valid(unsigned int endpoint_address)
{
	return endpoint_address == 0x00U || endpoint_address == 0x80U ||
	    endpoint_address == 0x81U || endpoint_address == 0x02U ||
	    endpoint_address == 0x82U;
}

/* USB 2.0 9.4.5 defines GET_STATUS for device, interface and endpoint. */
static inline int
usb_get_status_request_valid(unsigned int request_type,
    unsigned int value, unsigned int index, unsigned int length)
{
	if (request_type > 0xffU || value > 0xffffU || index > 0xffffU ||
	    length > 0xffffU || value != 0U || length != 2U)
		return 0;
	switch (request_type) {
	case 0x80U:
		return index == 0U;
	case 0x81U:
		return index < USB_INTERFACE_COUNT;
	case 0x82U:
		return index <= 0xffU && usb_status_endpoint_valid(index);
	default:
		return 0;
	}
}

/* EP0 remains available before configuration; other recipients do not. */
static inline int
usb_get_status_request_allowed(unsigned int request_type,
	unsigned int value, unsigned int index, unsigned int length,
	int configured)
{
	if (!usb_get_status_request_valid(request_type, value, index, length))
		return 0;
	if (request_type == 0x80U || (request_type == 0x82U &&
	    (index == 0x00U || index == 0x80U)))
		return 1;
	return configured != 0;
}

/* Only endpoint halt selectors for declared non-control endpoints are valid. */
static inline int
usb_endpoint_feature_request_valid(unsigned int request_type,
	unsigned int request, unsigned int value, unsigned int index,
	unsigned int length)
{
	if (request_type > 0xffU || value > 0xffffU || index > 0xffffU ||
	    length > 0xffffU || request_type != 0x02U ||
	    (request != 1U && request != 3U) || value != 0U ||
	    index > 0xffU || length != 0U)
		return 0;
	return index == 0x81U || index == 0x02U || index == 0x82U;
}

static inline int
usb_cdc_line_coding_length_valid(unsigned int received_length)
{
	return received_length == USB_CDC_LINE_CODING_SIZE;
}

/* CDC PSTN 1.2 sections 6.3.10-6.3.13 define these setup-field layouts. */
static inline int
usb_cdc_request_valid(unsigned int request_type, unsigned int request,
    unsigned int value, unsigned int index, unsigned int length)
{
	if (request_type > 0xffU || value > 0xffffU || index > 0xffffU ||
	    length > 0xffffU || index != USB_CDC_COMM_INTERFACE)
		return 0;
	switch (request) {
	case USB_CDC_REQ_SET_LINE_CODING:
		return request_type == 0x21U && value == 0U &&
		    length == USB_CDC_LINE_CODING_SIZE;
	case USB_CDC_REQ_GET_LINE_CODING:
		return request_type == 0xa1U && value == 0U &&
		    length == USB_CDC_LINE_CODING_SIZE;
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		return request_type == 0x21U && value <= 0x0003U &&
		    length == 0U;
	case USB_CDC_REQ_SEND_BREAK:
		return request_type == 0x21U && length == 0U;
	default:
		return 0;
	}
}

#ifdef KERNEL
struct tty;
struct uio;

void	usbinit(void);
void	usbintr(void);
void	usbdrain(void);
void	usbpoll(void);
int	usbopen(dev_t dev, int flag, int mode);
int	usbclose(dev_t dev, int flag, int mode);
int	usbread(dev_t dev, struct uio *uio, int flag);
int	usbwrite(dev_t dev, struct uio *uio, int flag);
int	usbioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
int	usbselect(dev_t dev, int rw);
void	usbputc(dev_t dev, char c);
char	usbgetc(dev_t dev);

extern struct tty usbttys[];

/*
 * RP2040-E15 accounting, readable as machdep.usb_e15_deferred and
 * machdep.usb_bulkin_arms.
 */
extern u_int usb_e15_deferred;
extern u_int usb_e15_bulkin_arms;

/*
 * Console robustness accounting, readable as machdep.usb_service_reentered
 * and machdep.usb_tx_recovered. The first counts services refused to a
 * caller that interrupted the driver, which only a fault can be; the second
 * counts bulk IN buffers released after a completion that never arrived.
 * Both read zero on a healthy boot.
 */
extern u_int usb_service_reentered;
extern u_int usb_tx_recovered;

void	usbabandon(void);
#endif	/* KERNEL */

#endif	/* !_RP2040_DEV_USB_H_ */
