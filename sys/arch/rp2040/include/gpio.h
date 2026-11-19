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
 * General-purpose pins as /dev/porta and /dev/portb (arch/rp2040/dev/gpio.c).
 *
 * The request numbers are those of the RetroBSD GPIO driver for the PIC32
 * (its <sys/gpio.h>), so a program written for that interface needs only
 * this header: each request is a command bit above a port number, and its
 * argument is a 16-bit mask of pins within the port, passed by value. The
 * RP2040 has one bank of 30 pins; port A is GP0-GP15 and port B is
 * GP16-GP29.
 *
 *	ioctl(fd, GPIO_PORTA | GPIO_CONFOUT, 1 << 2);	GP2 becomes an output
 *	ioctl(fd, GPIO_PORTA | GPIO_SET, 1 << 2);	and goes high
 *	v = ioctl(fd, GPIO_PORTB | GPIO_POLL, 0);	levels of GP16-GP29
 *
 * Only the pins a board leaves free are reachable: a request names others
 * without effect, and GPIO_POLL reports them as zero. Output requests act
 * on pins this device configured. The RP2040 pad has no open-drain mode, so
 * GPIO_CONFOD, which the PIC32 driver accepts, is refused here.
 *
 * GPIO_PWM, a request the PIC32 driver lacks, drives pins from a PWM slice
 * at a fixed frequency. Its argument is the pin mask in the low 16 bits and
 * the duty in the high 16, a count of GPIO_PWM_DUTY_MAX parts:
 *
 *	ioctl(fd, GPIO_PORTA | GPIO_PWM, 1 << 8 | 128 << 16);	GP8 at 50 percent
 *
 * Unlike the digital requests it answers EINVAL for a pin the board holds,
 * a duty past the maximum or an empty mask, and EBUSY for a slice the
 * kernel uses. GPIO_DECONF stops the pins; the last close of any port node
 * stops every PWM pin.
 */

#ifndef _RP2040_GPIO_H_
#define	_RP2040_GPIO_H_

#define	GPIO_PORT(n)	((n) & 0xff)			/* port number */
#define	GPIO_PORTA	GPIO_PORT(0)
#define	GPIO_PORTB	GPIO_PORT(1)

#define	GPIO_IOC_VOID	0x20000000U			/* IOC_VOID */
#define	GPIO_COMMAND	0x1fff0000U			/* command mask */
#define	GPIO_REQUEST(bit) (GPIO_IOC_VOID | 1U << (bit) | 'g' << 8)
#define	GPIO_CONFIN	GPIO_REQUEST(16)	/* configure as input */
#define	GPIO_CONFOUT	GPIO_REQUEST(17)	/* configure as output */
#define	GPIO_CONFOD	GPIO_REQUEST(18)	/* open drain: refused */
#define	GPIO_DECONF	GPIO_REQUEST(19)	/* release the pins */
#define	GPIO_STORE	GPIO_REQUEST(20)	/* store all outputs */
#define	GPIO_SET	GPIO_REQUEST(21)	/* set to 1 by mask */
#define	GPIO_CLEAR	GPIO_REQUEST(22)	/* set to 0 by mask */
#define	GPIO_INVERT	GPIO_REQUEST(23)	/* invert by mask */
#define	GPIO_POLL	GPIO_REQUEST(24)	/* return the pin levels */
#define	GPIO_PWM	GPIO_REQUEST(26)	/* PWM by mask; duty << 16 */

/*
 * The PWM carrier: every slice counts clk_sys / GPIO_PWM_DIV_INT and wraps
 * after GPIO_PWM_DUTY_MAX counts, 1,922 Hz at a 125 MHz clk_sys. A channel
 * is high while the counter is below its compare value, which the duty sets
 * directly; TOP is one less than the period, so the duty GPIO_PWM_DUTY_MAX
 * lies above the counter's range and holds the pin high.
 */
#define	GPIO_PWM_DUTY_MAX	255U
#define	GPIO_PWM_TOP		(GPIO_PWM_DUTY_MAX - 1)
#define	GPIO_PWM_DIV_INT	255U

#define	GPIO_NPORTS	2
#define	GPIO_PORTA_PINS	0x0000ffffU		/* GP0-GP15 */
#define	GPIO_PORTB_PINS	0x3fff0000U		/* GP16-GP29 */

/*
 * The pins a board leaves to this device, as a mask of GPn bits; board is
 * a BOARD_ID_ value of <machine/board.h>. The bare Pico keeps GP0 and GP1
 * for UART0 and GP23, GP24, GP25 and GP29 for its regulator mode, VBUS
 * sense, LED and VSYS divider. The PicoCalc carrier wires every pin but
 * GP8, GP9 and GP28, which reach only its expansion header
 * (doc/PICOCALC.md, "Pin map").
 */
static inline unsigned int
gpio_board_pins(int board)
{
	switch (board) {
	case 1:		/* BOARD_ID_PICO */
		return 0x1c7ffffcU;
	case 2:		/* BOARD_ID_PICOCALC */
		return 1U << 8 | 1U << 9 | 1U << 28;
	default:
		return 0;
	}
}

/* The GPn bits a port-relative mask names; zero for a port past B. */
static inline unsigned int
gpio_port_bits(unsigned int port, unsigned int mask)
{
	if (port == 0)
		return mask & GPIO_PORTA_PINS;
	if (port == 1)
		return (mask << 16) & GPIO_PORTB_PINS;
	return 0;
}

/* The port-relative value of a word of GPn levels. */
static inline unsigned int
gpio_port_value(unsigned int port, unsigned int levels)
{
	if (port == 0)
		return levels & GPIO_PORTA_PINS;
	if (port == 1)
		return (levels & GPIO_PORTB_PINS) >> 16;
	return 0;
}

/*
 * True for a request this device carries out, its port number already
 * removed: no parameter structure, group 'g', at least one command bit,
 * no bit outside the nine commands, no open drain, and not input and
 * output at once.
 */
static inline int
gpio_request_valid(unsigned int request)
{
	unsigned int commands = request & GPIO_COMMAND;
	unsigned int known = (GPIO_CONFIN | GPIO_CONFOUT | GPIO_DECONF |
	    GPIO_STORE | GPIO_SET | GPIO_CLEAR | GPIO_INVERT | GPIO_POLL |
	    GPIO_PWM) & GPIO_COMMAND;

	if ((request & ~GPIO_COMMAND) != (GPIO_IOC_VOID | 'g' << 8))
		return 0;
	if (commands == 0 || (commands & ~known) != 0)
		return 0;
	if ((commands & GPIO_CONFIN & GPIO_COMMAND) != 0 &&
	    (commands & GPIO_CONFOUT & GPIO_COMMAND) != 0)
		return 0;
	if ((commands & GPIO_PWM & GPIO_COMMAND) != 0 &&
	    commands != (GPIO_PWM & GPIO_COMMAND))
		return 0;
	return 1;
}

/*
 * The PWM slice of a pin (datasheet 4.5.2: slice n serves GP2n and GP2n+1,
 * wrapping every 16 pins; channel A is the even pin) and the pins of a slice.
 */
#define	GPIO_PWM_SLICE(pin)	(((pin) >> 1) & 7U)
#define	GPIO_PWM_SLICE_PINS(s)	(3U << 2 * (s) | 3U << (2 * (s) + 16))

/*
 * The refusals of GPIO_PWM, as the errno values they become (EINVAL and
 * EBUSY; gpio.c asserts the match).
 */
#define	GPIO_PWM_EINVAL		22
#define	GPIO_PWM_EBUSY		16

#define	GPIO_PWM_SLICE_BELL	5U	/* PicoCalc: the amplifier pins */
#define	GPIO_PWM_SLICE_PCM	6U	/* PicoCalc: /dev/audio's sample clock */

/*
 * True when pins (GPn bits) lie on a slice the kernel uses: on a PicoCalc,
 * slice 5 always (the bell and /dev/audio's carrier) and slice 6 while
 * /dev/audio is open. The bare Pico's kernel uses no slice.
 */
static inline int
gpio_pwm_slice_held(int board, unsigned int pins, int audio_open)
{
	if (board != 2)		/* BOARD_ID_PICOCALC */
		return 0;
	if ((pins & GPIO_PWM_SLICE_PINS(GPIO_PWM_SLICE_BELL)) != 0)
		return 1;
	return audio_open != 0 &&
	    (pins & GPIO_PWM_SLICE_PINS(GPIO_PWM_SLICE_PCM)) != 0;
}

/*
 * Zero when a PWM request may run, else the errno it fails with: pins is
 * the whole mask the request names, not the part the board leaves free.
 */
static inline int
gpio_pwm_check(int board, unsigned int pins, unsigned int duty,
    int audio_open)
{
	if (pins == 0 || (pins & ~gpio_board_pins(board)) != 0 ||
	    duty > GPIO_PWM_DUTY_MAX)
		return GPIO_PWM_EINVAL;
	if (gpio_pwm_slice_held(board, pins, audio_open))
		return GPIO_PWM_EBUSY;
	return 0;
}

/*
 * The slice's compare word with the channel of pin set to duty: channel A
 * is the low half and channel B the high half, and the other keeps its value.
 */
static inline unsigned int
gpio_pwm_compare(unsigned int pin, unsigned int old, unsigned int duty)
{
	unsigned int shift = 16 * (pin & 1);

	return (old & ~(0xffffU << shift)) | duty << shift;
}

#ifdef KERNEL
struct uio;

int	gpioopen(dev_t dev, int flag, int mode);
int	gpioclose(dev_t dev, int flag, int mode);
int	gpiorw(dev_t dev, struct uio *uio, int flag);
int	gpioioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);

/*
 * The GPn bits gpioioctl() configured, PWM pins included, which
 * machdep.adc.N reads; and the PWM pins among them, which /dev/audio reads.
 */
extern u_int gpio_owned;
extern u_int gpio_pwm_pins;
#endif

#endif /* !_RP2040_GPIO_H_ */
