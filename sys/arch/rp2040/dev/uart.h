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

#ifndef	_UART_H
#define	_UART_H

/*
 * The RP2040 carries two UARTs, both ARM PL011 rather than the ST USART the
 * other targets drive. The tty half of dev/uart.c is unchanged; only the
 * register access below differs.
 */
#define	NUART	2

#ifdef KERNEL

#include <machine/intr.h>

#define	UART0_BASE		0x40034000UL
#define	UART1_BASE		0x40038000UL

/* PL011 registers, RP2040 datasheet section 4.2.8. */
#define	UART_DR			0x000		/* Data. */
#define	UART_FR			0x018		/* Flags. */
#define	UART_IBRD		0x024		/* Integer baud divisor. */
#define	UART_FBRD		0x028		/* Fractional baud divisor. */
#define	UART_LCR_H		0x02c		/* Line control. */
#define	UART_CR			0x030		/* Control. */
#define	UART_IFLS		0x034		/* FIFO level select. */
#define	UART_IMSC		0x038		/* Interrupt mask. */
#define	UART_MIS		0x040		/* Masked interrupt status. */
#define	UART_ICR		0x044		/* Interrupt clear. */

#define	UART_FR_BUSY		0x0008		/* Transmitting. */
#define	UART_FR_RXFE		0x0010		/* Receive FIFO empty. */
#define	UART_FR_TXFF		0x0020		/* Transmit FIFO full. */
#define	UART_FR_RXFF		0x0040
#define	UART_FR_TXFE		0x0080

#define	UART_LCR_H_FEN		0x0010		/* FIFOs enabled. */
#define	UART_LCR_H_WLEN_8	0x0060		/* Eight data bits. */

#define	UART_CR_UARTEN		0x0001
#define	UART_CR_TXE		0x0100
#define	UART_CR_RXE		0x0200

#define	UART_INT_RX		0x0010		/* Receive. */
#define	UART_INT_TX		0x0020		/* Transmit. */
#define	UART_INT_ALL		0x07ff

#define	UART_REG(base, off)	(*(volatile u_int *)((base) + (off)))

/*
 * The hardware layer the tty half calls. Each replaces one ST LL_USART entry
 * point, with PL011 semantics: the ST parts signal an empty holding register,
 * the PL011 signals a full or empty FIFO.
 */
static __inline int
uart_tx_ready(u_int base)
{
	return (UART_REG(base, UART_FR) & UART_FR_TXFF) == 0;
}

static __inline int
uart_tx_idle(u_int base)
{
	return (UART_REG(base, UART_FR) & UART_FR_BUSY) == 0;
}

static __inline int
uart_rx_ready(u_int base)
{
	return (UART_REG(base, UART_FR) & UART_FR_RXFE) == 0;
}

static __inline void
uart_tx_put(u_int base, int c)
{
	UART_REG(base, UART_DR) = (u_int)c & 0xff;
}

static __inline int
uart_rx_get(u_int base)
{
	return (int)(UART_REG(base, UART_DR) & 0xff);
}

static __inline void
uart_enable(u_int base)
{
	UART_REG(base, UART_CR) |= UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

static __inline void
uart_disable(u_int base)
{
	UART_REG(base, UART_CR) &= ~UART_CR_UARTEN;
}

static __inline void
uart_irq_enable(u_int base, u_int mask)
{
	UART_REG(base, UART_IMSC) |= mask;
}

static __inline void
uart_irq_disable(u_int base, u_int mask)
{
	UART_REG(base, UART_IMSC) &= ~mask;
}

static __inline u_int
uart_irq_status(u_int base)
{
	return UART_REG(base, UART_MIS);
}

static __inline void
uart_irq_clear(u_int base, u_int mask)
{
	UART_REG(base, UART_ICR) = mask;
}

void		uartinit(int unit);
int		uartopen(dev_t dev, int flag, int mode);
int		uartclose(dev_t dev, int flag, int mode);
int		uartread(dev_t dev, struct uio *uio, int flag);
int		uartwrite(dev_t dev, struct uio *uio, int flag);
int		uartselect(dev_t dev, int rw);
int		uartioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
void		uartintr(dev_t dev);
void		uartstart(struct tty *tp);
void		uartputc(dev_t dev, char c);
char		uartgetc(dev_t dev);

extern struct	tty uartttys[NUART];

#endif	/* KERNEL */

#endif	/* !_UART_H */
