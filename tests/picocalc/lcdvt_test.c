/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Host test of the PicoCalc second console (options LCD_VT): lcdcons.c, the
 * real terminal lcdterm.c and the key decision of picokbd_map.c, linked
 * against a model panel and the tty model of tests/picocalc/shim-vt.
 *
 * The model panel records what the panel primitives draw, as lcdterm_test.c
 * does. The tty model keeps each tty's output queue as bytes, runs the
 * start routine from ttstart() and implements ttwrite()'s high-water sleep,
 * TIOCSTOP and TIOCSTART as sys/kern/tty.c does; the render interrupt is
 * entered by hand when the model's NVIC pend register is set, which is what
 * the hardware does after lcdstart() pends IRQ 26. The test compares whole
 * screen states of the two consoles (lcdterm_state(), LCDTERM_HOST_TEST)
 * byte for byte, so a field the exchange forgets fails it.
 *
 * Each scenario runs in a forked child, because panic and fault abandon the
 * drawing flag for good: "main" covers switching, freezing, flow control and
 * the marker; "panic" and "fault" end with ttyv0's text on the panel while
 * ttyv1 was shown. The scenarios share no state.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <machine/intr.h>
#include <machine/board.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/lcdterm.h>
#include <rp2040/dev/lcdgfx.h>
#include <rp2040/dev/picokbd_evq.h>
#include <rp2040/dev/picokbd_map.h>

#define	NVIC_ISPR	0xe000e200UL
#define	HIWAT		336		/* TTHIWAT at 115200 baud (tty.c). */
#define	CHUNK		100		/* ttwrite()'s OBUFSIZ. */
#define	DEV0		makedev(LCDCONS_MAJOR, 0)
#define	DEV1		makedev(LCDCONS_MAJOR, LCDCONS_VT1_MINOR)

#define	CHECK(cond, label) check((cond) != 0, (label), __LINE__)

static int failures;

static void
check(int ok, const char *label, int ln)
{
	if (!ok) {
		fprintf(stderr, "FAIL line %d: %s\n", ln, label);
		failures++;
	}
}

/* Kernel globals and the register model. */
struct shim_user u;
const char *panicstr;

static struct { unsigned long addr; unsigned val; } regs[16];

volatile unsigned *
shim_vt_reg(unsigned long addr)
{
	unsigned i;

	for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
		if (regs[i].addr == addr)
			return &regs[i].val;
		if (regs[i].addr == 0) {
			regs[i].addr = addr;
			return &regs[i].val;
		}
	}
	fprintf(stderr, "register model full\n");
	exit(2);
}

static int spl_level;

int
splhigh(void)
{
	int old = spl_level;

	spl_level = 2;
	return old;
}

int
spltty(void)
{
	int old = spl_level;

	if (spl_level < 1)
		spl_level = 1;
	return old;
}

void
splx(int s)
{
	spl_level = s;
}

void arm_intr_set_priority(int irq __unused, int ipl __unused) { }
void arm_intr_enable_irq(int irq __unused) { }
int board_id(void) { return BOARD_ID_PICOCALC; }
void board_console(int *major, int *minor) { *major = LCDCONS_MAJOR; *minor = 0; }

/* Model panel: one character and attribute per physical cell. */
static unsigned char panel_ch[LT_ROWS][LT_COLS];
static unsigned char panel_at[LT_ROWS][LT_COLS];
static int panel_top;
static int ndraws, nfills, ntops;

void
lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{
	if (prow < 0 || prow >= LT_ROWS || col < 0 || col >= LT_COLS) {
		fprintf(stderr, "draw outside panel: %d,%d\n", prow, col);
		exit(2);
	}
	ndraws++;
	panel_ch[prow][col] = (unsigned char)ch;
	panel_at[prow][col] = (unsigned char)attr;
}

void
lcd_fill_cells(int prow, int col, int n, unsigned attr)
{
	if (prow < 0 || prow >= LT_ROWS || col < 0 || n < 0 ||
	    col + n > LT_COLS) {
		fprintf(stderr, "fill outside panel: %d,%d+%d\n", prow, col, n);
		exit(2);
	}
	nfills++;
	while (n-- > 0) {
		panel_ch[prow][col] = ' ';
		panel_at[prow][col++] = (unsigned char)attr;
	}
}

void lcd_set_top(int prow) { ntops++; panel_top = prow; }
void lcd_gfx_reset(void) { }
void lcd_bell(void) { }

static int nabort;
void st7365p_init(void) { }
void st7365p_display_on(void) { }
void st7365p_abort(void) { nabort++; }
void picokbd_init(void) { }
void picokbd_poll_start(void) { }
int picokbd_getc(void) { return 0; }
int picokbd_version(void) { return -1; }

int
picokbd_evq_start(const void *file __unused, const void *proc __unused,
    int pid __unused, int pgrp __unused, int tty_pgrp __unused,
    unsigned tty_flags __unused, int readable __unused)
{
	return 0;
}

int
picokbd_evq_stop(const void *file __unused, const void *proc __unused,
    int pid __unused)
{
	return 0;
}

int
picokbd_evq_drain(struct lcd_keyevs *out __unused, const void *proc __unused,
    int pid __unused, int pgrp __unused, int tty_pgrp __unused,
    unsigned tty_flags __unused, int readable __unused)
{
	return 0;
}

void picokbd_evq_fileclose(const void *file __unused) { }
void picokbd_evq_procexit(const void *proc __unused, int pid __unused) { }
void picokbd_evq_check(int tty_pgrp __unused, unsigned tty_flags __unused) { }
void picokbd_evq_modeset(const void *proc __unused, int pid __unused) { }

/* The pixel device: only whether a program holds the panel matters here. */
static int gfx_open;
int lcdgfx_open(void) { gfx_open = 1; return 0; }
int lcdgfx_close(void) { gfx_open = 0; return 0; }
int lcdgfx_write(struct uio *uio __unused) { return 0; }
int lcdgfx_ioctl(unsigned cmd __unused, char *addr __unused) { return 0; }
int lcdgfx_isopen(void) { return gfx_open; }

/*
 * The tty model. Calls that sleep, wake, arm a callout or deliver input are
 * counted so that the panic path can be held to making none.
 */
static int nttstart, nowake, nwoke, ntimeout, ninput, nsleeps, nblocked;
static void (*timeout_fn[8])(caddr_t);
static int ntimeout_pending;

void
timeout(void (*fun)(caddr_t), caddr_t arg __unused, int ticks __unused)
{
	ntimeout++;
	if (ntimeout_pending < 8)
		timeout_fn[ntimeout_pending++] = fun;
}

int
shim_vt_getc(struct clist *q)
{
	int c;

	if (q->c_cc == 0)
		return -1;
	c = q->c_buf[q->c_head];
	q->c_head = (q->c_head + 1) % (int)sizeof(q->c_buf);
	q->c_cc--;
	return c;
}

static void
q_put(struct clist *q, int c)
{
	if (q->c_cc >= (int)sizeof(q->c_buf)) {
		fprintf(stderr, "tty model: output queue overflow\n");
		exit(2);
	}
	q->c_buf[(q->c_head + q->c_cc++) % (int)sizeof(q->c_buf)] =
	    (unsigned char)c;
}

void ttyinput(int c __unused, struct tty *tp __unused) { ninput++; }

void
ttyowake(struct tty *tp)
{
	nowake++;
	if (tp->t_state & TS_ASLEEP) {
		tp->t_state &= ~TS_ASLEEP;
		nwoke++;
	}
}

void
ttstart(struct tty *tp)
{
	nttstart++;
	if (tp->t_oproc)
		(*tp->t_oproc)(tp);
}

void ttychars(struct tty *tp __unused) { }
int ttyopen(dev_t dev, struct tty *tp) { tp->t_dev = dev; return 0; }

void
ttyflush(struct tty *tp, int rw)
{
	if (rw & FWRITE) {
		tp->t_outq.c_cc = 0;
		tp->t_outq.c_head = 0;
		tp->t_state &= ~TS_TTSTOP;
	}
}

void
ttyclose(struct tty *tp)
{
	ttyflush(tp, FREAD | FWRITE);
	tp->t_pgrp = 0;
	tp->t_state = 0;
}

/* ttywait() sleeps while output is queued; the model counts the sleep. */
void
ttywflush(struct tty *tp)
{
	if (tp->t_outq.c_cc > 0 || (tp->t_state & TS_BUSY))
		nblocked++;
	else
		ttyflush(tp, FREAD);
}

int ttread(struct tty *tp __unused, struct uio *uio __unused, int flag __unused)
{
	return 0;
}

int ttyselect(struct tty *tp __unused, int rw __unused) { return 0; }

struct tuio {
	const char	*p;
	int		n;
};

/* ttwrite(): a chunk at a time, ttstart after each, sleep above TTHIWAT. */
int
ttwrite(struct tty *tp, struct uio *uio, int flag __unused)
{
	struct tuio *t = (struct tuio *)uio;
	int n;

	while (t->n > 0) {
		if (tp->t_outq.c_cc > HIWAT) {
			ttstart(tp);
			if (tp->t_outq.c_cc > HIWAT) {
				tp->t_state |= TS_ASLEEP;
				nsleeps++;
				return 0;
			}
		}
		for (n = 0; n < CHUNK && t->n > 0; n++, t->n--)
			q_put(&tp->t_outq, (unsigned char)*t->p++);
		ttstart(tp);
	}
	return 0;
}

int
ttioctl(struct tty *tp, u_int cmd, caddr_t addr __unused, int flag __unused)
{
	switch (cmd) {
	case TIOCSTOP:
		tp->t_state |= TS_TTSTOP;
		return 0;
	case TIOCSTART:
		if (tp->t_state & TS_TTSTOP) {
			tp->t_state &= ~TS_TTSTOP;
			ttstart(tp);
		}
		return 0;
	default:
		return -1;
	}
}

void	lcdrender(void);

/* Driving the console. */
static struct tty *const T0 = &lcdttys[0];
static struct tty *const T1 = &lcdttys[1];

static int
pending(void)
{
	return *shim_vt_reg(NVIC_ISPR) != 0;
}

/* Take the render interrupt while it is pending, and run due callouts. */
static void
render(void)
{
	int guard;

	for (guard = 0; guard < 50 && (pending() || ntimeout_pending); guard++) {
		while (ntimeout_pending > 0)
			(*timeout_fn[--ntimeout_pending])(0);
		if (pending()) {
			*shim_vt_reg(NVIC_ISPR) = 0;
			lcdrender();
		}
	}
	CHECK(spl_level == 0, "interrupt mask balanced after the render pass");
}

/* Write s to console unit; returns the bytes the writer has left. */
static int
wr(int unit, const char *s)
{
	struct tuio t = { s, (int)strlen(s) };

	(void)lcdwrite(unit ? DEV1 : DEV0, (struct uio *)&t, 0);
	return t.n;
}

static void
wrr(int unit, const char *s)
{
	(void)wr(unit, s);
	render();
}

static void
show(int to)
{
	lcdcons_vtswitch(to);
	render();
}

static int
ioc(int unit, u_int cmd)
{
	return lcdioctl(unit ? DEV1 : DEV0, cmd, 0, 0);
}

static unsigned char snap_a[8192], snap_b[8192];

static unsigned
state_size(void)
{
	unsigned n;

	(void)lcdterm_state(0, &n);
	return n;
}

static void
save(unsigned char *to, int frozen)
{
	unsigned n;
	const void *p = lcdterm_state(frozen, &n);

	memcpy(to, p, n);
}

static int
same(const unsigned char *a, int frozen)
{
	unsigned n;
	const void *p = lcdterm_state(frozen, &n);

	return memcmp(a, p, n) == 0;
}

#define	NOMARK	0
#define	MARK	1

/*
 * The model panel shows the live console: every cell but the cursor's and,
 * when a marker is expected, the top right one equals the cell store, and
 * the scroll start is the ring offset.
 */
static void
check_panel(const char *label, int mark)
{
	int r, c, crow, ccol, p, bad = 0;

	lcdterm_cursor(&crow, &ccol);
	if (panel_top != lcdterm_top())
		bad = 1;
	for (r = 0; r < LT_ROWS; r++) {
		p = (panel_top + r) % LT_ROWS;
		for (c = 0; c < LT_COLS; c++) {
			if (r == crow && c == ccol)
				continue;
			if (mark && r == 0 && c == LT_COLS - 1) {
				if (panel_ch[p][c] != '!' ||
				    panel_at[p][c] != LT_ATTR(7, 1, 0))
					bad = 1;
				continue;
			}
			if (panel_ch[p][c] != lcdterm_cell(r, c) ||
			    panel_at[p][c] != lcdterm_attr(r, c))
				bad = 1;
		}
	}
	check(!bad, label, __LINE__);
}

/* The model panel's row r as text without trailing blanks. */
static const char *
row(int r)
{
	static char buf[4][LT_COLS + 1];
	static int next;
	char *b = buf[next++ & 3];
	int c, p = (panel_top + r) % LT_ROWS, n = 0;

	for (c = 0; c < LT_COLS; c++)
		b[n++] = panel_ch[p][c] >= 0x20 && panel_ch[p][c] < 0x7f ?
		    (char)panel_ch[p][c] : '?';
	while (n > 0 && b[n - 1] == ' ')
		n--;
	b[n] = '\0';
	return b;
}

static int
panel_has(const char *s)
{
	int r;

	for (r = 0; r < LT_ROWS; r++)
		if (strstr(row(r), s) != NULL)
			return 1;
	return 0;
}

static void
boot(void)
{
	lcdcons_init();
	CHECK(lcdopen(DEV0, 0, 0) == 0, "open ttyv0");
	CHECK(lcdopen(DEV1, 0, 0) == 0, "open ttyv1");
	CHECK(lcdopen(makedev(LCDCONS_MAJOR, 3), 0, 0) == ENXIO,
	    "a minor beyond ttyv1 is refused");
}

static void
fill_console(int unit, const char *tag, int lines)
{
	char b[64];
	int i;

	for (i = 0; i < lines; i++) {
		snprintf(b, sizeof(b), "%s line %d\r\n", tag, i);
		wrr(unit, b);
	}
}

static void
test_open_state(void)
{
	CHECK(state_size() % 4 == 0 && state_size() > 3392, "state size");
	CHECK((T0->t_state & TS_HIDDEN) == 0, "ttyv0 opened while shown is not frozen");
	CHECK(T1->t_state & TS_HIDDEN, "ttyv1 opened while hidden is frozen");
	CHECK(T0->t_state & TS_ISOPEN, "ttyv0 is open");
	CHECK(T1->t_winsize.ws_col == LT_COLS, "ttyv1 window size");
	check_panel("the panel starts blank and equal to the store", NOMARK);
}

static void
test_roundtrip(void)
{
	int top0, b0, b1;

	fill_console(0, "zero", 40);
	wrr(0, "\033[31mred zero\033[0m");
	save(snap_a, 0);
	top0 = lcdterm_top();
	CHECK(top0 != 0, "the console 0 ring has scrolled");

	show(1);
	CHECK(T0->t_state & TS_HIDDEN, "the outgoing tty is frozen");
	CHECK((T1->t_state & TS_HIDDEN) == 0, "the incoming tty is thawed");
	check_panel("a switch repaints the incoming console from its store", NOMARK);
	CHECK(!panel_has("zero line"), "console one shows none of console zero");
	fill_console(1, "one", 35);
	wrr(1, "\033[1;42;34mbold on green\033[0m");
	save(snap_b, 0);

	show(0);
	CHECK(same(snap_a, 0), "A to B to A: console zero state equals its state before");
	CHECK(same(snap_b, 1), "A to B to A: console one state saved whole");
	check_panel("the panel shows console zero after the round trip", NOMARK);
	CHECK(panel_top == top0, "the scroll start is the restored ring offset");
	CHECK(panel_has("red zero"), "console zero text returns");

	show(1);
	CHECK(same(snap_b, 0), "B to A to B: console one state equals its state before");
	CHECK(same(snap_a, 1), "B to A to B: console zero state saved whole");
	check_panel("the panel shows console one again", NOMARK);
	b0 = ndraws;
	b1 = ntops;
	show(1);
	CHECK(ndraws == b0 && ntops == b1, "a key for the console already shown does nothing");
	show(0);
}

static void
test_frozen_draws_nothing(void)
{
	char big[800];
	int d, f, t, left, i, k;
	static const char head[] = "\033[H\033[2J";

	/* ttyv1 is frozen: output queues, nothing is drawn, nothing is started. */
	d = ndraws; f = nfills; t = ntops;
	*shim_vt_reg(NVIC_ISPR) = 0;
	wr(1, "hidden bytes");
	CHECK(!pending(), "a start on the frozen console pends no renderer pass");
	CHECK((T1->t_state & TS_BUSY) == 0, "the frozen console is not marked busy");
	lcdrender();			/* a stray pass */
	CHECK(ndraws == d && nfills == f && ntops == t,
	    "the renderer draws nothing for the frozen console");
	CHECK(T1->t_outq.c_cc == 12, "the frozen console keeps its queue");
	ioc(1, TIOCSTART);
	(void)ioc(1, TIOCFLUSH);
	T1->t_outq.c_cc = 0;
	T1->t_outq.c_head = 0;

	/* A writer past the high-water mark sleeps and loses no byte. */
	memcpy(big, head, sizeof(head) - 1);
	for (i = 0; i < 700; i++)
		big[sizeof(head) - 1 + i] = (char)('0' + i % 10);
	big[sizeof(head) - 1 + 700] = '\0';
	nsleeps = 0;
	left = wr(1, big);
	CHECK(nsleeps == 1 && (T1->t_state & TS_ASLEEP), "the writer to the frozen console sleeps");
	CHECK(T1->t_outq.c_cc > HIWAT && T1->t_outq.c_cc <= HIWAT + CHUNK,
	    "the frozen queue stops at the high-water mark plus one chunk");
	CHECK(left > 0, "the sleeping writer has bytes left");
	CHECK(ndraws == d && nfills == f && ntops == t, "a sleeping writer draws nothing");

	nwoke = 0;
	show(1);
	CHECK(nwoke == 1 && (T1->t_state & TS_ASLEEP) == 0, "the thaw wakes the writer");
	k = left;
	left = wr(1, big + (int)strlen(big) - k);
	render();
	CHECK(left == 0, "the writer finishes after the thaw");
	k = 1;
	for (i = 0; i < 700; i++)
		if (panel_ch[(panel_top + i / LT_COLS) % LT_ROWS][i % LT_COLS] !=
		    (unsigned char)('0' + i % 10))
			k = 0;
	CHECK(k, "every byte of the frozen console's queue is drawn, in order");
	show(0);
}

static void
test_prestopped(void)
{
	int c, d;

	(void)ioc(0, TIOCSTOP);
	wr(0, "stopped text");
	render();
	c = T0->t_outq.c_cc;
	CHECK(c == 12 && !panel_has("stopped text"), "a stopped console queues and draws nothing");
	show(1);
	show(0);
	CHECK(T0->t_state & TS_TTSTOP, "a console stopped before the switch is still stopped after it");
	CHECK(T0->t_outq.c_cc == c, "its queue is undrawn after the round trip");
	d = ndraws;
	CHECK(!panel_has("stopped text"), "its text is not on the panel after the round trip");
	(void)ioc(0, TIOCSTART);
	render();
	CHECK(panel_has("stopped text") && ndraws > d, "an explicit restart draws the queue");
}

static void
test_hidden_restart(void)
{
	int d, f, t;

	(void)ioc(1, TIOCSTOP);
	wr(1, "later text");
	d = ndraws; f = nfills; t = ntops;
	*shim_vt_reg(NVIC_ISPR) = 0;
	(void)ioc(1, TIOCSTART);
	CHECK((T1->t_state & TS_TTSTOP) == 0, "TIOCSTART on the frozen console clears the stop");
	CHECK((T1->t_state & TS_BUSY) == 0 && !pending(),
	    "TIOCSTART on the frozen console starts no drawing");
	render();
	CHECK(ndraws == d && nfills == f && ntops == t,
	    "TIOCSTART on the frozen console draws nothing");
	show(1);
	CHECK(panel_has("later text"), "the thaw draws what the restart released");
	show(0);
}

static void
test_renderer_guard(void)
{
	int d;

	T0->t_state |= TS_HIDDEN;	/* a state the switch never leaves */
	wr(0, "guarded");
	d = ndraws;
	lcdrender();
	CHECK(ndraws == d, "the renderer drains no tty that carries TS_HIDDEN");
	T0->t_state &= ~TS_HIDDEN;
	lcdrender();
	CHECK(panel_has("guarded"), "the same tty drains once thawed");
}

static void
test_split_escape(void)
{
	/* ESC [ 3 1 | m R: the sequence ends after the console returns. */
	wrr(0, "\033[0m\033[2J\033[H");
	wrr(0, "\033[3");
	show(1);
	wr(0, "1mR");
	show(0);
	CHECK(panel_has("R"), "the rest of a split escape sequence draws after the return");
	CHECK(panel_ch[panel_top][0] == 'R' && LT_ATTR_FG(panel_at[panel_top][0]) == 1,
	    "the sequence split across the switch takes effect");
	wrr(0, "\033[0m");
}

static void
test_stale_busy(void)
{
	wr(0, "pending pass");
	CHECK(T0->t_state & TS_BUSY, "a start marks the console busy");
	lcdcons_vtswitch(1);
	render();
	CHECK((T0->t_state & TS_BUSY) == 0, "freezing a console clears its output-pass flag");
	CHECK(!panel_has("pending pass"), "its queued output stays undrawn");
	show(0);
	CHECK(panel_has("pending pass"), "and draws after the thaw");
}

static void
test_marker(void)
{
	const char *msg = "kernel says hello\r\n";
	const char *p;
	int d;

	show(1);
	save(snap_b, 0);
	save(snap_a, 1);
	d = ndraws;
	for (p = msg; *p != '\0'; p++)
		lcdputc(0, *p);
	CHECK(ndraws > d, "kernel output while ttyv0 is hidden draws the marker");
	check_panel("the marker is one cell and nothing else changes", MARK);
	CHECK(!panel_has("kernel says"), "kernel output is not drawn on the shown console");
	CHECK(same(snap_b, 0), "kernel output leaves the shown console's store alone");
	CHECK(same(snap_a, 1), "kernel output leaves the frozen console's store alone");
	fill_console(1, "scrolling", 40);
	check_panel("the marker survives output of the shown console", MARK);
	show(0);
	check_panel("switching to ttyv0 clears the marker", NOMARK);
	show(1);
	check_panel("the marker does not return after the switch", NOMARK);
	show(0);
	lcdputc(0, 'Z');
	CHECK(panel_has("Z"), "kernel output reaches ttyv0 while it is shown");
}

static void
test_gfx_open(void)
{
	int d;

	d = ndraws;
	gfx_open = 1;
	show(1);
	CHECK((T0->t_state & TS_HIDDEN) == 0 && ndraws == d, "a switch is refused while a pixel program owns the panel");
	gfx_open = 0;
	render();
	CHECK((T0->t_state & TS_HIDDEN) == 0, "a refused request is not kept");
}

static void
test_close(void)
{
	nblocked = 0;
	wr(1, "unread output");
	CHECK(T1->t_outq.c_cc > 0, "the frozen console has output queued");
	CHECK(lcdclose(DEV1, 0, 0) == 0, "close ttyv1");
	CHECK(nblocked == 0, "closing a frozen console does not wait for a switch");
	CHECK(lcdopen(DEV1, 0, 0) == 0, "reopen ttyv1");
	CHECK(T1->t_state & TS_HIDDEN, "a reopened ttyv1 starts frozen while ttyv0 is shown");
}

static void
test_keys(void)
{
	CHECK(picokbd_vt_key(PICOKBD_PRESSED, 0x81, 1) == 0, "Ctrl+F1 selects ttyv0");
	CHECK(picokbd_vt_key(PICOKBD_PRESSED, 0x82, 1) == 1, "Ctrl+F2 selects ttyv1");
	CHECK(picokbd_vt_key(PICOKBD_PRESSED, 0x81, 0) == -1, "F1 alone selects nothing");
	CHECK(picokbd_vt_key(PICOKBD_PRESSED, 0x82, 0) == -1, "F2 alone selects nothing");
	CHECK(picokbd_vt_key(PICOKBD_PRESSED, 0x83, 1) == -1, "Ctrl+F3 selects nothing");
	CHECK(picokbd_vt_key(PICOKBD_PRESSED, 0x80, 1) == -1, "key 0x80 selects nothing");
	CHECK(picokbd_vt_key(PICOKBD_PRESSED, 'a', 1) == -1, "Ctrl+a selects nothing");
	CHECK(picokbd_vt_key(PICOKBD_RELEASED, 0x81, 1) == -1, "a release selects nothing");
	CHECK(picokbd_vt_key(PICOKBD_HOLD, 0x82, 1) == -1, "a hold selects nothing");
	CHECK(picokbd_vt_key(PICOKBD_IDLE, 0x82, 1) == -1, "idle selects nothing");
}

static void
scenario_main(void)
{
	boot();
	test_open_state();
	test_roundtrip();
	test_frozen_draws_nothing();
	test_prestopped();
	test_hidden_restart();
	test_renderer_guard();
	test_split_escape();
	test_stale_busy();
	test_marker();
	test_gfx_open();
	test_close();
	test_keys();
}

/* ttyv0 has three lines and ttyv1 is shown with its own. */
static void
panic_setup(void)
{
	boot();
	wrr(0, "console zero first\r\n");
	wrr(0, "console zero second\r\n");
	wrr(0, "\033[32mconsole zero third\033[0m");
	show(1);
	fill_console(1, "console one", 3);
	CHECK(panel_has("console one line 2") && !panel_has("console zero"),
	    "ttyv1 is shown before the failure");
	nttstart = nowake = ntimeout = ninput = nsleeps = 0;
}

static void
panic_report(const char *label)
{
	check(!panel_has("console one"), label, __LINE__);
	check(strcmp(row(0), "console zero first") == 0 &&
	    strcmp(row(1), "console zero second") == 0 &&
	    strncmp(row(2), "console zero third", 18) == 0,
	    "the panel ends with ttyv0's text", __LINE__);
	CHECK(panel_has("panic: the end"), "the report is on the panel");
	CHECK(panel_top == lcdterm_top(), "the scroll start is ttyv0's ring offset");
	CHECK(nttstart == 0 && nowake == 0 && ntimeout == 0 && ninput == 0 &&
	    nsleeps == 0, "no tty call, wakeup, callout or sleep on the failure path");
	CHECK(spl_level == 0, "the failure path restores the interrupt mask");
}

static void
scenario_panic(void)
{
	const char *p;

	panic_setup();
	panicstr = "test";
	for (p = "\r\npanic: the end\r\n"; *p != '\0'; p++)
		lcdputc(0, *p);
	panic_report("a panic with ttyv1 shown leaves no ttyv1 text on the panel");
}

static void
scenario_fault(void)
{
	const char *p;

	panic_setup();
	lcdabandon();			/* arm_fault(): panicstr is still NULL */
	for (p = "\r\npanic: the end\r\n"; *p != '\0'; p++)
		lcdputc(0, *p);
	panic_report("a fault with ttyv1 shown leaves no ttyv1 text on the panel");
}

static int
run(void (*scenario)(void))
{
	int status;
	pid_t pid;

	fflush(NULL);
	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0) {
		scenario();
		exit(failures != 0);
	}
	if (waitpid(pid, &status, 0) < 0)
		return 1;
	return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}

int
main(void)
{
	int bad = 0;

	bad |= run(scenario_main);
	bad |= run(scenario_panic);
	bad |= run(scenario_fault);
	if (bad) {
		fprintf(stderr, "lcdvt_test: failed\n");
		return 1;
	}
	printf("lcdvt_test: ok\n");
	return 0;
}
