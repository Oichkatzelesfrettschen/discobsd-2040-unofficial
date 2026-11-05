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
 * routine. The tty layer calls the start routine with spltty() held, which
 * on this port is PRIMASK, so drawing there would hold every interrupt off
 * for a whole queue: a region redraw is hundreds of cells at about 122 us
 * each. A callout would not do either, because softclock() runs callouts
 * inside the SysTick exception, which cannot preempt itself. lcdstart()
 * marks the tty busy and pends IRQ 26, which the RP2040 leaves unconnected
 * so software can pend it through NVIC_ISPR (datasheet 2.3.2). Its
 * handler, SPARE_IRQ_0_Handler(), runs at the IPL_SOFTCLOCK priority byte
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
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/config.h>

#include <machine/intr.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include <rp2040/dev/lcdterm.h>

#define	DEFER_RING	128		/* Power of two. */

#define	LCD_RENDER_IRQ	26		/* SPARE_IRQ_0, never wired. */
#define	NVIC_ISPR	0xe000e200UL

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
	if (lcd_ready)
		return;
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

	if (lcd_ready && lcd_owned) {
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

	if (lcd_owned && panicstr != NULL && !lcd_abandoned) {
		splx(s);
		lcdabandon();
		s = spltty();
	}
	if (!lcd_owned) {
		lcd_owned = 1;
		got = 1;
	}
	splx(s);
	return got;
}

static void
defer_put(int c)
{
	if (defer_head - defer_tail < DEFER_RING)
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

/* Called by the tty layer at spltty(). */
static void
lcdstart(struct tty *tp)
{
	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP))
		return;
	if (tp->t_outq.c_cc == 0) {
		ttyowake(tp);
		return;
	}
	tp->t_state |= TS_BUSY;
	render_pend();
}

/* The render interrupt: draw the queued output, preemptible by the clock. */
void
SPARE_IRQ_0_Handler(void)
{
	struct tty *tp = &lcdttys[0];
	int s, c;

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
	s = spltty();
	while (tp->t_outq.c_cc > 0 && (tp->t_state & TS_TTSTOP) == 0) {
		c = getc(&tp->t_outq);
		splx(s);
		lcdterm_putc(c);
		s = spltty();
	}
	splx(s);
	lcd_release();
	s = spltty();
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
	s = spltty();
	defer_put((u_char)c);
	splx(s);
	if (!lcd_acquire())
		return;		/* The owner drains the byte. */
	lcd_release();
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

/* Terminal reports go to the input queue as though typed. */
void
lcdterm_reply(const char *s, int n)
{
	lcdcons_input(s, n);
}

int
lcdopen(dev_t dev, int flag __unused, int mode __unused)
{
	struct tty *tp;

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
	struct tty *tp = &lcdttys[minor(dev)];

	ttywflush(tp);
	ttyclose(tp);
	return 0;
}

int
lcdread(dev_t dev, struct uio *uio, int flag)
{
	return ttread(&lcdttys[minor(dev)], uio, flag);
}

int
lcdwrite(dev_t dev, struct uio *uio, int flag)
{
	return ttwrite(&lcdttys[minor(dev)], uio, flag);
}

int
lcdselect(dev_t dev, int rw)
{
	return ttyselect(&lcdttys[minor(dev)], rw);
}

int
lcdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	int error;

	error = ttioctl(&lcdttys[minor(dev)], cmd, addr, flag);
	if (error < 0)
		error = ENOTTY;
	return error;
}

static int
lcdprobe(struct conf_device *config __unused)
{
	int version;

	lcdcons_init();
	printf("lcd0: ST7365P 320x320 on SPI1, %dx%d cells", LT_COLS, LT_ROWS);
	if (CONS_MAJOR == LCDCONS_MAJOR)
		printf(", console");
	printf("\n");
	version = picokbd_version();
	if (version >= 0)
		printf("kbd0: keyboard controller at I2C1 0x%x, firmware %d.%d\n",
		    PICOCALC_KBD_ADDR, version >> 4, version & 0xf);
	else
		printf("kbd0: keyboard controller at I2C1 0x%x not answering\n",
		    PICOCALC_KBD_ADDR);
	return 1;
}

struct driver picocalcdriver = {
	"picocalc", lcdprobe,
};
