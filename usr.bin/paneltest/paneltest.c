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
 * paneltest: the PicoCalc panel register test (sys/arch/rp2040/doc/
 * PANELTEST.md). It holds /dev/lcdgfx, so the kernel accepts
 * machdep.picocalc.panel_variant and panel_read, draws the calibration
 * patterns of the experiments, and logs every register set and readback to
 * /var/log/paneltest.log. Keys: n and p step the register set, 1 to 7 pick
 * a pattern, r reads the panel registers back, q quits; the kernel programs
 * the shipped set again when the device closes.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/lcdgfx.h>
#include <machine/cpu.h>
#include <machine/picocalcvar.h>
#include <fcntl.h>
#include <sgtty.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../sys/arch/rp2040/dev/lcdfont.h"
#include "../../sys/arch/rp2040/dev/paneltest_sets.h"

#define	NAME_ROW(name, pre, base, post)	name,
static const char *const set_names[] = { PANELTEST_SETS(NAME_ROW) };

#define	BLACK	0x0000
#define	WHITE	0xffff
#define	GRAY50	0x7bef		/* RGB565 15/31/15: 6-bit level 31 widened. */
#define	RED	0xf800
#define	GREEN	0x07e0
#define	BLUE	0x001f
#define	LOG	"/var/log/paneltest.log"

static int gfx, cur_set, cur_pat = 1;
static struct picocalc_panel_read rb;
static int have_rb;

static void
fill(int x0, int y0, int x1, int y1, unsigned color)
{
	struct lcdg_rect r;

	r.x0 = (u_short)x0; r.y0 = (u_short)y0;
	r.x1 = (u_short)x1; r.y1 = (u_short)y1;
	r.color = (u_short)color;
	(void)ioctl(gfx, LCDG_FILL, &r);
}

/* One line of 6x10 console glyphs through a 1-bit window, palette fg/bg. */
static void
text(int x, int y, const char *s, int fg, int bg)
{
	struct lcdg_window w;
	unsigned char row[54];
	int n = (int)strlen(s), stride, r, i, col, c;

	if (n > 53)
		n = 53;
	if (n == 0 || x + 6 * n > LCDG_WIDTH || y + 10 > LCDG_HEIGHT)
		return;
	w.x = (u_short)x; w.y = (u_short)y;
	w.w = (u_short)(6 * n); w.h = 10;
	w.fmt = LCDG_WIN_BITS; w.fg = (u_char)fg; w.bg = (u_char)bg;
	if (ioctl(gfx, LCDG_WINDOW, &w) < 0)
		return;
	stride = (6 * n + 7) / 8;
	for (r = 0; r < 10; r++) {
		memset(row, 0, sizeof(row));
		for (i = 0; i < n; i++) {
			c = (unsigned char)s[i];
			if (c < LCDFONT_FIRST || c >= LCDFONT_FIRST + LCDFONT_NCHARS)
				c = '?';
			for (col = 0; col < 6; col++) {
				int bit = 6 * i + col;
				if (r < LCDFONT_HEIGHT &&
				    (lcdfont[c - LCDFONT_FIRST][r] & (0x80 >> col)))
					row[bit / 8] |= (unsigned char)(0x80 >> (bit % 8));
			}
		}
		(void)write(gfx, row, (size_t)stride);
	}
}

static int
set_variant(int n)
{
	int mib[3] = { CTL_MACHDEP, CPU_PICOCALC, CPU_PICOCALC_PANEL_VARIANT };
	int v = PANELTEST_VALUE(n);

	return sysctl(mib, 3, NULL, NULL, &v, sizeof(v));
}

static void
log_line(const char *s)
{
	FILE *f = fopen(LOG, "a");

	if (f != NULL) {
		fputs(s, f);
		fputc('\n', f);
		fclose(f);
	}
}

static void
format_ent(char *buf, size_t len, const struct picocalc_panel_ent *e)
{
	int i, off;

	off = snprintf(buf, len, "%02x fb%02x:", e->cmd, e->fb);
	for (i = 0; i < e->n && i < PANEL_READ_BYTES && off < (int)len; i++)
		off += snprintf(buf + off, len - (size_t)off, " %02x", e->data[i]);
}

/*
 * Read the registers back, log them, then program the set again: a Command
 * Table 2 read through SPI Read Control can clock parameter bytes into the
 * register when SPIRC has no effect.
 */
static void
readback(void)
{
	int mib[3] = { CTL_MACHDEP, CPU_PICOCALC, CPU_PICOCALC_PANEL_READ };
	size_t len = sizeof(rb);
	char line[80];
	int i;

	have_rb = sysctl(mib, 3, &rb, &len, NULL, 0) == 0;
	snprintf(line, sizeof(line), "readback set %d %s: %s", cur_set,
	    set_names[cur_set], have_rb ? "ok" : "failed");
	log_line(line);
	for (i = 0; have_rb && i < rb.nent && i < PANEL_READ_N; i++) {
		format_ent(line, sizeof(line), &rb.ent[i]);
		log_line(line);
	}
	(void)set_variant(cur_set);
}

static void
header(void)
{
	char line[64];

	snprintf(line, sizeof(line), "%d/%d %.30s p%d", cur_set,
	    PANELTEST_NSETS - 1, set_names[cur_set], cur_pat);
	fill(0, 0, 319, 9, BLACK);
	text(0, 0, line, 15, 0);
}

/* 1: a console text page with the latest readback. */
static void
pat_text(void)
{
	static const char *const sample[] = {
		"DiscoBSD 2.7 (PICOCALC_PSRAM) panel register test",
		"The quick brown fox jumps over the lazy dog 0123456789",
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz",
		"{}[]()<>|~^`'\"\\ Il1| O0o @#$%&* ;:,.!?-_=+/",
		"keys: n/p set  1-7 pattern  r readback  q quit",
	};
	char line[80];
	int i, y = 14;

	fill(0, 10, 319, 319, BLACK);
	for (i = 0; i < 5; i++, y += 10)
		text(0, y, sample[i], i == 4 ? 8 : 7, 0);
	y += 4;
	for (i = 0; have_rb && i < rb.nent && i < PANEL_READ_N; i++, y += 10) {
		format_ent(line, sizeof(line), &rb.ent[i]);
		text(0, y, line, 15, 0);
	}
}

/* 2: black and white halves, 1-pixel checker, columns and rows. */
static void
pat_edges(void)
{
	int x, y;

	fill(0, 10, 159, 119, BLACK);
	fill(160, 10, 319, 119, WHITE);
	fill(0, 120, 319, 319, BLACK);
	for (y = 120; y < 180; y++)
		for (x = (y & 1); x < 100; x += 2)
			fill(x, y, x, y, WHITE);
	for (x = 110; x < 210; x += 2)
		fill(x, 120, x, 179, WHITE);
	for (y = 120; y < 180; y += 2)
		fill(220, y, 319, y, WHITE);
	text(0, 200, "white text on black", 15, 0);
	fill(0, 220, 319, 319, WHITE);
}

/* 3: 32 gray bars, then full black and full white patches. */
static void
pat_gray(void)
{
	int i;
	unsigned g;

	for (i = 0; i < 32; i++) {
		g = (unsigned)i << 11 | (unsigned)(2 * i + (i > 15)) << 5 |
		    (unsigned)i;
		fill(i * 10, 10, i * 10 + 9, 199, g);
	}
	fill(0, 200, 159, 319, BLACK);
	fill(160, 200, 319, 319, WHITE);
}

/* 4: isolated 1-pixel red, green, blue and white columns, red beside white. */
static void
pat_subpixel(void)
{
	static const unsigned col[4] = { RED, GREEN, BLUE, WHITE };
	int i, x;

	fill(0, 10, 319, 319, BLACK);
	for (x = 8, i = 0; x < 300; x += 12, i = (i + 1) % 4)
		fill(x, 20, x, 199, col[i]);
	fill(150, 220, 150, 319, RED);
	fill(151, 220, 151, 319, WHITE);
	text(0, 205, "R G B W columns; red beside white below", 7, 0);
}

/* 5: full 50% gray with a 1-dot checkerboard square (flicker, VCOM). */
static void
pat_flicker(void)
{
	int x, y;

	fill(0, 10, 319, 319, GRAY50);
	for (y = 110; y < 210; y++)
		for (x = 110 + (y & 1); x < 210; x += 2)
			fill(x, y, x, y, WHITE);
}

/* 6: a white square on 50% gray (crosstalk bands). */
static void
pat_crosstalk(void)
{
	fill(0, 10, 319, 319, GRAY50);
	fill(110, 110, 209, 209, WHITE);
}

/*
 * 7: ten 32-line color bands over RAM lines 0-319, then a 480-line scroll
 * area started at line 160: the glass shows lines 160-479, so bands 5-9
 * sit in the top half and lines 320-479, which nothing drew, below them.
 */
static void
pat_lines(void)
{
	static const unsigned band[10] = { RED, GREEN, BLUE, WHITE, 0xffe0,
	    0x07ff, 0xf81f, GRAY50, 0xfc00, 0x841f };
	struct lcdg_scrdef d;
	struct lcdg_scroll s;
	int i;

	for (i = 0; i < 10; i++)
		fill(0, i * 32, 319, i * 32 + 31, band[i]);
	d.top = 0;
	d.bottom = 0;
	(void)ioctl(gfx, LCDG_SCRDEF, &d);
	s.line = 160;
	(void)ioctl(gfx, LCDG_SCROLL, &s);
}

static void
draw(void)
{
	struct lcdg_scrdef d;
	struct lcdg_scroll s;

	d.top = 0;
	d.bottom = LCDG_RAM_LINES - LCDG_HEIGHT;
	(void)ioctl(gfx, LCDG_SCRDEF, &d);
	s.line = 0;
	(void)ioctl(gfx, LCDG_SCROLL, &s);
	switch (cur_pat) {
	case 1: pat_text(); break;
	case 2: pat_edges(); break;
	case 3: pat_gray(); break;
	case 4: pat_subpixel(); break;
	case 5: pat_flicker(); break;
	case 6: pat_crosstalk(); break;
	case 7: pat_lines(); return;
	}
	header();
}

static void
change_set(int n)
{
	char line[80];

	if (n < 0 || n >= PANELTEST_NSETS)
		return;
	if (set_variant(n) < 0) {
		snprintf(line, sizeof(line), "set %d refused", n);
		log_line(line);
		return;
	}
	cur_set = n;
	snprintf(line, sizeof(line), "set %d %s", n, set_names[n]);
	log_line(line);
}

int
main(void)
{
	struct sgttyb saved, raw;
	char c, line[80];
	time_t now = time(NULL);

	gfx = open("/dev/lcdgfx", O_WRONLY);
	if (gfx < 0) {
		perror("paneltest: /dev/lcdgfx");
		return 1;
	}
	if (ioctl(0, TIOCGETP, &saved) < 0) {
		perror("paneltest: tty");
		return 1;
	}
	raw = saved;
	raw.sg_flags |= RAW;
	raw.sg_flags &= ~ECHO;
	(void)ioctl(0, TIOCSETP, &raw);
	snprintf(line, sizeof(line), "paneltest start %s", ctime(&now));
	line[strcspn(line, "\n")] = '\0';
	log_line(line);
	readback();
	draw();
	while (read(0, &c, 1) == 1) {
		if (c == 'q' || c == 'Q' || c == 3)
			break;
		if (c == 'n')
			change_set((cur_set + 1) % PANELTEST_NSETS);
		else if (c == 'p')
			change_set((cur_set + PANELTEST_NSETS - 1) %
			    PANELTEST_NSETS);
		else if (c >= '1' && c <= '7')
			cur_pat = c - '0';
		else if (c == 'r') {
			readback();
			cur_pat = 1;
		} else
			continue;
		draw();
	}
	(void)ioctl(0, TIOCSETP, &saved);
	close(gfx);
	printf("paneltest: log in %s\n", LOG);
	return 0;
}
