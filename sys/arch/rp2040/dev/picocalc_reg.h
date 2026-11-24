/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * RP2040 registers the PicoCalc console drivers program: reset control
 * (datasheet 2.14), pin function and pad control (2.19), the SIO GPIO
 * outputs (2.3.1.7) and the microsecond timer (4.6). Private to
 * st7365p.c and picokbd.c.
 */

#ifndef _PICOCALC_REG_H
#define _PICOCALC_REG_H

/* A host test predefines PC_REG32 to route the registers to a model. */
#ifndef PC_REG32
#define	PC_REG32(a)		(*(volatile u_int *)(a))
#endif

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
#define	PC_PADS_DRIVE_MASK	(0x3UL << 4)	/* 2, 4, 8, 12 mA */
#define	PC_PADS_IE		(1UL << 6)
#define	PC_PADS_OD		(1UL << 7)

#define	PC_SIO_GPIO_IN		0xd0000004UL
#define	PC_SIO_OUT_SET		0xd0000014UL
#define	PC_SIO_OUT_CLR		0xd0000018UL
#define	PC_SIO_OE_SET		0xd0000024UL
#define	PC_SIO_OE_CLR		0xd0000028UL

#define	PC_TIMERAWL		0x40054028UL	/* Microseconds, free running. */

static __inline void
pc_unreset(u_int mask)
{
	PC_REG32(PC_RESETS_BASE + PC_ATOMIC_CLR + PC_RESETS_RESET) = mask;
	while ((PC_REG32(PC_RESETS_BASE + PC_RESETS_DONE) & mask) != mask)
		continue;
}

/*
 * Route pin n to function f with the given pad bits, input enabled. The
 * DRIVE field (PADS_BANK0 GPIOn bits 5:4, reset 0x1 for 4 mA) keeps its
 * value when pad omits a drive strength and takes the strength pad names
 * otherwise, so a request for 8 mA (0x2) does not combine with the reset
 * value into 12 mA (0x3). A pad word of 0 therefore cannot select 2 mA.
 */
static __inline void
pc_pin(u_int n, u_int f, u_int pad)
{
	u_int clear = PC_PADS_OD | PC_PADS_PUE | PC_PADS_PDE;

	if ((pad & PC_PADS_DRIVE_MASK) != 0)
		clear |= PC_PADS_DRIVE_MASK;
	PC_REG32(PC_PADS_GPIO(n)) = (PC_REG32(PC_PADS_GPIO(n)) & ~clear) |
	    PC_PADS_IE | pad;
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
