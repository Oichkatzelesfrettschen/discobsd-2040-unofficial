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
 *
 * With the DEV_GPIO_PWM option, GPIO_PWM gives pins to a PWM slice (datasheet
 * 4.5) instead: the slice runs the fixed carrier of <machine/gpio.h> and
 * each pin's channel holds the duty. A kernel without the option answers
 * GPIO_PWM with EOPNOTSUPP and carries none of this code. Every kernel PWM
 * user is on the PicoCalc, whose slice 5 is the bell and /dev/audio's
 * carrier and whose slice 6 is /dev/audio's sample clock; gpio_pwm_check()
 * refuses those, and pcmopen() refuses to open
 * while a program holds slice 6. A PWM pin is also an owned pin, so
 * GPIO_DECONF, a digital configuration request and the last close all
 * take it back, to no function after DECONF and close.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/ioctl.h>

#include <sys/errno.h>

#include <machine/board.h>
#include <machine/gpio.h>

#ifdef DEV_GPIO_PWM
#ifndef DEV_GPIO
#error "DEV_GPIO_PWM is the PWM request of DEV_GPIO"
#endif
/*
 * picoaudio.c, which only the PicoCalc kernels compile, owns the flag that
 * /dev/audio is open; a kernel without it has no audio slice to protect.
 */
#ifdef PICOCALC_ENABLED
extern int picoaudio_pcm;
#define	GPIO_AUDIO_OPEN		picoaudio_pcm
#else
#define	GPIO_AUDIO_OPEN		0
#endif
#endif

_Static_assert(GPIO_IOC_VOID == IOC_VOID, "GPIO requests carry no parameter");
_Static_assert(BOARD_ID_PICO == 1 && BOARD_ID_PICOCALC == 2,
    "gpio_board_pins() switches on these values");
#ifdef DEV_GPIO_PWM
_Static_assert(GPIO_PWM_EINVAL == EINVAL && GPIO_PWM_EBUSY == EBUSY,
    "gpio_pwm_check() returns errno values");
_Static_assert(CPU_KHZ * 1000 / GPIO_PWM_DIV_INT / GPIO_PWM_DUTY_MAX > 1000,
    "the carrier is an audible-band PWM no slower than 1 kHz");
_Static_assert(GPIO_PWM_DIV_INT <= 255, "DIV.INT is eight bits");
#endif

#define	GREG32(a)		(*(volatile u_int *)(a))

#define	IO_BANK0_CTRL(pin)	(0x40014004UL + 8 * (pin))
#define	IO_FUNC_SIO		5
#define	IO_FUNC_NULL		0x1f
#define	PADS_BANK0_GPIO(pin)	(0x4001c004UL + 4 * (pin))
#define	PAD_RESET		0x56	/* IE, 4 mA, PDE, SCHMITT. */

#define	IO_FUNC_PWM		4
#define	RESETS_BASE		0x4000c000UL
#define	RESETS_RESET_CLR	0x3000		/* Atomic clear alias. */
#define	RESETS_DONE		0x8
#define	RESETS_PWM		(1UL << 14)
#define	PWM_SLICE(pin)		(0x40050000UL + 0x14 * GPIO_PWM_SLICE(pin))
#define	PWM_CSR			0x00
#define	PWM_DIV			0x04
#define	PWM_CC			0x0c
#define	PWM_TOP			0x10
#define	PWM_CSR_EN		(1UL << 0)
#define	PWM_DIV_INT(n)		((u_int)(n) << 4)

#define	SIO_GPIO_IN		0xd0000004UL
#define	SIO_GPIO_OUT_SET	0xd0000014UL
#define	SIO_GPIO_OUT_CLR	0xd0000018UL
#define	SIO_GPIO_OUT_XOR	0xd000001cUL
#define	SIO_GPIO_OE_SET		0xd0000024UL
#define	SIO_GPIO_OE_CLR		0xd0000028UL

u_int gpio_owned;	/* GPn bits this device configured. */
#ifdef DEV_GPIO_PWM
u_int gpio_pwm_pins;	/* The part of gpio_owned that a PWM slice drives. */
#endif

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

#ifdef DEV_GPIO_PWM
/*
 * The last close of a port node stops every PWM pin: d_close runs once per
 * node, and one open file reaches both ports, so no finer owner exists.
 */
int
gpioclose(dev_t dev __unused, int flag __unused, int mode __unused)
{
	gpio_route(gpio_pwm_pins, IO_FUNC_NULL);
	gpio_owned &= ~gpio_pwm_pins;
	gpio_pwm_pins = 0;
	return 0;
}

/*
 * Start pins (every GPn bit of one request, checked against the board) on
 * the PWM slices at duty. Each pin's channel gets its compare value before
 * the pad changes function, so the first period already has the duty; the
 * slice's period, divider and enable are the same for every pin that
 * shares it.
 */
static int
gpio_pwm_start(u_int pins, u_int duty)
{
	u_int pin, slice;
	int error;

	error = gpio_pwm_check(board_id(), pins, duty, GPIO_AUDIO_OPEN);
	if (error != 0)
		return error;
	GREG32(RESETS_BASE + RESETS_RESET_CLR) = RESETS_PWM;
	while ((GREG32(RESETS_BASE + RESETS_DONE) & RESETS_PWM) == 0)
		continue;
	for (pin = 0; pins >> pin != 0; pin++)
		if (pins >> pin & 1) {
			slice = PWM_SLICE(pin);
			GREG32(slice + PWM_TOP) = GPIO_PWM_TOP;
			GREG32(slice + PWM_DIV) = PWM_DIV_INT(GPIO_PWM_DIV_INT);
			GREG32(slice + PWM_CC) = gpio_pwm_compare(pin,
			    GREG32(slice + PWM_CC), duty);
			GREG32(slice + PWM_CSR) = PWM_CSR_EN;
			gpio_route(1U << pin, IO_FUNC_PWM);
		}
	gpio_owned |= pins;
	gpio_pwm_pins |= pins;
	return 0;
}
#endif /* DEV_GPIO_PWM */

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
	if (cmd == GPIO_PWM)
#ifdef DEV_GPIO_PWM
		return gpio_pwm_start(gpio_port_bits(port, (u_int)addr),
		    (u_int)addr >> 16);
#else
		return EOPNOTSUPP;
#endif
	pins = gpio_port_bits(port, (u_int)addr) & free_pins;

	if (cmd & GPIO_COMMAND & (GPIO_CONFIN | GPIO_CONFOUT)) {
		if (cmd & GPIO_COMMAND & GPIO_CONFIN)
			GREG32(SIO_GPIO_OE_CLR) = pins;
		gpio_route(pins, IO_FUNC_SIO);
		if (cmd & GPIO_COMMAND & GPIO_CONFOUT)
			GREG32(SIO_GPIO_OE_SET) = pins;
		gpio_owned |= pins;
#ifdef DEV_GPIO_PWM
		gpio_pwm_pins &= ~pins;
#endif
	}
	if (cmd & GPIO_COMMAND & GPIO_DECONF) {
		mine = pins & gpio_owned;
#ifdef DEV_GPIO_PWM
		gpio_pwm_pins &= ~mine;
#endif
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
