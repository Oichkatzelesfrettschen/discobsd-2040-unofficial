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
 * The PicoCalc console: a tty whose output lcdterm.c draws on the panel
 * and whose input picokbd.c polls from the keyboard controller.
 *
 * Tty output is drawn from a software interrupt, never from the start
 * routine. The tty layer calls the start routine with spltty() held,
 * which on this port is PRIMASK, from most paths, so drawing there would
 * hold every interrupt off
 * for a whole queue: a region redraw is hundreds of cells at about 122 us
 * each. A callout would not do either, because softclock() runs callouts
 * inside the SysTick exception, which cannot preempt itself. lcdstart()
 * marks the tty busy and pends IRQ 26, which the RP2040 leaves unconnected
 * so software can pend it through NVIC_ISPR (datasheet 2.3.2). Its
 * handler, lcdrender(), entered on a stack of its own through
 * SPARE_IRQ_0_Handler in lcdrender.S, runs at the IPL_SOFTCLOCK priority byte
 * (128): SysTick and the UART and USB interrupts (0 and 64) preempt it,
 * and PendSV (192), which switches processes, waits until it returns, so
 * no switch lands inside a panel window. The renderer masks interrupts
 * only to take a byte off the queue.
 *
 * Drawing a cell is a panel write window that another drawer must not
 * interrupt, and the kernel prints from interrupt handlers. One owner
 * flag serializes drawing: the renderer and the kernel's console putc
 * each take it, draw and release it; a putc that finds it held, which
 * only an interrupt nested over the owner can, appends to a deferred ring
 * that the owner drains, in order, before releasing. A full ring counts
 * what it drops and the drain prints a marker in its place. Kernel putc
 * draws synchronously at whatever priority its caller holds, so a kernel
 * message costs its caller one cell, or one row fill at a newline that
 * scrolls.
 *
 * A fatal kernel fault or a panic abandons the owner, which will never
 * resume: lcdabandon() ends the open panel transfer, resets the terminal
 * parser and frees the flag, so the report draws from a clean bus.
 *
 * Minor LCDGFX_MINOR of the same character major is /dev/lcdgfx (lcdgfx.c):
 * the entry points below hand that minor to it, and lcdcons_gfx() switches
 * the terminal between text and a pixel program under the drawing flag.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/config.h>
#include <sys/lcdcons.h>

#include <machine/intr.h>
#include <machine/board.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include <rp2040/dev/lcdterm.h>
#include <rp2040/dev/lcdgfx.h>
#include <rp2040/dev/picokbd_evq.h>

#define	DEFER_RING	128		/* Power of two. */

#define	LCD_RENDER_IRQ	26		/* SPARE_IRQ_0, never wired. */
#define	NVIC_ISPR	0xe000e200UL

/*
 * The renderer's stack (lcdrender.S switches to it). 2 KB holds the
 * renderer and every exception that can nest above it: the static
 * estimate in docs/research/picocalc-console-review.md is about 460
 * bytes for the renderer, 420 for SysTick and its callouts and 520-680
 * for a fault report. It is painted at init so that
 * lcdcons_render_stack_free() reports the depth never touched.
 */
#define	LCD_RSTACK_WORDS	512
#define	LCD_RSTACK_PAINT	0x5afe57acU

static u_int lcd_render_stack[LCD_RSTACK_WORDS] __attribute__((aligned(8)));
void *const lcd_render_sp = &lcd_render_stack[LCD_RSTACK_WORDS];

/*
 * Terminal reports wait here until the drawing flag is released, so the
 * tty input path never runs inside a draw. A report is queued whole or
 * not at all: 64 bytes hold eight of the longest, "ESC [ 32 ; 53 R".
 */
#define	REPLY_RING	64		/* Power of two. */

static char reply[REPLY_RING];
static u_int reply_head, reply_tail;

struct tty lcdttys[LCDCONS_NTTY];

static int lcd_ready;
static int lcd_owned;
static int lcd_abandoned;
static int render_wanted;	/* The renderer found the flag held. */

static char defer[DEFER_RING];
static u_int defer_head, defer_tail;
static u_int defer_lost;

static const char lost_marker[] = "\r\n[console output lost]\r\n";

void
lcdcons_init(void)
{
	int i;

	if (lcd_ready)
		return;
	for (i = 0; i < LCD_RSTACK_WORDS; i++)
		lcd_render_stack[i] = LCD_RSTACK_PAINT;
	st7365p_init();
	lcdterm_init();
	st7365p_display_on();
	picokbd_init();
	arm_intr_set_priority(LCD_RENDER_IRQ, IPL_SOFTCLOCK);
	arm_intr_enable_irq(LCD_RENDER_IRQ);
	lcd_ready = 1;
}

static void
render_pend(void)
{
	PC_REG32(NVIC_ISPR) = 1UL << LCD_RENDER_IRQ;
}

/*
 * End an owner that will never resume. Called by arm_fault() for a kernel
 * fault, before it prints, and by the putc path once panic() has run.
 */
void
lcdabandon(void)
{
	int s = splhigh();

	if (lcd_ready) {
		/*
		 * End a transfer only if a drawer was in one, but reset the
		 * terminal either way: a program's colors, character set,
		 * scrolling region or unfinished escape would otherwise shape
		 * or swallow the report.
		 */
		if (lcd_owned || lcdgfx_isopen())
			st7365p_abort();
		lcdterm_abort();
	}
	lcd_owned = 0;
	lcd_abandoned = 1;
	splx(s);
}

/* Take the drawing flag; 0 when another context holds it. */
static int
lcd_acquire(void)
{
	int s = spltty(), got = 0;

	if (!lcd_owned) {
		lcd_owned = 1;
		got = 1;
	}
	splx(s);
	return got;
}

/*
 * Queue a byte for the owner. Once the ring has overflowed, later bytes
 * are dropped too until the drain prints the loss marker, so the marker
 * stands where the gap is rather than after output queued behind it.
 */
static void
defer_put(int c)
{
	if (defer_lost == 0 && defer_head - defer_tail < DEFER_RING)
		defer[defer_head++ % DEFER_RING] = (char)c;
	else
		defer_lost++;
}

/*
 * Draw the deferred bytes in order and the cursor, then free the flag. The
 * flag clears only with interrupts masked and the ring seen empty, so a
 * byte an interrupt defers while the cursor draws is drawn too.
 */
static void
lcd_release(void)
{
	const char *m;
	int s, c, cursor_drawn = 0;

	for (;;) {
		s = spltty();
		if (defer_tail != defer_head) {
			c = defer[defer_tail++ % DEFER_RING];
			splx(s);
			lcdterm_putc((u_char)c);
			cursor_drawn = 0;
		} else if (defer_lost != 0) {
			defer_lost = 0;
			splx(s);
			for (m = lost_marker; *m != '\0'; m++)
				lcdterm_putc((u_char)*m);
			cursor_drawn = 0;
		} else if (!cursor_drawn) {
			splx(s);
			lcdterm_cursor_show();
			cursor_drawn = 1;
		} else {
			lcd_owned = 0;
			if (render_wanted) {
				render_wanted = 0;
				render_pend();
			}
			splx(s);
			return;
		}
	}
}

/*
 * The tty start routine. Most callers hold spltty(), but ttwrite() calls
 * ttstart() without it (sys/kern/tty.c), so the state checks and the
 * TS_BUSY update take it here: a keyboard poll that sets TS_TTSTOP for a
 * Ctrl-S between the check and the store would otherwise be overwritten.
 */
static void
lcdstart(struct tty *tp)
{
	int s = spltty();

	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
		splx(s);
		return;
	}
	if (tp->t_outq.c_cc == 0) {
		ttyowake(tp);
		splx(s);
		return;
	}
	tp->t_state |= TS_BUSY;
	render_pend();
	splx(s);
}

/* Bytes of the render stack no exception has reached since boot. */
u_int
lcdcons_render_stack_free(void)
{
	u_int i = 0;

	while (i < LCD_RSTACK_WORDS && lcd_render_stack[i] == LCD_RSTACK_PAINT)
		i++;
	return i * sizeof(u_int);
}

/*
 * Deliver the terminal reports queued during a draw in one masked
 * section, so a keystroke the keyboard poll delivers cannot land between
 * the bytes of a report and make the client reject it.
 */
static void
reply_flush(void)
{
	struct tty *tp = &lcdttys[0];
	int s = spltty();

	while (reply_tail != reply_head) {
		if (tp->t_state & TS_ISOPEN)
			ttyinput((u_char)reply[reply_tail % REPLY_RING], tp);
		reply_tail++;
	}
	splx(s);
}

void lcdrender(void);

/*
 * The renderer runs above thread mode, so every process, every system
 * call and the PendSV context switch wait while it draws. It draws for at
 * most LCD_RENDER_SLICE_US per pass and, with output still queued, lets
 * thread mode run for a tick before the next pass. A single escape that
 * redraws a scrolling region cannot be split and can exceed the slice.
 */
#define	LCD_RENDER_SLICE_US	8000

static void
render_resume(caddr_t arg __unused)
{
	render_pend();		/* From the callout; the IRQ draws. */
}

/*
 * The render interrupt, entered on the render stack through
 * SPARE_IRQ_0_Handler in lcdrender.S: draw the queued output, preemptible
 * by the clock.
 */
void
lcdrender(void)
{
	struct tty *tp = &lcdttys[0];
	u_int t0;
	int s, c, sliced;

	if (panicstr != NULL)
		return;		/* The panic printer owns the panel. */
	if (!lcd_acquire()) {
		/*
		 * A kernel message this interrupt preempted holds the flag;
		 * its release pends the renderer again.
		 */
		s = spltty();
		render_wanted = 1;
		splx(s);
		return;
	}
	t0 = pc_micros();
	sliced = 0;
	s = spltty();
	while (tp->t_outq.c_cc > 0 && (tp->t_state & TS_TTSTOP) == 0) {
		if (pc_micros() - t0 >= LCD_RENDER_SLICE_US) {
			sliced = 1;
			break;
		}
		c = getc(&tp->t_outq);
		splx(s);
		lcdterm_putc(c);
		s = spltty();
	}
	splx(s);
	lcd_release();
	reply_flush();
	s = spltty();
	if (sliced) {
		/* Still busy; thread mode runs for a tick first. */
		ttyowake(tp);
		timeout(render_resume, (caddr_t)0, 1);
		splx(s);
		return;
	}
	tp->t_state &= ~TS_BUSY;
	ttyowake(tp);
	if (tp->t_outq.c_cc > 0)
		lcdstart(tp);
	splx(s);
}

void
lcdputc(dev_t dev __unused, char c)
{
	struct tty *tp = &lcdttys[0];
	int s;

	if (!lcd_ready)
		lcdcons_init();
	if (panicstr != NULL) {
		/*
		 * panic() prints with interrupts enabled. From here on the
		 * panic printer draws with them masked, so no renderer or
		 * nested output can interrupt or abandon it; an owner it
		 * interrupted, which will never resume, is abandoned once.
		 */
		s = splhigh();
		if (!lcd_abandoned)
			lcdabandon();
		defer_put((u_char)c);
		if (lcd_acquire())
			lcd_release();
		splx(s);
		return;
	}
	s = spltty();
	defer_put((u_char)c);
	splx(s);
	if (!lcd_acquire())
		return;		/* The owner drains the byte. */
	lcd_release();
	reply_flush();
	s = spltty();
	if (tp->t_outq.c_cc > 0 && (tp->t_state & TS_ISOPEN))
		ttstart(tp);
	splx(s);
}

char
lcdgetc(dev_t dev __unused)
{
	if (!lcd_ready)
		lcdcons_init();
	return (char)picokbd_getc();
}

void
lcdcons_input(const char *s, int n)
{
	struct tty *tp = &lcdttys[0];
	int x;

	if ((tp->t_state & TS_ISOPEN) == 0)
		return;
	x = spltty();
	while (n-- > 0)
		ttyinput((u_char)*s++, tp);
	splx(x);
}

/*
 * Terminal reports go to the input queue as though typed, after the draw
 * that produced them releases the drawing flag.
 */
void
lcdterm_reply(const char *s, int n)
{
	int x = spltty();

	if (n > 0 && reply_head - reply_tail + (u_int)n <= REPLY_RING) {
		while (n-- > 0)
			reply[reply_head++ % REPLY_RING] = *s++;
	}
	splx(x);
}

int
lcdopen(dev_t dev, int flag __unused, int mode __unused)
{
	struct tty *tp;

	if (minor(dev) == LCDGFX_MINOR)
		return lcdgfx_open();
	if (minor(dev) >= LCDCONS_NTTY)
		return ENXIO;
	tp = &lcdttys[minor(dev)];
	if (!lcd_ready)
		lcdcons_init();
	tp->t_oproc = lcdstart;
	tp->t_addr = (caddr_t)tp;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars(tp);
		tp->t_ispeed = tp->t_ospeed = B115200;
		tp->t_state = TS_ISOPEN | TS_CARR_ON;
		tp->t_flags = ECHO | CRMOD | CRTBS | CRTERA | CTLECH | CRTKIL;
		tp->t_winsize.ws_row = LT_ROWS;
		tp->t_winsize.ws_col = LT_COLS;
		tp->t_winsize.ws_xpixel = LT_COLS * LT_CELL_W;
		tp->t_winsize.ws_ypixel = LT_ROWS * LT_CELL_H;
	}
	if ((tp->t_state & TS_XCLUDE) && u.u_uid != 0)
		return EBUSY;
	picokbd_poll_start();
	return ttyopen(dev, tp);
}

int
lcdclose(dev_t dev, int flag __unused, int mode __unused)
{
	struct tty *tp;

	if (minor(dev) == LCDGFX_MINOR)
		return lcdgfx_close();
	tp = &lcdttys[minor(dev)];
	ttywflush(tp);
	ttyclose(tp);
	return 0;
}

int
lcdread(dev_t dev, struct uio *uio, int flag)
{
	if (minor(dev) == LCDGFX_MINOR)
		return 0;		/* Write only: end of file. */
	return ttread(&lcdttys[minor(dev)], uio, flag);
}

int
lcdwrite(dev_t dev, struct uio *uio, int flag)
{
	if (minor(dev) == LCDGFX_MINOR)
		return lcdgfx_write(uio);
	return ttwrite(&lcdttys[minor(dev)], uio, flag);
}

int
lcdselect(dev_t dev, int rw)
{
	if (minor(dev) == LCDGFX_MINOR)
		return 1;		/* Always ready. */
	return ttyselect(&lcdttys[minor(dev)], rw);
}

_Static_assert(LCD_GLYPH_FIRST == LT_GLYPH_FIRST &&
    LCD_GLYPH_ROWS == LT_GLYPH_ROWS &&
    sizeof(((struct lcd_glyph *)0)->rows) == LT_GLYPH_ROWS,
    "the user glyph layout is the terminal's bank layout");

/*
 * Replace one bank glyph. Every draw holds the drawing flag, so holding it
 * keeps the glyph from changing under a cell being drawn and lets
 * lcdterm_setglyph() redraw the cells that show it. The flag is held only
 * by a drawer an interrupt nested over, which a thread-mode caller cannot
 * be, so EAGAIN reports a condition the scheduler does not produce.
 */
static int
lcd_setglyph(const struct lcd_glyph *g)
{
	int error;

	if (!lcd_acquire())
		return EAGAIN;
	error = lcdterm_setglyph(g->code, g->rows) < 0 ? EINVAL : 0;
	lcd_release();
	reply_flush();
	return error;
}

_Static_assert(PICOKBD_EVQ_RAW_OR_CBREAK == (RAW | CBREAK) &&
    PICOKBD_EVQ_VOLATILE == (PENDIN | FLUSHO),
    "the key event session compares the tty flag word the line discipline sets");

/*
 * Hold the raw key event session to the tty's present foreground group and
 * mode word. kbd_poll() calls it before it records each event, and every
 * request calls picokbd_evq_check() before it acts, so a change of either is
 * seen before any key is recorded or served.
 */
void
lcdcons_keycheck(void)
{
	picokbd_evq_check(lcdttys[0].t_pgrp, lcdttys[0].t_flags);
}

/*
 * A mode-setting ioctl is about to run on the console tty: a process other
 * than the session's owner ends the session even if it sets the flags it
 * found (getpass() in an owner that already has RAW and no ECHO).
 */
static void
lcd_modeset(void)
{
	int x = spltty();

	picokbd_evq_modeset(u.u_procp, u.u_procp->p_pid);
	splx(x);
}

/*
 * The last close of an open file ends the raw key event session it owns
 * (vn_closefile() calls this for every inode file). Process exit closes
 * through the same path.
 */
void
lcdcons_fileclose(const struct file *fp)
{
	int x = spltty();

	picokbd_evq_fileclose(fp);
	splx(x);
}

/*
 * A process exits or execs: the raw key event session it started ends, even
 * when the console file it was started on stays open in a parent shell
 * (kern_exit.c exit(), exec_subr.c exec_clear()).
 */
void
lcdcons_procexit(const struct proc *p)
{
	int x = spltty();

	picokbd_evq_procexit(p, p->p_pid);
	splx(x);
}

/*
 * The open file the running ioctl(2) was made on. A driver gets only the
 * file's flags, but the session belongs to the open file, so the descriptor
 * is looked up from the system call's first argument and must carry the
 * flags the driver was given; any other state fails closed.
 */
static const struct file *
lcd_ioctl_file(int flag)
{
	int fd = u.u_arg[0];
	const struct file *fp;

	if (fd < 0 || fd >= NOFILE)
		return NULL;
	fp = u.u_ofile[fd];
	if (fp == NULL || fp->f_flag != flag)
		return NULL;
	return fp;
}

/*
 * LCDIOC_KEYEVS: start or end the raw key event session of this open file
 * (picokbd_evq.h). The checks need the caller's process group and the
 * tty's foreground group, which ttread() compares the same way.
 */
static int
lcd_keyevs(int flag, int on)
{
	const struct file *fp = lcd_ioctl_file(flag);
	int x, error;

	if (fp == NULL || (on != 0 && on != 1))
		return EINVAL;
	x = spltty();
	if (on)
		error = picokbd_evq_start(fp, u.u_procp, u.u_procp->p_pid,
		    u.u_procp->p_pgrp, lcdttys[0].t_pgrp, lcdttys[0].t_flags,
		    (flag & FREAD) != 0);
	else
		error = picokbd_evq_stop(fp, u.u_procp, u.u_procp->p_pid);
	splx(x);
	return error;
}

/*
 * LCDIOC_GETKEY: remove the oldest raw key events. The poller queues them
 * from the clock callout, so the drain runs at spltty() as lcdcons_input()
 * does.
 */
static int
lcd_getkey(int flag, struct lcd_keyevs *out)
{
	int x = spltty();
	int error;

	error = picokbd_evq_drain(out, u.u_procp, u.u_procp->p_pid,
	    u.u_procp->p_pgrp, lcdttys[0].t_pgrp, lcdttys[0].t_flags,
	    (flag & FREAD) != 0);
	splx(x);
	return error;
}

/*
 * Hand the panel to /dev/lcdgfx (on) or take it back (off), under the
 * drawing flag so that no renderer or kernel print draws a cell inside the
 * panel transfers of the switch; what they defer drains at the release, into
 * a terminal that is either silent (on) or repainted (off). Entering
 * returns EAGAIN while the flag is held, which only an interrupt nested over
 * a drawer can leave and a thread-mode caller cannot reach. Leaving cannot
 * fail: a text console left blank is worse than a switch without the flag.
 */
int
lcdcons_gfx(int on)
{
	int held;

	if (!lcd_ready)
		lcdcons_init();
	held = lcd_acquire();
	if (!held && on)
		return EAGAIN;
	lcdterm_gfx(on);
	if (held) {
		lcd_release();
		reply_flush();
	}
	return 0;
}

int
lcdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	struct winsize *ws;
	int error;

	if (minor(dev) == LCDGFX_MINOR)
		return lcdgfx_ioctl(cmd, addr);
	if (cmd == LCDIOC_SETGLYPH)
		return lcd_setglyph((struct lcd_glyph *)addr);
	if (cmd == LCDIOC_GETKEY)
		return lcd_getkey(flag, (struct lcd_keyevs *)addr);
	if (cmd == LCDIOC_KEYEVS)
		return lcd_keyevs(flag, *(int *)addr);
	if (cmd == TIOCSETP || cmd == TIOCSETN || cmd == TIOCLBIS ||
	    cmd == TIOCLBIC || cmd == TIOCLSET)
		lcd_modeset();

	/*
	 * A zero row or column count means the size is unknown, which
	 * login(1) sets on every console login (usr.bin/login/login.c).
	 * The panel's size is always known, so it stands in; a nonzero
	 * size a program sets is kept.
	 */
	if (cmd == TIOCSWINSZ) {
		ws = (struct winsize *)addr;
		if (ws->ws_row == 0 || ws->ws_col == 0) {
			ws->ws_row = LT_ROWS;
			ws->ws_col = LT_COLS;
			ws->ws_xpixel = LT_COLS * LT_CELL_W;
			ws->ws_ypixel = LT_ROWS * LT_CELL_H;
		}
	}
	error = ttioctl(&lcdttys[minor(dev)], cmd, addr, flag);
	if (error < 0)
		error = ENOTTY;
	return error;
}

static int
lcdprobe(struct conf_device *config __unused)
{
	int console_major, console_minor;
	int keyboard_version;

	if (board_id() != BOARD_ID_PICOCALC)
		return 0;
	board_console(&console_major, &console_minor);
	lcdcons_init();
	printf("lcd0: ST7365P 320x320 on SPI1, %dx%d cells", LT_COLS, LT_ROWS);
	if (console_major == LCDCONS_MAJOR && console_minor == 0)
		printf(", console");
	printf("\n");
	keyboard_version = picokbd_version();
	if (keyboard_version >= 0)
		printf("kbd0: keyboard controller at I2C1 0x%x, firmware %d.%d\n",
		    PICOCALC_KBD_ADDR, keyboard_version >> 4, keyboard_version & 0xf);
	else
		printf("kbd0: keyboard controller at I2C1 0x%x not answering\n",
		    PICOCALC_KBD_ADDR);
	return 1;
}

struct driver picocalcdriver = {
	"picocalc", lcdprobe,
};
