/*
 * Console over UART0, an ARM PL011 on the RP2040.
 *
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)cons.c	1.3 (2.11BSD GTE) 1997/4/25
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/config.h>

#include <rp2040/dev/uart.h>

#include <machine/intr.h>

#define CONCAT(x,y) x ## y
#define BBAUD(x) CONCAT(B,x)

#ifndef UART_BAUD
#define UART_BAUD 115200
#endif

/*
 * RP2040 PL011 instance.
 *
 * The ST targets carry six USARTs with several pin choices each, so their
 * table records a port, a pin, an APB divisor, and an alternate function.
 * Here each UART has one pin pair in use and the peripheral clock is common,
 * so the table records the register base, the two pins, and the interrupt.
 */
struct uart_inst {
    u_int	base;		/* PL011 register base. */
    u_char	tx_gpio;
    u_char	rx_gpio;
    u_char	irq;		/* NVIC interrupt number. */
};

/* Pin function 2 selects a UART on every pin the Pico brings out. */
#define GPIO_FUNC_UART	2

/* RP2040 datasheet section 2.3.2, interrupt table. */
#define UART0_IRQ	20
#define UART1_IRQ	21

static const struct uart_inst uart[NUART] = {
    /* UART0 on GP0 and GP1, the pair the Pico exposes on pins 1 and 2. */
    { UART0_BASE, 0, 1, UART0_IRQ },
    /* UART1 on GP4 and GP5. */
    { UART1_BASE, 4, 5, UART1_IRQ },
};

/*
 * Reset controller and pin multiplexing, RP2040 datasheet sections 2.14 and
 * 2.19. Every peripheral leaves reset held, so a driver releases its own
 * block and waits for the acknowledgement before touching a register.
 */
#define RESETS_BASE	0x4000c000UL
#define RESETS_RESET	0x0
#define RESETS_DONE	0x8
#define RESETS_CLR	0x3000		/* Atomic clear alias. */

#define RESETS_IO_BANK0		(1UL << 5)
#define RESETS_PADS_BANK0	(1UL << 8)
#define RESETS_UART0		(1UL << 22)
#define RESETS_UART1		(1UL << 23)

#define IO_BANK0_BASE	0x40014000UL
#define IO_BANK0_CTRL(n)	(0x004 + 8 * (n))

#define PADS_BANK0_BASE	0x4001c000UL
#define PADS_BANK0_GPIO(n)	(0x04 + 4 * (n))
#define PADS_IE			(1UL << 6)	/* Input enable. */
#define PADS_OD			(1UL << 7)	/* Output disable. */

#define REG32(a)	(*(volatile u_int *)(a))

static void
uart_unreset(u_int mask)
{
    REG32(RESETS_BASE + RESETS_CLR + RESETS_RESET) = mask;
    while ((REG32(RESETS_BASE + RESETS_DONE) & mask) != mask)
	continue;
}

static void
uart_pin_setup(u_int gpio)
{
    /* Drive the pad from the peripheral, and let it read back. */
    REG32(PADS_BANK0_BASE + PADS_BANK0_GPIO(gpio)) =
	(REG32(PADS_BANK0_BASE + PADS_BANK0_GPIO(gpio)) & ~PADS_OD) | PADS_IE;
    REG32(IO_BANK0_BASE + IO_BANK0_CTRL(gpio)) = GPIO_FUNC_UART;
}

/*
 * Set the PL011 divisors. The part divides the peripheral clock by sixteen
 * times the baud rate, keeping the integer part in IBRD and sixty-fourths of
 * the remainder in FBRD, so the division is carried out in one step at
 * 64/16 scale to avoid losing the fraction.
 */
static void
uart_set_baud(u_int base, u_int baud)
{
    u_int div, ibrd, fbrd;

    div = (4 * (BUS_KHZ * 1000)) / baud;	/* 64/16 == 4 */
    ibrd = div >> 6;
    fbrd = div & 0x3f;

    if (ibrd == 0) {
	ibrd = 1;
	fbrd = 0;
    } else if (ibrd >= 65535) {
	ibrd = 65535;
	fbrd = 0;
    }

    REG32(base + UART_IBRD) = ibrd;
    REG32(base + UART_FBRD) = fbrd;
    /* Writing LCR_H latches the divisors. */
    REG32(base + UART_LCR_H) = UART_LCR_H_WLEN_8 | UART_LCR_H_FEN;
}

void
uartinit(int unit)
{
    register const struct uart_inst *uip;

    if (unit < 0 || unit >= NUART)
	return;

    uip = &uart[unit];

    uart_unreset(RESETS_IO_BANK0 | RESETS_PADS_BANK0);
    uart_unreset(unit == 0 ? RESETS_UART0 : RESETS_UART1);

    uart_disable(uip->base);

    uart_set_baud(uip->base, UART_BAUD);
    uart_pin_setup(uip->tx_gpio);
    uart_pin_setup(uip->rx_gpio);

    /* Start from a clean slate: mask and acknowledge everything. */
    uart_irq_disable(uip->base, UART_INT_ALL);
    uart_irq_clear(uip->base, UART_INT_ALL);

    arm_intr_set_priority(uip->irq, IPL_TTY);

    /*
     * The receive interrupt is left masked here and unmasked in uartopen
     * when a process opens the line. Enabling it at boot would storm the
     * handler with framing noise from a floating GP0/GP1 on a board that
     * uses the USB console and never wires the UART, starving the console.
     */
    uart_enable(uip->base);
}
struct tty uartttys[NUART];

void cnstart(struct tty *tp);

/*
 * The RP2040 raises one interrupt per UART. These names match the vector
 * table in rp2040/locore0.S.
 */
void
UART0_IRQ_Handler(void)
{
    uartintr(makedev(UART_MAJOR, 0));
}

void
UART1_IRQ_Handler(void)
{
    uartintr(makedev(UART_MAJOR, 1));
}

int
uartopen(dev_t dev, int flag __unused, int mode __unused)
{
    register const struct uart_inst *uip;
    register struct tty *tp;
    register int unit = minor(dev);

    if (unit < 0 || unit >= NUART)
        return (ENXIO);

    tp = &uartttys[unit];
    if (! tp->t_addr)
        return (ENXIO);

    uip = (struct uart_inst *)tp->t_addr;
    tp->t_oproc = uartstart;
    if ((tp->t_state & TS_ISOPEN) == 0) {
        if (tp->t_ispeed == 0) {
            tp->t_ispeed = BBAUD(UART_BAUD);
            tp->t_ospeed = BBAUD(UART_BAUD);
        }
        ttychars(tp);
        tp->t_state = TS_ISOPEN | TS_CARR_ON;
        tp->t_flags = ECHO | XTABS | CRMOD | CRTBS | CRTERA | CTLECH | CRTKIL;
        /* A serial line carries no window size; open at 80x24, which
         * resize(1) then syncs to the terminal's real size. */
        tp->t_winsize.ws_row = 24;
        tp->t_winsize.ws_col = 80;
    }
    if ((tp->t_state & TS_XCLUDE) && u.u_uid != 0)
        return (EBUSY);

    /*
     * uartinit has already set the divisors and line format from the
     * compile-time baud, so opening the line re-enables the transmitter and
     * receiver and unmasks receive and receive-timeout.
     */
    uart_enable(uip->base);
    /* PL011 raises RX past the FIFO trigger level and the timeout when a
     * partial FIFO sits idle, so the pair catches a burst and a lone key;
     * the NVIC line is enabled here rather than at boot. */
    arm_intr_enable_irq(uip->irq);
    uart_irq_enable(uip->base, UART_INT_RX | UART_INT_RT);

    return ttyopen(dev, tp);
}

/*ARGSUSED*/
int
uartclose(dev_t dev, int flag __unused, int mode __unused)
{
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];

    if (! tp->t_addr)
        return ENODEV;

    ttywflush(tp);
    ttyclose(tp);
    return(0);
}

/*ARGSUSED*/
int
uartread(dev_t dev, struct uio *uio, int flag)
{
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];

    if (! tp->t_addr)
        return ENODEV;

    return ttread(tp, uio, flag);
}

/*ARGSUSED*/
int
uartwrite(dev_t dev, struct uio *uio, int flag)
{
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];

    if (! tp->t_addr)
        return ENODEV;

    return ttwrite(tp, uio, flag);
}

int
uartselect(dev_t dev, int rw)
{
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];

    if (! tp->t_addr)
        return ENODEV;

    return (ttyselect (tp, rw));
}

/*ARGSUSED*/
int
uartioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];
    register int error;

    if (! tp->t_addr)
        return ENODEV;

    error = ttioctl(tp, cmd, addr, flag);
    if (error < 0)
        error = ENOTTY;
    return (error);
}

void
uartintr(dev_t dev)
{
    register int c;
    register int unit = minor(dev);
    register struct tty *tp = &uartttys[unit];
    register const struct uart_inst *uip;

    if (! tp->t_addr)
        return;

    uip = (struct uart_inst *)tp->t_addr;

    /* Receive: drain the FIFO while UARTFR reports it non-empty. */
    while (uart_rx_ready(uip->base)) {
        c = uart_rx_get(uip->base);
        ttyinput(c, tp);
    }

    /*
     * Acknowledge the receive sources through UARTICR. Draining the FIFO
     * drops the RX level interrupt, but PL011 holds the receive-timeout
     * latched until an explicit clear, so the write is what lets a lone
     * keystroke raise the next interrupt.
     */
    uart_irq_clear(uip->base, UART_INT_RX | UART_INT_RT);

    /* Transmit */
    if (uart_tx_ready(uip->base)) {
        led_control(LED_TTY, 0);

        /* Disable transmit interrupt. */
        uart_irq_disable(uip->base, UART_INT_TX);

        if (tp->t_state & TS_BUSY) {
            tp->t_state &= ~TS_BUSY;
            ttstart(tp);
        }
    }
}

/*
 * Start (restart) transmission on the given line.
 */
void
uartstart(struct tty *tp)
{
    register const struct uart_inst *uip;
    register int c, s;

    if (! tp->t_addr)
        return;

    uip = (struct uart_inst *)tp->t_addr;

    /*
     * Must hold interrupts in following code to prevent
     * state of the tp from changing.
     */
    s = spltty();
    /*
     * If it is currently active, or delaying, no need to do anything.
     */
    if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
out:
        led_control(LED_TTY, 0);
        splx(s);
        return;
    }

    /*
     * Wake up any sleepers.
     */
    ttyowake(tp);

    /*
     * Now restart transmission unless the output queue is empty.
     */
    if (tp->t_outq.c_cc == 0)
        goto out;

    if (uart_tx_ready(uip->base)) {
        c = getc(&tp->t_outq);
        uart_tx_put(uip->base, c & 0xff);
        tp->t_state |= TS_BUSY;
    }

    /* Enable transmit interrupt. */
    uart_irq_enable(uip->base, UART_INT_TX);

    led_control(LED_TTY, 1);
    splx(s);
}

void
uartputc(dev_t dev, char c)
{
    int unit = minor(dev);
    struct tty *tp = &uartttys[unit];
    register const struct uart_inst *uip = &uart[unit];
    register int s, timo;

    s = spltty();
again:
    /*
     * Try waiting for the console tty to come ready,
     * otherwise give up after a reasonable time.
     */
    timo = 30000;
    while (!uart_tx_ready(uip->base))
        if (--timo == 0)
            break;

    if (tp->t_state & TS_BUSY) {
        uartintr(dev);
        goto again;
    }
    led_control(LED_TTY, 1);
    uart_tx_put(uip->base, c);

    timo = 30000;
    while (!uart_tx_idle(uip->base))
        if (--timo == 0)
            break;

    led_control(LED_TTY, 0);
    splx(s);
}

char
uartgetc(dev_t dev)
{
    int unit = minor(dev);
    register const struct uart_inst *uip = &uart[unit];
    int s, c;

    s = spltty();
    for (;;) {
        /* Wait for key pressed. */
        if (uart_rx_ready(uip->base)) {
            c = uart_rx_get(uip->base);
            break;
        }
    }

    splx(s);
    return (unsigned char) c;
}

/*
 * Test to see if device is present.
 * Return true if found and initialized ok.
 */
static int
uartprobe(struct conf_device *config)
{
    /*
     * The RP2040's blocks are UART0 and UART1 and uart[] is indexed the
     * same way, so "device uart0" in the configuration names index 0. The
     * ST ports count their USARTs from one and subtract here; carrying that
     * subtraction over left every unit one below its own index, so unit 0
     * failed the range test below and the line never attached.
     */
    int unit = config->dev_unit;
    int is_console = (CONS_MAJOR == UART_MAJOR &&
                      CONS_MINOR == unit);

    if (unit < 0 || unit >= NUART)
        return 0;

    /*
     * The ST targets name a port letter, a pin number, and an alternate
     * function. Every RP2040 pin is GPIO n and the UART is always function
     * 2, so the line reports the pin pair alone.
     */
    printf("uart%d: pins tx=GP%d/rx=GP%d", unit,
        uart[unit].tx_gpio, uart[unit].rx_gpio);

    if (is_console)
        printf(", console");
    printf("\n");

    /* Initialize the device. */
    uartttys[unit].t_addr = (caddr_t) &uart[unit];
    if (! is_console)
        uartinit(unit);

    return 1;
}

struct driver uartdriver = {
    "uart", uartprobe,
};
