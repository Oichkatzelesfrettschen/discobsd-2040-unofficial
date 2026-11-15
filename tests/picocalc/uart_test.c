/*
 * Copyright (c) 2026 First Principalists
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
 * Host test of the RP2040 UART's transmit path (sys/arch/rp2040/dev/uart.c,
 * included unchanged with UART_HOST_MODEL). The model is the PL011 as the
 * RP2040 datasheet 4.2.6.3 states it: a 32-byte transmit FIFO, the trigger
 * TXIFLSEL at its reset value, half full (16 bytes), and a transmit
 * interrupt that asserts only when the FIFO level falls through the
 * trigger, clears when writes lift the level above it or ICR clears it,
 * and reaches uartintr() only while IMSC unmasks it. The wire takes one
 * byte per tick. Each case queues output, calls the line's start routine
 * and then ticks with no received byte at all, which is how a login
 * prompt or a program's output reaches an idle terminal; every byte must
 * reach the wire in order. A driver that feeds the FIFO one byte per start
 * leaves the level below the trigger, gets no interrupt and stops after the
 * first byte, which is what the PicoCalc's USB-C console showed.
 */

/* The hardware paths of uartinit() only, which this test never calls. */
static volatile unsigned shim_reg_sink;
#define	REG32(a)	(*((void)(a), &shim_reg_sink))

#include "uart.c"

#include <stdlib.h>
#include <string.h>

#define	FIFO		32
#define	TRIGGER		16	/* UART_IFLS_TX_HALF, which uartinit() programs. */
#define	MAXBYTES	4096

struct shim_user u;

static unsigned char txfifo[FIFO];
static int txlev, ris, imsc, enabled;
static unsigned char wire[MAXBYTES];
static int nwire;
static unsigned char queue[MAXBYTES];
static int qhead, qtail;
static int interrupts, owakes, failures;

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
	return txlev < FIFO;
}

int
uart_tx_idle(u_int base)
{
	(void)base;
	return txlev == 0;
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

/* One byte leaves the FIFO; a fall through the trigger asserts TX. */
static void
tick(void)
{
	int before = txlev;

	if (txlev == 0)
		return;
	if (nwire < MAXBYTES)
		wire[nwire++] = txfifo[0];
	memmove(txfifo, txfifo + 1, (size_t)(txlev - 1));
	txlev--;
	if (before > TRIGGER && txlev <= TRIGGER)
		ris |= UART_INT_TX;
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

void
ttyowake(struct tty *tp)
{
	(void)tp;
	owakes++;
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
	return tp;
}

static void
queue_bytes(struct tty *tp, const unsigned char *p, int n)
{
	memcpy(queue + qtail, p, (size_t)n);
	qtail += n;
	tp->t_outq.c_cc += n;
}

/*
 * Run the wire with no received byte: tick, and enter uartintr() whenever
 * the model's masked status is set, as the NVIC would.
 */
static void
run_idle(int ticks)
{
	while (ticks-- > 0) {
		tick();
		if (uart_irq_status(0) != 0) {
			interrupts++;
			uartintr(makedev(UART_MAJOR, 0));
		}
	}
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
	snprintf(what, sizeof(what), "%s: the transmit interrupt ends masked",
	    name);
	check((imsc & UART_INT_TX) == 0, what);
	printf("%s: %d bytes, %d interrupts, %d wakeups\n", name, nwire,
	    interrupts, owakes);
}

int
main(void)
{
	static unsigned char big[1000];
	struct tty *tp;
	int i;

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

	/* A stopped line (^S) sends nothing until it restarts. */
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

	if (failures) {
		printf("uart_test: %d failures\n", failures);
		return 1;
	}
	printf("uart_test: transmit path passes\n");
	return 0;
}
