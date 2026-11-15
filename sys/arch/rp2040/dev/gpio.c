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
 * /dev/porta and /dev/portb: the pins a board leaves free, driven through
 * the single-cycle IO block (datasheet 2.3.1) with the request numbers
 * of <machine/gpio.h>. A pin this device configures takes the SIO function
 * (GPIOn_CTRL FUNCSEL 5, datasheet 2.19.2) and the pad's reset setting:
 * input enabled, Schmitt trigger, 4 mA drive and the pull-down, so an
 * unconnected input reads zero. Released pins return to no function.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/ioctl.h>

#include <machine/board.h>
#include <machine/gpio.h>

_Static_assert(GPIO_IOC_VOID == IOC_VOID, "GPIO requests carry no parameter");
_Static_assert(BOARD_ID_PICO == 1 && BOARD_ID_PICOCALC == 2,
    "gpio_board_pins() switches on these values");

#define	GREG32(a)		(*(volatile u_int *)(a))

#define	IO_BANK0_CTRL(pin)	(0x40014004UL + 8 * (pin))
#define	IO_FUNC_SIO		5
#define	IO_FUNC_NULL		0x1f
#define	PADS_BANK0_GPIO(pin)	(0x4001c004UL + 4 * (pin))
#define	PAD_RESET		0x56	/* IE, 4 mA, PDE, SCHMITT. */

#define	SIO_GPIO_IN		0xd0000004UL
#define	SIO_GPIO_OUT_SET	0xd0000014UL
#define	SIO_GPIO_OUT_CLR	0xd0000018UL
#define	SIO_GPIO_OUT_XOR	0xd000001cUL
#define	SIO_GPIO_OE_SET		0xd0000024UL
#define	SIO_GPIO_OE_CLR		0xd0000028UL

static u_int gpio_owned;	/* GPn bits this device configured. */

int
gpioopen(dev_t dev, int flag __unused, int mode __unused)
{
	if (minor(dev) >= GPIO_NPORTS || gpio_board_pins(board_id()) == 0)
		return ENXIO;
	if (u.u_uid != 0)
		return EPERM;
	return 0;
}

/*
 * The PIC32 driver's text interface is absent, and conf.c's norw() would
 * report a write that changed no pin as done, so both directions fail.
 */
int
gpiorw(dev_t dev __unused, struct uio *uio __unused, int flag __unused)
{
	return ENODEV;
}

/* Give each pin of pins the function func and the reset pad setting. */
static void
gpio_route(u_int pins, u_int func)
{
	u_int pin;

	for (pin = 0; pins >> pin != 0; pin++)
		if (pins >> pin & 1) {
			GREG32(PADS_BANK0_GPIO(pin)) = PAD_RESET;
			GREG32(IO_BANK0_CTRL(pin)) = func;
		}
}

/*
 * The port is the low byte of the request, as in the PIC32 driver, so one
 * open file reaches both ports. addr is the mask itself.
 */
int
gpioioctl(dev_t dev __unused, u_int cmd, caddr_t addr, int flag __unused)
{
	u_int port = cmd & 0xff;
	u_int free_pins = gpio_board_pins(board_id());
	u_int pins, mine;

	cmd &= ~0xffU;
	if (!gpio_request_valid(cmd))
		return EINVAL;
	if (port >= GPIO_NPORTS)
		return ENXIO;
	pins = gpio_port_bits(port, (u_int)addr) & free_pins;

	if (cmd & GPIO_COMMAND & (GPIO_CONFIN | GPIO_CONFOUT)) {
		if (cmd & GPIO_COMMAND & GPIO_CONFIN)
			GREG32(SIO_GPIO_OE_CLR) = pins;
		gpio_route(pins, IO_FUNC_SIO);
		if (cmd & GPIO_COMMAND & GPIO_CONFOUT)
			GREG32(SIO_GPIO_OE_SET) = pins;
		gpio_owned |= pins;
	}
	if (cmd & GPIO_COMMAND & GPIO_DECONF) {
		mine = pins & gpio_owned;
		GREG32(SIO_GPIO_OE_CLR) = mine;
		gpio_route(mine, IO_FUNC_NULL);
		gpio_owned &= ~mine;
	}
	mine = pins & gpio_owned;
	if (cmd & GPIO_COMMAND & GPIO_STORE) {
		/* Every owned pin of the port takes its bit of the mask. */
		GREG32(SIO_GPIO_OUT_CLR) =
		    gpio_port_bits(port, ~0U) & gpio_owned & ~mine;
		GREG32(SIO_GPIO_OUT_SET) = mine;
	}
	if (cmd & GPIO_COMMAND & GPIO_SET)
		GREG32(SIO_GPIO_OUT_SET) = mine;
	if (cmd & GPIO_COMMAND & GPIO_CLEAR)
		GREG32(SIO_GPIO_OUT_CLR) = mine;
	if (cmd & GPIO_COMMAND & GPIO_INVERT)
		GREG32(SIO_GPIO_OUT_XOR) = mine;
	if (cmd & GPIO_COMMAND & GPIO_POLL)
		u.u_rval = gpio_port_value(port,
		    GREG32(SIO_GPIO_IN) & free_pins);
	return 0;
}
