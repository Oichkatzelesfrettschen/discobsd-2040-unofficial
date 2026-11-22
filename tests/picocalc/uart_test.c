/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Host test of the RP2040 UART's transmit path (sys/arch/rp2040/dev/uart.c,
 * included unchanged with UART_HOST_MODEL). The model is the PL011 as the
 * RP2040 datasheet 4.2.6.3 and 4.2.8 state it: a 32-byte transmit FIFO
 * feeding a shift register, the trigger TXIFLSEL at its reset value, half
 * full (16 bytes), and a transmit interrupt that asserts only when the FIFO
 * level falls through the trigger, clears when writes lift the level above
 * it or ICR clears it, and reaches uartintr() only while IMSC unmasks it.
 * UARTFR's TXFE reads an empty FIFO and BUSY a non-empty FIFO or a byte in
 * the shift register. One step is one character time: the byte in the
 * shift register reaches the wire and the FIFO's head moves into it. Every
 * CLOCK steps a kernel tick runs the pending callout, as softclock() does
 * (HZ 1000 against 86.8 us per character at 115200 baud).
 *
 * The output cases queue bytes, call the line's start routine and step
 * with no received byte at all, which is how a login prompt or a program's
 * output reaches an idle terminal; every byte must reach the wire in order.
 * A driver that feeds the FIFO one byte per start leaves the level below
 * the trigger, never raises an interrupt and stops after the first byte, which is
 * what the PicoCalc's USB-C console showed. The drain cases run tty.c's
 * ttywait() loop against the model: it must return only once the FIFO and
 * the shift register are empty, through the wakeup of the drain callout,
 * and TS_BUSY must stay set at every step until then.
 *
 * The test serializes the driver against its interrupt and callout: spltty()
 * is a no-op here, and uartintr() and the callout run only between steps.
 * It establishes the transmit state machine, not interrupt safety. The
 * start routine, uartputc() and uartdrain() rely on spltty() being a global
 * PRIMASK disable on this port (sys/arch/rp2040/include/intr.h); uartintr()
 * runs without it and SysTick can preempt it with uartdrain(), a case the
 * comment above uartstart() argues from the flags and this test never
 * enters. Apart from the pad case, which calls uart_pin_setup() on two
 * modeled pad registers, it leaves out uartinit(), the receive path, interrupt
 * nesting and hardware. Time advances only in step(), so a driver loop that polls the
 * line without it ends by a bound on the register reads, not by a clock.
 */

/*
 * Register accesses come from uartinit(), which this test never calls, and
 * from uart_pin_setup(), which the pad case calls directly. The two pad
 * control registers of the case keep their value; every other address
 * shares one sink.
 */
#define	SHIM_PADS_BASE	0x4001c000u	/* PADS_BANK0, datasheet 2.19.6.3. */
#define	SHIM_PAD(n)	(SHIM_PADS_BASE + 0x04u + 4u * (n))
#define	SHIM_PAD_RESET	0x56u		/* IE, 4 mA drive, PDE, Schmitt. */
#define	SHIM_LCR_H	0x4003402cu	/* UART0 UARTLCR_H, datasheet 4.2.8. */
static volatile unsigned shim_reg_sink, shim_pad[2], shim_lcr_h;

static volatile unsigned *
shim_reg(unsigned a)
{
	if (a == SHIM_PAD(0) || a == SHIM_PAD(1))
		return &shim_pad[(a - SHIM_PAD(0)) / 4];
	if (a == SHIM_LCR_H)
		return &shim_lcr_h;
	return &shim_reg_sink;
}
#define	REG32(a)	(*shim_reg((unsigned)(a)))

#include "uart.c"
#ifdef PICOCALC_ENABLED
#include "psram_pio.h"
#endif

#include <stdlib.h>
#include <string.h>

#define	FIFO		32
#define	TRIGGER		16	/* UART_IFLS_TX_HALF, which uartinit() programs. */
#define	MAXBYTES	4096
#define	CLOCK		11	/* Character times per kernel tick. */

struct shim_user u;

static unsigned char txfifo[FIFO];
static int txlev, ris, imsc, enabled;
static int shifting;
static unsigned char shiftreg;
static int steps;
static void (*co_fun)(caddr_t);
static caddr_t co_arg;
static int co_ticks, co_pending, co_runs;
static int asleep;
static unsigned char wire[MAXBYTES];
static int nwire;
static unsigned char queue[MAXBYTES];
static int qhead, qtail;
static int interrupts, owakes, failures;

/*
 * Register reads since the line last advanced. The model's time moves only
 * in step(), so a driver loop that polls UARTFR without a step ending it
 * (uartputc() waiting out a drain it cannot finish under PRIMASK) never
 * returns. A bound on the reads ends that loop by count, which load on the
 * host cannot change. The longest legitimate wait is two 30000-read polls
 * of uartputc(), once per pass through its retry.
 */
#define	POLL_BOUND	1000000
static long polls;

static void
poll_account(void)
{
	if (++polls > POLL_BOUND) {
		printf("FAIL the driver polls the line %d times without it advancing\n",
		    POLL_BOUND);
		exit(1);
	}
}

static void
check(int cond, const char *what)
{
	if (!cond) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* The PL011 helpers of uart.h, as the model. */
int
uart_tx_ready(u_int base)
{
	(void)base;
	poll_account();
	return txlev < FIFO;
}

/* BUSY clear. */
int
uart_tx_idle(u_int base)
{
	(void)base;
	poll_account();
	return txlev == 0 && !shifting;
}

/* TXFE set and BUSY clear. */
int
uart_tx_drained(u_int base)
{
	(void)base;
	poll_account();
	return txlev == 0 && !shifting;
}

int
uart_rx_ready(u_int base)
{
	(void)base;
	return 0;
}

void
uart_tx_put(u_int base, int c)
{
	(void)base;
	if (txlev >= FIFO) {
		printf("FAIL write to a full transmit FIFO\n");
		exit(1);
	}
	txfifo[txlev++] = (unsigned char)c;
	if (txlev > TRIGGER)
		ris &= ~UART_INT_TX;
}

int
uart_rx_get(u_int base)
{
	(void)base;
	return 0;
}

void
uart_enable(u_int base)
{
	(void)base;
	enabled = 1;
}

void
uart_disable(u_int base)
{
	(void)base;
	enabled = 0;
}

void
uart_irq_enable(u_int base, u_int mask)
{
	(void)base;
	imsc |= (int)mask;
}

void
uart_irq_disable(u_int base, u_int mask)
{
	(void)base;
	imsc &= ~(int)mask;
}

u_int
uart_irq_status(u_int base)
{
	(void)base;
	return (u_int)(ris & imsc);
}

void
uart_irq_clear(u_int base, u_int mask)
{
	(void)base;
	ris &= ~(int)mask;
}

/*
 * One character time: the shift register's byte reaches the wire and the
 * FIFO's head moves into it; a fall through the trigger asserts TX.
 */
static void
tick(void)
{
	int before = txlev;

	polls = 0;

	if (shifting) {
		if (nwire < MAXBYTES)
			wire[nwire++] = shiftreg;
		shifting = 0;
	}
	if (txlev == 0)
		return;
	shiftreg = txfifo[0];
	shifting = 1;
	memmove(txfifo, txfifo + 1, (size_t)(txlev - 1));
	txlev--;
	if (before > TRIGGER && txlev <= TRIGGER)
		ris |= UART_INT_TX;
}

/* The kernel's callout list, held to the one entry the driver may own. */
void
timeout(void (*fun)(caddr_t), caddr_t arg, int t)
{
	if (co_pending) {
		printf("FAIL a second callout while one is pending\n");
		exit(1);
	}
	co_fun = fun;
	co_arg = arg;
	co_ticks = t;
	co_pending = 1;
}

void
untimeout(void (*fun)(caddr_t), caddr_t arg)
{
	if (co_pending && co_fun == fun && co_arg == arg)
		co_pending = 0;
}

/* The tty layer the driver calls. */
int
shim_tty_getc(struct clist *q)
{
	if (q->c_cc <= 0 || qhead == qtail)
		return -1;
	q->c_cc--;
	return queue[qhead++];
}

void
ttstart(struct tty *tp)
{
	if (tp->t_oproc != NULL)
		(*tp->t_oproc)(tp);
}

/* tty.c wakes a ttywait() sleeper once t_outq is at its low mark. */
void
ttyowake(struct tty *tp)
{
	owakes++;
	if (asleep && tp->t_outq.c_cc == 0)
		asleep = 0;
}

void ttyinput(int c, struct tty *tp) { (void)c; (void)tp; }
void ttychars(struct tty *tp) { (void)tp; }
int ttyopen(dev_t dev, struct tty *tp) { (void)dev; (void)tp; return 0; }
int ttyclose(struct tty *tp) { (void)tp; return 0; }
void ttywflush(struct tty *tp) { (void)tp; }
int ttread(struct tty *tp, struct uio *uio, int flag)
{ (void)tp; (void)uio; (void)flag; return 0; }
int ttwrite(struct tty *tp, struct uio *uio, int flag)
{ (void)tp; (void)uio; (void)flag; return 0; }
int ttioctl(struct tty *tp, u_int cmd, caddr_t addr, int flag)
{ (void)tp; (void)cmd; (void)addr; (void)flag; return -1; }
int ttyselect(struct tty *tp, int rw) { (void)tp; (void)rw; return 0; }
void led_control(int mask, int on) { (void)mask; (void)on; }
int spltty(void) { return 0; }
void splx(int s) { (void)s; }
void arm_intr_set_priority(int irq, int ipl) { (void)irq; (void)ipl; }
void arm_intr_enable_irq(int irq) { (void)irq; }
void board_console(int *major, int *minor)
{ *major = UART_MAJOR; *minor = 0; }

static struct tty *
line(void)
{
	struct tty *tp = &uartttys[0];

	memset(tp, 0, sizeof(*tp));
	tp->t_addr = (caddr_t)&uart[0];
	tp->t_oproc = uartstart;
	tp->t_state = TS_ISOPEN | TS_CARR_ON;
	txlev = ris = imsc = nwire = qhead = qtail = interrupts = owakes = 0;
	shifting = steps = co_pending = co_runs = asleep = 0;
	memset(uart_txflags, 0, sizeof(uart_txflags));
	return tp;
}

static int
line_drained(void)
{
	return txlev == 0 && !shifting;
}

static void
queue_bytes(struct tty *tp, const unsigned char *p, int n)
{
	memcpy(queue + qtail, p, (size_t)n);
	qtail += n;
	tp->t_outq.c_cc += n;
}

/*
 * One step with no received byte: a character time, uartintr() whenever
 * the model's masked status is set, as the NVIC would enter it, and the
 * pending callout on each kernel tick.
 */
static void
step(void)
{
	tick();
	if (uart_irq_status(0) != 0) {
		interrupts++;
		uartintr(makedev(UART_MAJOR, 0));
	}
	if (++steps % CLOCK == 0 && co_pending && --co_ticks <= 0) {
		co_pending = 0;
		co_runs++;
		(*co_fun)(co_arg);
	}
}

static void
run_idle(int n)
{
	while (n-- > 0)
		step();
}

/*
 * tty.c's ttywait(): call the start routine and sleep until ttyowake() while
 * t_outq holds bytes or TS_BUSY is set. Returns the steps slept, or -1 when
 * no wakeup came within the bound. *early counts the steps at which TS_BUSY
 * read clear while the FIFO or shift register still held a byte.
 */
static int
drain_wait(struct tty *tp, int bound, int *early)
{
	int slept = 0;

	*early = 0;
	while (tp->t_outq.c_cc != 0 || (tp->t_state & TS_BUSY)) {
		(*tp->t_oproc)(tp);
		if (tp->t_outq.c_cc == 0 && (tp->t_state & TS_BUSY) == 0)
			break;
		asleep = 1;
		while (asleep) {
			if (slept++ >= bound)
				return -1;
			step();
			if ((tp->t_state & TS_BUSY) == 0 && !line_drained())
				(*early)++;
		}
	}
	return slept;
}

static void
case_text(const char *name, const unsigned char *p, int n)
{
	struct tty *tp = line();
	char what[96];

	queue_bytes(tp, p, n);
	ttstart(tp);
	run_idle(4 * n + 64);
	snprintf(what, sizeof(what), "%s: %d of %d bytes reach the wire", name,
	    nwire, n);
	check(nwire == n, what);
	snprintf(what, sizeof(what), "%s: the bytes arrive in order", name);
	check(nwire == n && memcmp(wire, p, (size_t)n) == 0, what);
	snprintf(what, sizeof(what), "%s: the queue and TS_BUSY end clear",
	    name);
	check(tp->t_outq.c_cc == 0 && (tp->t_state & TS_BUSY) == 0, what);
	snprintf(what, sizeof(what), "%s: no callout is left pending", name);
	check(co_pending == 0, what);
	snprintf(what, sizeof(what), "%s: the transmit interrupt ends masked",
	    name);
	check((imsc & UART_INT_TX) == 0, what);
	printf("%s: %d bytes, %d interrupts, %d wakeups\n", name, nwire,
	    interrupts, owakes);
}

/*
 * Queue n bytes, start the line and drain it at once, as close(2) and a
 * TIOCSETP do through ttywait(): the wait ends only after the last byte
 * leaves the shift register, TS_BUSY is set at every step until then, and
 * the drain callout's wakeup ends the sleep with no received byte.
 */
static void
case_drain(const char *name, const unsigned char *p, int n)
{
	struct tty *tp = line();
	char what[96];
	int slept, early;

	queue_bytes(tp, p, n);
	ttstart(tp);
	check((tp->t_state & TS_BUSY) != 0,
	    "drain: TS_BUSY is set while bytes remain to send");
	slept = drain_wait(tp, 8 * n + 64, &early);
	snprintf(what, sizeof(what), "%s: the drain wait is woken", name);
	check(slept >= 0, what);
	snprintf(what, sizeof(what),
	    "%s: TS_BUSY never clears with a byte unsent (%d steps)", name,
	    early);
	check(early == 0, what);
	snprintf(what, sizeof(what),
	    "%s: the wait returns with the FIFO and shifter empty", name);
	check(line_drained() && nwire == n && memcmp(wire, p, (size_t)n) == 0,
	    what);
	snprintf(what, sizeof(what), "%s: TS_BUSY, IMSC and the callout end clear",
	    name);
	check((tp->t_state & TS_BUSY) == 0 && (imsc & UART_INT_TX) == 0 &&
	    co_pending == 0 && (uart_txflags[0] & UART_TX_DRAIN) == 0, what);
	printf("%s: %d bytes, %d steps asleep, %d callouts, %d interrupts\n",
	    name, nwire, slept, co_runs, interrupts);
}

/*
 * TIOCSBRK and TIOCCBRK hold the line's TX low through UARTLCR_H's BRK bit
 * and release it, leaving the word length and FIFO enable alone; a kernel
 * whose flash lacks room for them (the PICOCALC build) answers ENOTTY,
 * as every other request the driver does not own does.
 */
static void
case_break(void)
{
	dev_t dev = makedev(UART_MAJOR, 0);
	unsigned line_control = UART_LCR_H_WLEN_8 | UART_LCR_H_FEN;

	(void)line();
	shim_lcr_h = line_control;
#if !defined(PICOCALC_ENABLED) || defined(PICOCALC_SD)
	check(uartioctl(dev, TIOCSBRK, 0, 0) == 0 &&
	    shim_lcr_h == (line_control | UART_LCR_H_BRK),
	    "TIOCSBRK sets BRK and keeps the line control");
	check(uartioctl(dev, TIOCSBRK, 0, 0) == 0 &&
	    shim_lcr_h == (line_control | UART_LCR_H_BRK),
	    "a second TIOCSBRK changes nothing");
	check(uartioctl(dev, TIOCCBRK, 0, 0) == 0 && shim_lcr_h == line_control,
	    "TIOCCBRK clears BRK and keeps the line control");
	check(uartioctl(dev, TIOCCBRK, 0, 0) == 0 && shim_lcr_h == line_control,
	    "a TIOCCBRK with no break leaves the line control");
#else
	check(uartioctl(dev, TIOCSBRK, 0, 0) == ENOTTY &&
	    shim_lcr_h == line_control, "TIOCSBRK on a PICOCALC kernel is ENOTTY");
	check(uartioctl(dev, TIOCCBRK, 0, 0) == ENOTTY &&
	    shim_lcr_h == line_control, "TIOCCBRK on a PICOCALC kernel is ENOTTY");
#endif
	check(uartioctl(dev, TIOCSBRK + 1, 0, 0) == ENOTTY &&
	    shim_lcr_h == line_control, "another request leaves UARTLCR_H alone");
	/* A line that is not open on a UART lacks a register to drive. */
	uartttys[0].t_addr = 0;
	check(uartioctl(dev, TIOCSBRK, 0, 0) == ENODEV &&
	    shim_lcr_h == line_control, "TIOCSBRK without a line is ENODEV");
}

int
main(void)
{
	static unsigned char big[1000];
	struct tty *tp;
	int i, early;

	for (i = 0; i < (int)sizeof(big); i++)
		big[i] = (unsigned char)('a' + i % 26);

	case_text("one byte", (const unsigned char *)"x", 1);
	case_text("login prompt", (const unsigned char *)"login: ", 7);
	case_text("exactly the FIFO", big, FIFO);
	case_text("FIFO plus one", big, FIFO + 1);
	case_text("1000 bytes", big, (int)sizeof(big));

	/* Output appended while the line is busy goes out behind the rest. */
	tp = line();
	queue_bytes(tp, big, 100);
	ttstart(tp);
	run_idle(10);
	queue_bytes(tp, (const unsigned char *)"tail", 4);
	ttstart(tp);
	run_idle(600);
	check(nwire == 104 && memcmp(wire, big, 100) == 0 &&
	    memcmp(wire + 100, "tail", 4) == 0,
	    "appended output follows the earlier bytes");

	/* A stopped line (^S) sends zero bytes until it restarts. */
	tp = line();
	tp->t_state |= TS_TTSTOP;
	queue_bytes(tp, big, 10);
	ttstart(tp);
	run_idle(100);
	check(nwire == 0, "a stopped line sends nothing");
	tp->t_state &= ~TS_TTSTOP;
	ttstart(tp);
	run_idle(100);
	check(nwire == 10, "the restarted line sends its queue");

	case_drain("drain 1 byte", big, 1);
	case_drain("drain 7 bytes", big, 7);
	case_drain("drain 32 bytes", big, FIFO);
	case_drain("drain 100 bytes", big, 100);

	/*
	 * Output queued while the FIFO drains goes out at once, before the
	 * next kernel tick, behind the earlier bytes, and adds zero callouts.
	 */
	tp = line();
	queue_bytes(tp, big, 3);
	ttstart(tp);
	run_idle(1);
	queue_bytes(tp, (const unsigned char *)"tail", 4);
	ttstart(tp);
	run_idle(7);
	check(steps < CLOCK && nwire == 7 && memcmp(wire, big, 3) == 0 &&
	    memcmp(wire + 3, "tail", 4) == 0,
	    "output appended during the drain leaves before the next tick");
	check(drain_wait(tp, 64, &early) >= 0 && early == 0 && line_drained(),
	    "the appended output drains");

	/*
	 * The console putc during the drain writes behind the queued bytes and
	 * returns; the drain still ends through the callout.
	 */
	tp = line();
	queue_bytes(tp, big, 3);
	ttstart(tp);
	run_idle(1);
	uartputc(makedev(UART_MAJOR, 0), '!');
	check(drain_wait(tp, 64, &early) >= 0 && early == 0 && nwire == 4 &&
	    memcmp(wire, big, 3) == 0 && wire[3] == '!',
	    "uartputc() during the drain follows the queued bytes");

	check(UART0_BASE + UART_LCR_H == SHIM_LCR_H, "the model's UARTLCR_H address");
	case_break();

	/*
	 * Pads from their reset value: receive trades the pull-down for the
	 * pull-up so that an unconnected pin idles high instead of reading
	 * as a break; transmit keeps its pulls; both read back and drive.
	 */
	check(PADS_BANK0_BASE == SHIM_PADS_BASE, "the model's pad base");
	shim_pad[0] = shim_pad[1] = SHIM_PAD_RESET | PADS_OD;
	uart_pin_setup(0, 0);
	uart_pin_setup(1, 1);
	check(shim_pad[0] == SHIM_PAD_RESET, "transmit pad keeps its pulls");
	check(shim_pad[1] == ((SHIM_PAD_RESET & ~PADS_PDE) | PADS_PUE),
	    "receive pad takes the pull-up for the pull-down");

	/*
	 * UART1's pins. A PicoCalc build puts them on GP8 and GP9, the keyboard
	 * controller's USART3 pair, and none of them may fall on a PSRAM line:
	 * SIO0-SIO3 are four consecutive pins from PSRAM_PIN_SIO0, beside the
	 * chip select and the clock. Every other board keeps GP4 and GP5.
	 */
#if NUART > 1
#ifdef PICOCALC_ENABLED
	check(uart[1].tx_gpio == 8 && uart[1].rx_gpio == 9,
	    "UART1 pins on a PicoCalc are GP8 and GP9");
	{
		unsigned pin, bad = 0;

		for (pin = 0; pin < 2; pin++) {
			unsigned g = pin ? uart[1].rx_gpio : uart[1].tx_gpio;

			if ((g >= PSRAM_PIN_SIO0 && g < PSRAM_PIN_SIO0 + 4) ||
			    g == PSRAM_PIN_CS || g == PSRAM_PIN_SCK)
				bad++;
		}
		check(bad == 0, "UART1 pins avoid the PSRAM lines");
	}
#else
	check(uart[1].tx_gpio == 4 && uart[1].rx_gpio == 5,
	    "UART1 pins off a PicoCalc are GP4 and GP5");
#endif
#endif

	if (failures) {
		printf("uart_test: %d failures\n", failures);
		return 1;
	}
	printf("uart_test: transmit path passes\n");
	return 0;
}
