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
 * RP2040 registers the PicoCalc console drivers program: reset control
 * (datasheet 2.14), pin function and pad control (2.19), the SIO GPIO
 * outputs (2.3.1.7) and the microsecond timer (4.6). Private to
 * st7365p.c and picokbd.c.
 */

#ifndef _PICOCALC_REG_H
#define _PICOCALC_REG_H

#define	PC_REG32(a)		(*(volatile u_int *)(a))

#define	PC_RESETS_BASE		0x4000c000UL
#define	PC_RESETS_RESET		0x0
#define	PC_RESETS_DONE		0x8
#define	PC_ATOMIC_SET		0x2000
#define	PC_ATOMIC_CLR		0x3000

#define	PC_RESET_I2C1		(1UL << 4)
#define	PC_RESET_IO_BANK0	(1UL << 5)
#define	PC_RESET_PADS_BANK0	(1UL << 8)
#define	PC_RESET_SPI1		(1UL << 17)

#define	PC_IO_BANK0_CTRL(n)	(0x40014000UL + 0x004 + 8 * (n))
#define	PC_FUNC_SPI		1
#define	PC_FUNC_I2C		3
#define	PC_FUNC_SIO		5

#define	PC_PADS_GPIO(n)		(0x4001c000UL + 0x04 + 4 * (n))
#define	PC_PADS_SCHMITT		(1UL << 1)
#define	PC_PADS_PDE		(1UL << 2)
#define	PC_PADS_PUE		(1UL << 3)
#define	PC_PADS_IE		(1UL << 6)
#define	PC_PADS_OD		(1UL << 7)

#define	PC_SIO_OUT_SET		0xd0000014UL
#define	PC_SIO_OUT_CLR		0xd0000018UL
#define	PC_SIO_OE_SET		0xd0000024UL

#define	PC_TIMERAWL		0x40054028UL	/* Microseconds, free running. */

static __inline void
pc_unreset(u_int mask)
{
	PC_REG32(PC_RESETS_BASE + PC_ATOMIC_CLR + PC_RESETS_RESET) = mask;
	while ((PC_REG32(PC_RESETS_BASE + PC_RESETS_DONE) & mask) != mask)
		continue;
}

/* Route pin n to function f with the given pad bits, input enabled. */
static __inline void
pc_pin(u_int n, u_int f, u_int pad)
{
	PC_REG32(PC_PADS_GPIO(n)) = (PC_REG32(PC_PADS_GPIO(n)) &
	    ~(PC_PADS_OD | PC_PADS_PUE | PC_PADS_PDE)) | PC_PADS_IE | pad;
	PC_REG32(PC_IO_BANK0_CTRL(n)) = f;
}

static __inline void
pc_gpio_put(u_int n, int on)
{
	PC_REG32(on ? PC_SIO_OUT_SET : PC_SIO_OUT_CLR) = 1UL << n;
}

/* Drive pin n from SIO as an output at the given level. */
static __inline void
pc_gpio_output(u_int n, int on)
{
	pc_gpio_put(n, on);
	pc_pin(n, PC_FUNC_SIO, 0);
	PC_REG32(PC_SIO_OE_SET) = 1UL << n;
}

static __inline u_int
pc_micros(void)
{
	return PC_REG32(PC_TIMERAWL);
}

#endif /* !_PICOCALC_REG_H */
