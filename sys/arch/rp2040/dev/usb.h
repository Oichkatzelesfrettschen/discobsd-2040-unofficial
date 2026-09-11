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

#ifdef KERNEL
struct tty;
struct uio;

void	usbinit(void);
void	usbintr(void);
int	usbopen(dev_t dev, int flag, int mode);
int	usbclose(dev_t dev, int flag, int mode);
int	usbread(dev_t dev, struct uio *uio, int flag);
int	usbwrite(dev_t dev, struct uio *uio, int flag);
int	usbioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
int	usbselect(dev_t dev, int rw);
void	usbputc(dev_t dev, char c);
char	usbgetc(dev_t dev);

extern struct tty usbttys[];
#endif	/* KERNEL */

#endif	/* !_RP2040_DEV_USB_H_ */
