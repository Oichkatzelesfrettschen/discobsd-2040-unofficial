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
 * Drawing a cell is a panel write window that another drawer must not
 * interrupt, yet the kernel prints from interrupt handlers. One owner
 * flag serializes drawing without masking interrupts across it: the
 * output routine and the kernel's putc each take the flag, draw, and
 * release it; a putc that finds the flag held, which only an interrupt
 * nested over the owner can, appends to a deferred ring that the owner
 * drains before it releases. A panic bypasses the flag so its message
 * reaches the panel.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/config.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/lcdterm.h>

#define	DEFER_RING	128		/* Power of two. */

struct tty lcdttys[LCDCONS_NTTY];

static int lcd_ready;
static int lcd_owned;

static char defer[DEFER_RING];
static u_int defer_head, defer_tail;

void
lcdcons_init(void)
{
	if (lcd_ready)
		return;
	st7365p_init();
	lcdterm_init();
	picokbd_init();
	lcd_ready = 1;
}

/* Take the drawing flag; 0 when another context holds it. */
static int
lcd_acquire(void)
{
	int s = spltty(), got = 0;

	if (!lcd_owned || panicstr != NULL) {
		lcd_owned = 1;
		got = 1;
	}
	splx(s);
	return got;
}

static void
lcd_release(void)
{
	int s = spltty(), c;

	while (defer_tail != defer_head) {
		c = defer[defer_tail++ % DEFER_RING];
		splx(s);
		lcdterm_putc(c);
		s = spltty();
	}
	lcdterm_cursor_show();
	lcd_owned = 0;
	splx(s);
}

static void
defer_put(int c)
{
	if (defer_head - defer_tail < DEFER_RING)
		defer[defer_head++ % DEFER_RING] = (char)c;
}

static void
lcdstart(struct tty *tp)
{
	int s, c;

again:
	s = spltty();
	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
		splx(s);
		return;
	}
	if (!lcd_acquire()) {
		/* The owner's release restarts output. */
		splx(s);
		return;
	}
	tp->t_state |= TS_BUSY;
	while (tp->t_outq.c_cc > 0 && (tp->t_state & TS_TTSTOP) == 0) {
		c = getc(&tp->t_outq);
		splx(s);
		lcdterm_putc(c);
		s = spltty();
	}
	tp->t_state &= ~TS_BUSY;
	splx(s);
	lcd_release();
	/*
	 * Output a nested start found the drawing flag held for, between the
	 * loop's last check and the release above, is still queued.
	 */
	if (tp->t_outq.c_cc > 0 && (tp->t_state & TS_TTSTOP) == 0)
		goto again;
	ttyowake(tp);
}

void
lcdputc(dev_t dev __unused, char c)
{
	struct tty *tp = &lcdttys[0];
	int s;

	if (!lcd_ready)
		lcdcons_init();
	s = spltty();
	if (!lcd_acquire()) {
		defer_put((u_char)c);
		splx(s);
		return;
	}
	splx(s);
	lcdterm_putc((u_char)c);
	lcd_release();
	if (tp->t_outq.c_cc > 0 && (tp->t_state & TS_ISOPEN))
		ttstart(tp);
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
