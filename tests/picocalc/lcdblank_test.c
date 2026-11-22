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
 * Host gate of the console blanker (lcdcons.c under LCD_BLANK, decision D216,
 * docs/research/picocalc-console-blanker.md): the idle count, the renderer's
 * sleep and wake ordering, the abandon and graphics-open paths and the clock
 * step, against a model panel. The model keeps the panel's command log, its
 * sleep state and the SLPOUT time that the kernel keeps as lcd_slpout_at (zero
 * at boot). The clock is the PC_TIMERAWL register that pc_micros() reads. The
 * renderer interrupt runs by hand when the model NVIC pend register is set,
 * and timeout() records its callouts without running them: a scenario fires
 * them itself. Each scenario runs in a forked child, as lcdvt_test does.
 * lcdblank_wiring.sh checks the st7365p.c and picokbd.c orderings that the
 * model cannot reach.
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
#include <rp2040/dev/picocalc_reg.h>
#include <sys/kernel.h>

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

void st7365p_init(void) { }
void st7365p_display_on(void) { }
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

void picokbd_evq_wakeon(void) { }

/* The session layer's read-select, reduced to its tty half inside the section. */
int
picokbd_evq_select(const struct picokbd_sel *o, const void *proc __unused,
    int pid __unused, int pgrp __unused, int tty_pgrp __unused,
    unsigned tty_flags __unused)
{
	int x = o->raise(), r = o->tty();

	o->restore(x);
	return r;
}

static int nselwakes;

void
selwakeup(struct proc *p __unused, long coll __unused)
{
	nselwakes++;
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
struct callout_rec {
	void	(*fn)(caddr_t);
	int	ticks;
};
static struct callout_rec callouts[32];
static int ncallouts;

void
timeout(void (*fun)(caddr_t), caddr_t arg __unused, int ticks)
{
	ntimeout++;
	if (ncallouts < 32) {
		callouts[ncallouts].fn = fun;
		callouts[ncallouts].ticks = ticks;
	}
	ncallouts++;
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

static struct tty *sel_tp;

int
ttyselect(struct tty *tp, int rw __unused)
{
	sel_tp = tp;
	return 0;
}

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


/* The blanker's model: the clock, the panel's sleep state and command log. */
int hz = 1000;				/* HZ of sys/arch/rp2040/include/machparam.h. */
struct shim_timeval time;		/* The kernel's clock; the blanker must not read it. */

#define	BLANK_LOG	64
static const char *cmdlog[BLANK_LOG];
static int ncmd;
static int slp_asleep;			/* The panel sleeps (SLPIN sent, no SLPOUT). */
static int slp_pending;			/* The model's lcd_power_pending. */
static u_int wake_at;			/* The kernel's lcd_slpout_at: zero at boot. */
static void (*sleep_hook)(void);	/* Runs during the next SLPIN, once. */

static u_int
clock_now(void)
{
	return *shim_vt_reg(PC_TIMERAWL);
}

static void
clock_set(u_int us)
{
	*shim_vt_reg(PC_TIMERAWL) = us;
}

static void
clock_add(u_int us)
{
	*shim_vt_reg(PC_TIMERAWL) += us;
}

static void
mdelay(unsigned ms)
{
	clock_add(ms * 1000U);
}

static void
logcmd(const char *c)
{
	if (ncmd < BLANK_LOG)
		cmdlog[ncmd] = c;
	ncmd++;
}

static int
count_cmd(const char *c)
{
	int i, n = 0;

	for (i = 0; i < ncmd && i < BLANK_LOG; i++)
		if (strcmp(cmdlog[i], c) == 0)
			n++;
	return n;
}

static const char *
last_cmd(void)
{
	return ncmd > 0 && ncmd <= BLANK_LOG ? cmdlog[ncmd - 1] : "(none)";
}

static void
reset_log(void)
{
	ncmd = 0;
	ncallouts = 0;
}

/* The /dev/lcdgfx read, not reached by the blanker: a zero-length read. */
int
lcdgfx_read(struct uio *uio __unused)
{
	return 0;
}

/* The panel primitives of st7365p.c that the blanker calls. */
void
st7365p_sleep(void)
{
	slp_pending = 1;
	logcmd("SLPIN");
	slp_asleep = 1;
	if (sleep_hook != NULL) {
		void (*h)(void) = sleep_hook;

		sleep_hook = NULL;
		h();
	}
	mdelay(5);
}

void
st7365p_wake(void)
{
	slp_pending = 1;
	logcmd("SLPOUT");
	slp_asleep = 0;
	mdelay(5);
	wake_at = clock_now();
	slp_pending = 0;
}

int
st7365p_sleep_early(void)
{
	return clock_now() - wake_at < 120000U;
}

/* st7365p_abort() sends SLPOUT when a transfer or a sleep transition is pending. */
void
st7365p_abort(void)
{
	if (slp_pending) {
		logcmd("SLPOUT");
		slp_asleep = 0;
		slp_pending = 0;
	}
}

void lcdrender(void);

/* Drive the renderer until its interrupt is no longer pended. */
static void
pump(void)
{
	int g;

	for (g = 0; g < 50 && *shim_vt_reg(NVIC_ISPR) != 0; g++) {
		*shim_vt_reg(NVIC_ISPR) = 0;
		lcdrender();
	}
}

/* One completed keyboard exchange, 50 ms after the previous one. */
static int
exchange(int key)
{
	int woke;

	clock_add(50000U);
	woke = lcdcons_blank_poll(key);
	pump();
	return woke;
}

static void
boot(u_int start)
{
	clock_set(start);
	time.tv_sec = 0;
	time.tv_usec = 0;
	lcdcons_init();
}

/* Exchanges until the blank request reaches the panel on the last one (6001). */
static void
idle_to_sleep(void)
{
	int k;

	for (k = 0; k < 6001; k++)
		exchange(0);
}

static void
key_in_sleep(void)
{
	(void)lcdcons_blank_poll(1);
}

/* 1: the idle count starts at the first exchange and blanks at 300 s. */
static void
scen_idle(void)
{
	int k = 0;

	boot(3000000000U);
	exchange(0);
	k++;
	CHECK(count_cmd("SLPIN") == 0, "boot does not blank at the first exchange");
	while (k < 5981) {
		exchange(0);
		k++;
	}
	CHECK(count_cmd("SLPIN") == 0, "299 s of idle does not blank");
	while (k < 6000) {
		exchange(0);
		k++;
	}
	CHECK(count_cmd("SLPIN") == 0, "idle just under 300 s does not blank");
	exchange(0);
	k++;
	CHECK(count_cmd("SLPIN") == 1, "300 s of idle blanks once");
	for (k = 0; k < 100; k++)
		exchange(0);
	CHECK(count_cmd("SLPIN") == 1, "a blanked panel receives no second SLPIN");
	CHECK(slp_asleep == 1, "the model panel sleeps after the blank");
}

/* 2: a key while asleep wakes at once, with no gap and no callout. */
static void
scen_wake(void)
{
	int woke;

	boot(3000000000U);
	idle_to_sleep();
	CHECK(count_cmd("SLPIN") == 1, "the setup panel sleeps");
	/*
	 * The SLPOUT of the last wake lies 50 ms before this key. The kernel
	 * makes SLPIN wait for that gap, so this state is not reachable through
	 * the blanker alone; the setter tests that the wake ignores the gap.
	 */
	wake_at = clock_now() - 50000U;
	reset_log();
	woke = lcdcons_blank_poll(1);
	CHECK(woke == 1, "a key while asleep reports the wake");
	pump();
	CHECK(ncmd == 1 && strcmp(last_cmd(), "SLPOUT") == 0,
	    "a key wakes the panel with no gap");
	CHECK(ncallouts == 0, "a key wake arms no callout");
	CHECK(slp_asleep == 0, "the model panel is awake after the key");
}

/* 3: a sleep request inside the 120 ms window arms one callout, then SLPIN. */
static void
scen_window(void)
{
	int i;

	/*
	 * 2^32 us less 300 s: the clock wraps to zero at the 6000th exchange.
	 * The count then lacks 1 us (lcdcons_blank_poll() stores 1 for a zero
	 * clock), so the request is pended on the 6002nd exchange, at 100 ms
	 * after the wrap: inside the window.
	 */
	boot(3994967296U);
	for (i = 0; i < 6002; i++)
		exchange(0);
	CHECK(count_cmd("SLPIN") == 0 && ncallouts == 1,
	    "a sleep request in the window arms one callout");
	for (i = 0; i < 10; i++)
		lcdrender();
	CHECK(ncallouts == 1, "one deferral callout per window");
	CHECK(count_cmd("SLPIN") == 0, "no SLPIN inside the window");
	if (ncallouts >= 1) {
		CHECK(callouts[0].ticks == hz / 8 + 1,
		    "the deferral waits hz / 8 + 1 ticks");
		clock_add(120000U);
		callouts[0].fn((caddr_t)0);
	}
	pump();
	CHECK(count_cmd("SLPIN") == 1, "the callout sends SLPIN after the window");
}

/* 4: a key during SLPIN leaves the panel awake once the renderer settles. */
static void
scen_key_during_sleep(void)
{
	int i;

	boot(3000000000U);
	for (i = 0; i < 6000; i++)
		exchange(0);
	sleep_hook = key_in_sleep;
	exchange(0);
	CHECK(count_cmd("SLPIN") == 1, "the key-during-SLPIN panel sleeps");
	CHECK(ncmd == 2 && strcmp(last_cmd(), "SLPOUT") == 0,
	    "a key during SLPIN leaves the panel awake");
	CHECK(slp_asleep == 0, "the model panel is awake after the settle");
}

/* 5: lcdabandon() on a sleeping panel ends in an awake panel. */
static void
scen_abandon(void)
{
	boot(3000000000U);
	idle_to_sleep();
	CHECK(slp_asleep == 1, "the panel sleeps before the abandon");
	reset_log();
	lcdabandon();
	CHECK(count_cmd("SLPOUT") == 1 && slp_asleep == 0,
	    "lcdabandon wakes a sleeping panel");
}

/* 6: a graphics open wakes the panel first; an open lcdgfx keeps it awake. */
static void
scen_gfx(void)
{
	int i;

	boot(3000000000U);
	idle_to_sleep();
	reset_log();
	(void)lcdcons_gfx(1);
	logcmd("GFX");
	CHECK(ncmd == 2 && strcmp(cmdlog[0], "SLPOUT") == 0 &&
	    strcmp(cmdlog[1], "GFX") == 0,
	    "a graphics open wakes the panel before its first command");
	lcdgfx_open();
	reset_log();
	for (i = 0; i < 12000; i++)
		exchange(0);
	CHECK(count_cmd("SLPIN") == 0, "an open lcdgfx keeps the panel awake through 600 s");
}

/* 7: the idle count ignores steps of the kernel clock. */
static void
scen_clock_step(void)
{
	int k;

	boot(3000000000U);
	for (k = 0; k < 6000; k++) {
		time.tv_sec = (k & 1) ? 2000000000L : 5L;
		exchange(0);
	}
	CHECK(count_cmd("SLPIN") == 0, "a clock step does not blank early");
	time.tv_sec = 0;
	exchange(0);
	CHECK(count_cmd("SLPIN") == 1, "idle time does not follow the clock");
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

	bad |= run(scen_idle);
	bad |= run(scen_wake);
	bad |= run(scen_window);
	bad |= run(scen_key_during_sleep);
	bad |= run(scen_abandon);
	bad |= run(scen_gfx);
	bad |= run(scen_clock_step);
	if (bad) {
		fprintf(stderr, "lcdblank_test: failed\n");
		return 1;
	}
	printf("lcdblank_test: ok\n");
	return 0;
}
