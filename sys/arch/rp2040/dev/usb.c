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

/*
 * Console over USB: a CDC-ACM device on the RP2040's own controller.
 *
 * The Pico exposes one cable, so the console rides on it. The device shows
 * a CDC-ACM function, which Linux binds as /dev/ttyACM0 with no driver of
 * its own, and the vendor "reset" interface the Pico SDK defines, so
 * picotool reboot -u returns the board to BOOTSEL without a hand on the
 * button.
 *
 * The device controller is programmed as the datasheet describes it in
 * section 4.1.2: control and buffer words in DPSRAM, one buffer per
 * endpoint, an interrupt per completed buffer, and the setup packet at
 * DPSRAM offset 0. Three endpoints beyond EP0: an interrupt IN the CDC
 * class requires and which is never armed, a bulk OUT the host writes
 * keystrokes to, and a bulk IN the console writes through.
 *
 * Output goes through a ring rather than straight to the endpoint, because
 * the host only drains the IN endpoint while a terminal has the port open,
 * and the kernel prints long before anyone has. The ring keeps the last
 * USB_TXRING bytes, so a terminal opened after boot sees the boot messages
 * rather than nothing; 16K holds a whole boot with room to spare.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/config.h>

#include <machine/intr.h>
#include <machine/scb.h>

#include <rp2040/dev/usb.h>
#include <rp2040/dev/flash.h>

#define	REG32(a)	(*(volatile u_int *)(a))
#define	USBREG(off)	REG32(USB_REGS_BASE + (off))
#define	USBSET(off)	REG32(USB_REGS_BASE + USB_REG_SET + (off))
#define	USBCLR(off)	REG32(USB_REGS_BASE + USB_REG_CLR + (off))
#define	DPRAM32(off)	REG32(USB_DPRAM_BASE + (off))
#define	DPRAM8(off)	(*(volatile u_char *)(USB_DPRAM_BASE + (off)))

#define	RESETS_BASE	0x4000c000UL
#define	RESETS_CLR	0x3000UL
#define	RESETS_DONE	0x8
#define	RESETS_USBCTRL	(1UL << 24)
#define	USBCTRL_IRQ	5

#define	AIRCR_VECTKEY		0x05fa0000UL
#define	AIRCR_SYSRESETREQ	(1UL << 2)

/* Endpoint numbers and their DPSRAM buffers. */
#define	EP_NOTIFY	1			/* Interrupt IN, CDC requires it. */
#define	EP_DATA		2			/* Bulk OUT and bulk IN. */
#define	BUF_NOTIFY	(USB_DPRAM_BUFFERS + 0x00)
#define	BUF_DATA_OUT	(USB_DPRAM_BUFFERS + 0x40)
#define	BUF_DATA_IN	(USB_DPRAM_BUFFERS + 0x80)

/* Interface numbers in the configuration below. */
#define	ITF_CDC_COMM	0
#define	ITF_CDC_DATA	1
#define	ITF_RESET	2

/* Standard requests. */
#define	REQ_GET_STATUS		0
#define	REQ_CLEAR_FEATURE	1
#define	REQ_SET_FEATURE		3
#define	REQ_SET_ADDRESS		5
#define	REQ_GET_DESCRIPTOR	6
#define	REQ_GET_CONFIGURATION	8
#define	REQ_SET_CONFIGURATION	9
#define	REQ_GET_INTERFACE	10
#define	REQ_SET_INTERFACE	11

#define	DESC_DEVICE		1
#define	DESC_CONFIGURATION	2
#define	DESC_STRING		3

/* CDC PSTN class requests. */
#define	CDC_SET_LINE_CODING		0x20
#define	CDC_GET_LINE_CODING		0x21
#define	CDC_SET_CONTROL_LINE_STATE	0x22
#define	CDC_SEND_BREAK			0x23

/* The Pico SDK reset interface, pico/usb_reset_interface.h. */
#define	RESET_INTERFACE_SUBCLASS	0x00
#define	RESET_INTERFACE_PROTOCOL	0x01
#define	RESET_REQUEST_BOOTSEL		0x01
#define	RESET_REQUEST_FLASH		0x02

#define	ROM_RESET_USB_BOOT	ROM_CODE('U', 'B')

/*
 * Descriptors. The device is a composite with an interface association,
 * as the CDC spec asks of a device carrying more than one function; the
 * vendor and product identifiers are the ones Raspberry Pi assigns to Pico
 * CDC devices, which is what picotool looks for before it probes the reset
 * interface.
 */
static const u_char usb_device_desc[18] = {
	18, DESC_DEVICE,
	0x00, 0x02,			/* USB 2.0 */
	0xef, 0x02, 0x01,		/* Miscellaneous, common, IAD. */
	USB_PACKET_MAX,
	0x8a, 0x2e,			/* Raspberry Pi */
	0x0a, 0x00,			/* Pico SDK CDC, RP2040 */
	0x00, 0x01,			/* Device release 1.0 */
	1, 2, 3,			/* Manufacturer, product, serial. */
	1,
};

#define	CONFIG_DESC_LEN	(9 + 8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7 + 9)

static const u_char usb_config_desc[CONFIG_DESC_LEN] = {
	/* Configuration: three interfaces, bus powered, 250 mA. */
	9, DESC_CONFIGURATION, CONFIG_DESC_LEN & 0xff, CONFIG_DESC_LEN >> 8,
	3, 1, 0, 0x80, 125,

	/* Interface association: the two CDC interfaces are one function. */
	8, 0x0b, ITF_CDC_COMM, 2, 0x02, 0x02, 0x00, 0,

	/* Communication interface: CDC, ACM, no protocol. */
	9, 0x04, ITF_CDC_COMM, 0, 1, 0x02, 0x02, 0x00, 4,
	/* Header functional descriptor, CDC 1.10. */
	5, 0x24, 0x00, 0x10, 0x01,
	/* Call management: none, data interface named. */
	5, 0x24, 0x01, 0x00, ITF_CDC_DATA,
	/* ACM: line coding and control line state supported. */
	4, 0x24, 0x02, 0x02,
	/* Union: communication interface master, data interface slave. */
	5, 0x24, 0x06, ITF_CDC_COMM, ITF_CDC_DATA,
	/* Notification endpoint: interrupt IN, 8 bytes, 16 ms. */
	7, 0x05, 0x80 | EP_NOTIFY, 0x03, 8, 0, 16,

	/* Data interface: bulk OUT and bulk IN, 64 bytes each. */
	9, 0x04, ITF_CDC_DATA, 0, 2, 0x0a, 0x00, 0x00, 0,
	7, 0x05, EP_DATA, 0x02, USB_PACKET_MAX, 0, 0,
	7, 0x05, 0x80 | EP_DATA, 0x02, USB_PACKET_MAX, 0, 0,

	/* Reset interface: vendor class, no endpoints. */
	9, 0x04, ITF_RESET, 0, 0, 0xff, RESET_INTERFACE_SUBCLASS,
	RESET_INTERFACE_PROTOCOL, 5,
};

static const char *const usb_strings[] = {
	0,				/* 0: language list, built below. */
	"DiscoBSD",
	"DiscoBSD RP2040 console",
	"rp2040",
	"Console",
	"Reset",
};
#define	NSTRINGS	(sizeof(usb_strings) / sizeof(usb_strings[0]))

/*
 * Device state.
 */
enum ep0_stage {
	EP0_IDLE,
	EP0_IN_DATA,			/* Sending a descriptor or reply. */
	EP0_IN_STATUS,			/* Zero-length IN closing an OUT request. */
	EP0_OUT_DATA,			/* Receiving SET_LINE_CODING. */
	EP0_OUT_STATUS,			/* Zero-length OUT closing an IN reply. */
};

#define	USB_TXRING	8192
#define	USB_RXRING	64

static struct {
	int		configured;
	int		dtr;
	int		address;	/* Pending until the status stage. */

	enum ep0_stage	stage;
	const u_char	*in_ptr;	/* Remaining IN data. */
	u_int		in_len;
	int		in_zlp;		/* A short final packet is owed. */
	u_char		out_req;	/* Request awaiting OUT data. */
	u_char		ep0_in_pid;
	u_char		ep0_out_pid;
	u_char		data_in_pid;
	u_char		data_out_pid;

	u_char		line_coding[7];

	int		tx_busy;	/* A bulk IN buffer is armed. */
	u_int		tx_head, tx_tail;
	u_char		tx_ring[USB_TXRING];

	u_int		rx_head, rx_tail;
	u_char		rx_ring[USB_RXRING];

	u_char		reply[64];	/* Small control replies. */
} usbd;

struct tty usbttys[1];

static void usb_tx_kick(void);

/*
 * Buffer control words are shared with the controller, which runs from a
 * slower clock, so the datasheet (4.1.2.7.1) has the processor write the
 * word, wait a few cycles, and only then set AVAILABLE.
 */
static void
usb_buf_arm(u_int off, u_int len, u_int flags)
{
	u_int v = (len & USB_BUF_CTRL_LEN_MASK) | flags;

	DPRAM32(off) = v;
	__asm__ volatile ("nop; nop; nop; nop; nop; nop");
	DPRAM32(off) = v | USB_BUF_CTRL_AVAIL;
}

static void
usb_ep0_stall(void)
{
	USBSET(USB_EP_STALL_ARM) = USB_EP_STALL_ARM_EP0_IN |
	    USB_EP_STALL_ARM_EP0_OUT;
	DPRAM32(USB_DPRAM_BUF_CTRL(0, 1)) = USB_BUF_CTRL_STALL;
	DPRAM32(USB_DPRAM_BUF_CTRL(0, 0)) = USB_BUF_CTRL_STALL;
	usbd.stage = EP0_IDLE;
}

/* Send the next packet of an EP0 IN reply. */
static void
usb_ep0_in_next(void)
{
	u_int n, i;

	n = usbd.in_len;
	if (n > USB_PACKET_MAX)
		n = USB_PACKET_MAX;
	for (i = 0; i < n; i++)
		DPRAM8(USB_DPRAM_EP0_BUF + i) = usbd.in_ptr[i];
	usbd.in_ptr += n;
	usbd.in_len -= n;
	if (usbd.in_len == 0 && n < USB_PACKET_MAX)
		usbd.in_zlp = 0;
	usb_buf_arm(USB_DPRAM_BUF_CTRL(0, 1), n, USB_BUF_CTRL_FULL |
	    (usbd.ep0_in_pid ? USB_BUF_CTRL_DATA1 : 0));
	usbd.ep0_in_pid ^= 1;
}

/* Reply to an IN request with at most wLength bytes. */
static void
usb_ep0_send(const u_char *p, u_int len, u_int wlength)
{
	if (len > wlength)
		len = wlength;
	usbd.in_ptr = p;
	usbd.in_len = len;
	/* A reply shorter than asked that fills its last packet needs a
	 * zero-length packet to say it is over. */
	usbd.in_zlp = (len < wlength) && (len % USB_PACKET_MAX == 0);
	usbd.stage = EP0_IN_DATA;
	usb_ep0_in_next();
}

/* Close an OUT request, or one with no data, with a zero-length IN. */
static void
usb_ep0_ack(void)
{
	usbd.stage = EP0_IN_STATUS;
	usb_buf_arm(USB_DPRAM_BUF_CTRL(0, 1), 0, USB_BUF_CTRL_FULL |
	    USB_BUF_CTRL_DATA1);
	usbd.ep0_in_pid = 0;
}

/* Await the zero-length OUT that closes an IN reply. */
static void
usb_ep0_expect_status(void)
{
	usbd.stage = EP0_OUT_STATUS;
	usb_buf_arm(USB_DPRAM_BUF_CTRL(0, 0), 0, USB_BUF_CTRL_DATA1);
}

static void
usb_string_desc(u_int index, u_int wlength)
{
	const char *s;
	u_int n, i;

	if (index >= NSTRINGS) {
		usb_ep0_stall();
		return;
	}
	if (index == 0) {
		usbd.reply[0] = 4;
		usbd.reply[1] = DESC_STRING;
		usbd.reply[2] = 0x09;	/* English, United States. */
		usbd.reply[3] = 0x04;
		usb_ep0_send(usbd.reply, 4, wlength);
		return;
	}
	s = usb_strings[index];
	for (n = 0; s[n] != '\0' && 2 + 2 * n < sizeof(usbd.reply) - 1; n++)
		continue;
	usbd.reply[0] = 2 + 2 * n;
	usbd.reply[1] = DESC_STRING;
	for (i = 0; i < n; i++) {
		usbd.reply[2 + 2 * i] = s[i];
		usbd.reply[3 + 2 * i] = 0;
	}
	usb_ep0_send(usbd.reply, 2 + 2 * n, wlength);
}

/*
 * Reset the endpoints the configuration names. Called on
 * SET_CONFIGURATION and again on bus reset, which the datasheet says
 * returns the controller's address to zero but says nothing about the
 * data toggles, so they are reset here too.
 */
static void
usb_configure(int on)
{
	usbd.configured = on;
	usbd.dtr = 0;
	usbd.tx_busy = 0;
	usbd.data_in_pid = 0;
	usbd.data_out_pid = 0;
	if (on) {
		/* Ready to receive one packet from the host. */
		usb_buf_arm(USB_DPRAM_BUF_CTRL(EP_DATA, 0), USB_PACKET_MAX, 0);
		usbd.data_out_pid = 1;
		usb_tx_kick();
	}
}

static void
usb_reset_to_bootsel(u_int wvalue)
{
	void (*rom_reset)(u_int, u_int);
	u_int gpio_mask = 0;

	/*
	 * The SDK's contract: bit 8 of wValue says bits 15:9 name an
	 * activity LED, and the low seven bits are the interfaces to
	 * disable. The whole cable is about to go away, so there is no
	 * status stage to send.
	 */
	if (wvalue & 0x100)
		gpio_mask = 1UL << (wvalue >> 9);
	rom_reset = (void (*)(u_int, u_int))rom_func_lookup(ROM_RESET_USB_BOOT);
	rom_reset(gpio_mask, wvalue & 0x7f);
}

static void
usb_reset_to_flash(void)
{
	arm_dsb();
	SCB_REG32(SCB_AIRCR) = AIRCR_VECTKEY | AIRCR_SYSRESETREQ;
	arm_dsb();
	for (;;)
		continue;
}

/* A setup packet is in DPSRAM; answer it. */
static void
usb_setup(void)
{
	u_int type, request, wvalue, windex, wlength;
	u_int dir, kind;

	type = DPRAM8(USB_DPRAM_SETUP + 0);
	request = DPRAM8(USB_DPRAM_SETUP + 1);
	wvalue = DPRAM8(USB_DPRAM_SETUP + 2) | (DPRAM8(USB_DPRAM_SETUP + 3) << 8);
	windex = DPRAM8(USB_DPRAM_SETUP + 4) | (DPRAM8(USB_DPRAM_SETUP + 5) << 8);
	wlength = DPRAM8(USB_DPRAM_SETUP + 6) | (DPRAM8(USB_DPRAM_SETUP + 7) << 8);

	/* Every control transfer starts with DATA1 in both directions. */
	usbd.ep0_in_pid = 1;
	usbd.ep0_out_pid = 1;
	usbd.stage = EP0_IDLE;

	dir = type & 0x80;
	kind = type & 0x60;

	if (kind == 0x00) {			/* Standard. */
		switch (request) {
		case REQ_GET_DESCRIPTOR:
			switch (wvalue >> 8) {
			case DESC_DEVICE:
				usb_ep0_send(usb_device_desc,
				    sizeof(usb_device_desc), wlength);
				return;
			case DESC_CONFIGURATION:
				usb_ep0_send(usb_config_desc,
				    sizeof(usb_config_desc), wlength);
				return;
			case DESC_STRING:
				usb_string_desc(wvalue & 0xff, wlength);
				return;
			}
			break;
		case REQ_SET_ADDRESS:
			/* Takes effect after the status stage, 4.1.2.8. */
			usbd.address = wvalue & 0x7f;
			usb_ep0_ack();
			return;
		case REQ_SET_CONFIGURATION:
			usb_configure(wvalue != 0);
			usb_ep0_ack();
			return;
		case REQ_GET_CONFIGURATION:
			usbd.reply[0] = usbd.configured;
			usb_ep0_send(usbd.reply, 1, wlength);
			return;
		case REQ_GET_STATUS:
			usbd.reply[0] = 0;
			usbd.reply[1] = 0;
			usb_ep0_send(usbd.reply, 2, wlength);
			return;
		case REQ_GET_INTERFACE:
			usbd.reply[0] = 0;
			usb_ep0_send(usbd.reply, 1, wlength);
			return;
		case REQ_CLEAR_FEATURE:
			/*
			 * ENDPOINT_HALT cleared on a bulk endpoint resets the
			 * host's data toggle to DATA0, and the device's must
			 * follow or the host discards the next packets as
			 * retransmissions. Linux's cdc_acm clears both at
			 * open. A buffer already armed is re-armed with the
			 * new toggle and its bytes untouched.
			 */
			if ((type & 0x1f) == 2 && wvalue == 0 &&
			    (windex & 0x0f) == EP_DATA) {
				if (windex & 0x80) {
					u_int bc = DPRAM32(
					    USB_DPRAM_BUF_CTRL(EP_DATA, 1));

					usbd.data_in_pid = 0;
					/*
					 * Only a packet the controller has not
					 * yet sent is re-armed; one already taken
					 * would go out twice.
					 */
					if (usbd.tx_busy &&
					    (bc & USB_BUF_CTRL_AVAIL)) {
						usb_buf_arm(
						    USB_DPRAM_BUF_CTRL(EP_DATA, 1),
						    bc & USB_BUF_CTRL_LEN_MASK,
						    USB_BUF_CTRL_FULL);
						usbd.data_in_pid = 1;
					}
				} else {
					usb_buf_arm(
					    USB_DPRAM_BUF_CTRL(EP_DATA, 0),
					    USB_PACKET_MAX, 0);
					usbd.data_out_pid = 1;
				}
			}
			usb_ep0_ack();
			return;
		case REQ_SET_INTERFACE:
		case REQ_SET_FEATURE:
			usb_ep0_ack();
			return;
		}
	} else if ((kind == 0x20 || kind == 0x40) &&
	    (type & 0x1f) == 1 && (windex & 0xff) == ITF_RESET) {
		/*
		 * picotool sends these as class requests to the interface,
		 * bmRequestType 0x21, whatever the interface's vendor class
		 * suggests; the SDK's handler accepts either type.
		 */
		switch (request) {
		case RESET_REQUEST_BOOTSEL:
			usb_reset_to_bootsel(wvalue);
			/* NOTREACHED */
			break;
		case RESET_REQUEST_FLASH:
			usb_reset_to_flash();
			/* NOTREACHED */
			break;
		}
	} else if (kind == 0x20) {		/* Class: CDC. */
		switch (request) {
		case CDC_SET_LINE_CODING:
			if (wlength != sizeof(usbd.line_coding))
				break;
			usbd.out_req = request;
			usbd.stage = EP0_OUT_DATA;
			usb_buf_arm(USB_DPRAM_BUF_CTRL(0, 0), wlength,
			    USB_BUF_CTRL_DATA1);
			return;
		case CDC_GET_LINE_CODING:
			usb_ep0_send(usbd.line_coding,
			    sizeof(usbd.line_coding), wlength);
			return;
		case CDC_SET_CONTROL_LINE_STATE:
			usbd.dtr = wvalue & 1;
			usb_ep0_ack();
			if (usbd.dtr)
				usb_tx_kick();
			return;
		case CDC_SEND_BREAK:
			usb_ep0_ack();
			return;
		}
	}
	(void)dir;
	usb_ep0_stall();
}

/* The bulk IN buffer is free: fill it from the ring. */
static void
usb_tx_kick(void)
{
	u_int n, i;

	if (! usbd.configured || usbd.tx_busy || usbd.tx_head == usbd.tx_tail)
		return;
	n = usbd.tx_head - usbd.tx_tail;
	if (n > USB_PACKET_MAX)
		n = USB_PACKET_MAX;
	for (i = 0; i < n; i++)
		DPRAM8(BUF_DATA_IN + i) =
		    usbd.tx_ring[(usbd.tx_tail + i) % USB_TXRING];
	usbd.tx_tail += n;
	usbd.tx_busy = 1;
	usb_buf_arm(USB_DPRAM_BUF_CTRL(EP_DATA, 1), n, USB_BUF_CTRL_FULL |
	    (usbd.data_in_pid ? USB_BUF_CTRL_DATA1 : 0));
	usbd.data_in_pid ^= 1;
}

/* Queue one byte for the host, dropping the oldest when nobody drains. */
static void
usb_tx_put(u_char c)
{
	if (usbd.tx_head - usbd.tx_tail >= USB_TXRING)
		usbd.tx_tail++;
	usbd.tx_ring[usbd.tx_head % USB_TXRING] = c;
	usbd.tx_head++;
}

/* A bulk OUT buffer has data from the host. */
static void
usb_rx_done(void)
{
	struct tty *tp = &usbttys[0];
	u_int n, i, c;

	n = DPRAM32(USB_DPRAM_BUF_CTRL(EP_DATA, 0)) & USB_BUF_CTRL_LEN_MASK;
	for (i = 0; i < n; i++) {
		c = DPRAM8(BUF_DATA_OUT + i);
		if (tp->t_state & TS_ISOPEN) {
			ttyinput(c, tp);
		} else if (usbd.rx_head - usbd.rx_tail < USB_RXRING) {
			usbd.rx_ring[usbd.rx_head % USB_RXRING] = c;
			usbd.rx_head++;
		}
	}
	usb_buf_arm(USB_DPRAM_BUF_CTRL(EP_DATA, 0), USB_PACKET_MAX,
	    usbd.data_out_pid ? USB_BUF_CTRL_DATA1 : 0);
	usbd.data_out_pid ^= 1;
}

/*
 * Service the controller. Runs from the interrupt, and also polled with
 * interrupts masked by the console routines, which is safe because the
 * masking keeps the two from interleaving.
 */
static void
usb_service(void)
{
	struct tty *tp = &usbttys[0];
	u_int ints, done, i;

	ints = USBREG(USB_INTS);

	if (ints & USB_INTS_BUS_RESET) {
		USBCLR(USB_SIE_STATUS) = USB_SIE_STATUS_BUS_RESET;
		USBREG(USB_ADDR_ENDP) = 0;
		usbd.address = 0;
		usbd.stage = EP0_IDLE;
		usb_configure(0);
	}

	if (ints & USB_INTS_SETUP_REQ) {
		USBCLR(USB_SIE_STATUS) = USB_SIE_STATUS_SETUP_REC;
		usb_setup();
	}

	if (ints & USB_INTS_BUFF_STATUS) {
		done = USBREG(USB_BUFF_STATUS);
		USBCLR(USB_BUFF_STATUS) = done;

		if (done & USB_BUFF_STATUS_BIT(0, 1)) {
			/* EP0 IN went out. */
			if (usbd.address) {
				USBREG(USB_ADDR_ENDP) = usbd.address;
				usbd.address = 0;
			}
			switch (usbd.stage) {
			case EP0_IN_DATA:
				if (usbd.in_len)
					usb_ep0_in_next();
				else if (usbd.in_zlp) {
					usbd.in_zlp = 0;
					usb_ep0_in_next();
				} else
					usb_ep0_expect_status();
				break;
			default:
				usbd.stage = EP0_IDLE;
				break;
			}
		}
		if (done & USB_BUFF_STATUS_BIT(0, 0)) {
			/* EP0 OUT came in. */
			if (usbd.stage == EP0_OUT_DATA) {
				if (usbd.out_req == CDC_SET_LINE_CODING)
					for (i = 0; i < sizeof(usbd.line_coding);
					    i++)
						usbd.line_coding[i] =
						    DPRAM8(USB_DPRAM_EP0_BUF + i);
				usb_ep0_ack();
			} else
				usbd.stage = EP0_IDLE;
		}
		if (done & USB_BUFF_STATUS_BIT(EP_DATA, 1)) {
			/* Bulk IN went out: send more, or wake the tty. */
			usbd.tx_busy = 0;
			led_control(LED_TTY, 0);
			usb_tx_kick();
			if (! usbd.tx_busy && (tp->t_state & TS_BUSY)) {
				tp->t_state &= ~TS_BUSY;
				ttstart(tp);
			}
		}
		if (done & USB_BUFF_STATUS_BIT(EP_DATA, 0))
			usb_rx_done();
	}

	/* Anything queued while the endpoint was idle goes out now. */
	usb_tx_kick();
}

/*
 * Interrupt entry. The controller is serviced with every interrupt masked,
 * because the tty layer restarts output from the clock interrupt, which
 * outranks this one, and a second usb_tx_kick entered inside the first
 * overwrites the packet the controller is about to send.
 */
void
usbintr(void)
{
	int s;

	s = splhigh();
	usb_service();
	splx(s);
}

/*
 * The vector table names this; see rp2040/locore0.S.
 */
void
USBCTRL_IRQ_Handler(void)
{
	usbintr();
}

/*
 * Bring the controller up as a full-speed device. machdep.c has already
 * started clk_usb. The sequence is the datasheet's 4.1.3.2.1.
 */
void
usbinit(void)
{
	u_int i;

	REG32(RESETS_BASE + RESETS_CLR) = RESETS_USBCTRL;
	while ((REG32(RESETS_BASE + RESETS_DONE) & RESETS_USBCTRL) == 0)
		continue;

	for (i = 0; i < USB_DPRAM_SIZE; i += 4)
		DPRAM32(i) = 0;
	bzero(&usbd, sizeof(usbd));

	/* Default line coding: 115200 8N1, reported back if asked. */
	usbd.line_coding[0] = 115200 & 0xff;
	usbd.line_coding[1] = (115200 >> 8) & 0xff;
	usbd.line_coding[2] = (115200 >> 16) & 0xff;
	usbd.line_coding[3] = 0;
	usbd.line_coding[6] = 8;

	USBREG(USB_MUXING) = USB_MUXING_TO_PHY | USB_MUXING_SOFTCON;
	USBREG(USB_PWR) = USB_PWR_VBUS_DETECT | USB_PWR_VBUS_DETECT_OVERRIDE_EN;
	USBREG(USB_MAIN_CTRL) = USB_MAIN_CTRL_CONTROLLER_EN;
	USBREG(USB_SIE_CTRL) = USB_SIE_CTRL_EP0_INT_1BUF;
	USBREG(USB_INTE) = USB_INTS_BUFF_STATUS | USB_INTS_BUS_RESET |
	    USB_INTS_SETUP_REQ;

	DPRAM32(USB_DPRAM_EP_CTRL(EP_NOTIFY, 1)) = USB_EP_CTRL_ENABLE |
	    USB_EP_CTRL_INT_PER_BUF | USB_EP_CTRL_TYPE_INTERRUPT | BUF_NOTIFY;
	DPRAM32(USB_DPRAM_EP_CTRL(EP_DATA, 0)) = USB_EP_CTRL_ENABLE |
	    USB_EP_CTRL_INT_PER_BUF | USB_EP_CTRL_TYPE_BULK | BUF_DATA_OUT;
	DPRAM32(USB_DPRAM_EP_CTRL(EP_DATA, 1)) = USB_EP_CTRL_ENABLE |
	    USB_EP_CTRL_INT_PER_BUF | USB_EP_CTRL_TYPE_BULK | BUF_DATA_IN;

	arm_intr_set_priority(USBCTRL_IRQ, IPL_TTY);
	arm_intr_enable_irq(USBCTRL_IRQ);

	/* Pull D+ up: the host sees a full-speed device. */
	USBSET(USB_SIE_CTRL) = USB_SIE_CTRL_PULLUP_EN;
}

/*
 * tty glue, the shape of dev/uart.c.
 */

void	usbstart(struct tty *tp);

int
usbopen(dev_t dev, int flag, int mode)
{
	struct tty *tp = &usbttys[0];
	int c;

	tp->t_oproc = usbstart;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		tp->t_ispeed = B115200;
		tp->t_ospeed = B115200;
		ttychars(tp);
		tp->t_state = TS_ISOPEN | TS_CARR_ON;
		tp->t_flags = ECHO | XTABS | CRMOD | CRTBS | CRTERA |
		    CTLECH | CRTKIL;
	}
	if ((tp->t_state & TS_XCLUDE) && u.u_uid != 0)
		return EBUSY;

	/* Keystrokes that arrived before the open. */
	while (usbd.rx_tail != usbd.rx_head) {
		c = usbd.rx_ring[usbd.rx_tail % USB_RXRING];
		usbd.rx_tail++;
		ttyinput(c, tp);
	}
	return ttyopen(dev, tp);
}

int
usbclose(dev_t dev, int flag, int mode)
{
	struct tty *tp = &usbttys[0];

	ttywflush(tp);
	ttyclose(tp);
	return 0;
}

int
usbread(dev_t dev, struct uio *uio, int flag)
{
	return ttread(&usbttys[0], uio, flag);
}

int
usbwrite(dev_t dev, struct uio *uio, int flag)
{
	return ttwrite(&usbttys[0], uio, flag);
}

int
usbioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	int error;

	error = ttioctl(&usbttys[0], cmd, addr, flag);
	if (error < 0)
		error = ENOTTY;
	return error;
}

int
usbselect(dev_t dev, int rw)
{
	return ttyselect(&usbttys[0], rw);
}

void
usbstart(struct tty *tp)
{
	int s;

	s = spltty();
	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
		splx(s);
		return;
	}
	ttyowake(tp);
	while (tp->t_outq.c_cc != 0 &&
	    usbd.tx_head - usbd.tx_tail < USB_TXRING)
		usb_tx_put(getc(&tp->t_outq));
	usb_tx_kick();
	if (usbd.tx_busy) {
		tp->t_state |= TS_BUSY;
		led_control(LED_TTY, 1);
	}
	splx(s);
}

/*
 * Console output. Bytes go into the ring and out as the host drains it;
 * the wait below only bounds how long a full ring stalls the kernel when
 * the host is present but slow, since a host that is absent is what the
 * ring is for. The controller is serviced on every call, not only while
 * waiting, so that enumeration and the reset interface keep working from
 * contexts that run with interrupts masked, a panic or a fault loop among
 * them.
 */
void
usbputc(dev_t dev, char c)
{
	int s, spin;

	s = spltty();
	usb_service();
	for (spin = 0; usbd.configured && usbd.dtr &&
	    usbd.tx_head - usbd.tx_tail >= USB_TXRING && spin < 20000; spin++)
		usb_service();
	usb_tx_put(c);
	usb_tx_kick();
	splx(s);
}

/*
 * Push everything queued out to the host before a reset, which would
 * otherwise take the ring with it. Bounded, so a host that is not reading
 * delays the reset by at most a moment.
 */
void
usbdrain(void)
{
	int s, spin;

	s = spltty();
	for (spin = 0; usbd.configured && usbd.tx_head != usbd.tx_tail &&
	    spin < 200000; spin++)
		usb_service();
	splx(s);
}

/*
 * Service the controller once from a context that has nothing else to
 * do, a nested fault among them, so the host keeps its console and the
 * reset interface.
 */
void
usbpoll(void)
{
	int s;

	s = spltty();
	usb_service();
	splx(s);
}

/*
 * Console input: poll the controller until a byte arrives.
 */
char
usbgetc(dev_t dev)
{
	int s, c;

	s = spltty();
	while (usbd.rx_tail == usbd.rx_head)
		usb_service();
	c = usbd.rx_ring[usbd.rx_tail % USB_RXRING];
	usbd.rx_tail++;
	splx(s);
	return c;
}

static int
usbprobe(struct conf_device *config)
{
	printf("uartusb: CDC-ACM on the USB device controller, interrupt %u",
	    USBCTRL_IRQ);
	if (CONS_MAJOR == UARTUSB_MAJOR)
		printf(", console");
	printf("\n");
	return 1;
}

struct driver uartusbdriver = {
	"uartusb", usbprobe,
};
