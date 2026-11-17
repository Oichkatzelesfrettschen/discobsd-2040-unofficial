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
 * paneltest: the PicoCalc panel register test (sys/arch/rp2040/doc/
 * PANELTEST.md). It holds /dev/lcdgfx, so the kernel accepts
 * machdep.picocalc.panel_variant and panel_read, draws the calibration
 * patterns of the experiments, and logs every register set and readback to
 * /var/log/paneltest.log. Keys: the right and left arrows (or n and p)
 * step the register set, the up and down arrows (or 1 to 7) pick a
 * pattern, r reads the panel registers back, f measures the frame rate
 * through the scanline sample, q quits; the kernel programs the shipped
 * set again when the device closes. Every pattern shows the set number,
 * its name and the pattern number in the top left corner.
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
#include "panel_decode.h"

#define	NAME_ROW(name, pre, base, post)	name,
static const char *const set_names[] = { PANELTEST_SETS(NAME_ROW) };

#define	BLACK	0x0000
#define	WHITE	0xffff
#define	GRAY50	0x7bef		/* RGB565 15/31/15: 6-bit level 31 widened. */
#define	RED	0xf800
#define	GREEN	0x07e0
#define	BLUE	0x001f
#define	LOG	"/var/log/paneltest.log"
#define	NPAT	7

/* Arrow keys, after their ESC [ or ESC O prefix. */
#define	K_UP	0x100
#define	K_DOWN	0x101
#define	K_RIGHT	0x102
#define	K_LEFT	0x103

static int gfx, cur_set, cur_pat = 1;
static struct picocalc_panel_read rb;
static int have_rb;
static struct picocalc_panel_scan sc;
static struct pd_scan scan;
static int scan_set = -1;		/* Set the scan was taken under. */

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

	off = snprintf(buf, len, "%02x fb%02x h%x:", e->cmd, e->fb, e->how);
	for (i = 0; i < e->n && i < PANEL_READ_BYTES && off < (int)len; i++)
		off += snprintf(buf + off, len - (size_t)off, " %02x", e->data[i]);
}

/* The first plain read (no FBh, no variant) of cmd, or NULL. */
static const struct picocalc_panel_ent *
find_ent(int cmd)
{
	int i;

	for (i = 0; have_rb && i < rb.nent && i < PANEL_READ_N; i++)
		if (rb.ent[i].cmd == cmd && rb.ent[i].fb == 0 &&
		    rb.ent[i].how == 0)
			return &rb.ent[i];
	return NULL;
}

static void
hex(char *buf, size_t len, const unsigned char *p, int n)
{
	size_t off = strlen(buf);
	int i;

	for (i = 0; i < n && off < len; i++)
		off += (size_t)snprintf(buf + off, len - off, " %02x", p[i]);
}

/* A multi-byte read raw and shifted left one bit (the dummy clock). */
static void
raw_shift(char *buf, size_t len, const struct picocalc_panel_ent *e)
{
	unsigned char v[PANEL_READ_BYTES];
	int n = e->n < PANEL_READ_BYTES ? e->n : PANEL_READ_BYTES;

	pd_shift(e->data, n, v);
	snprintf(buf, len, "%02x raw", e->cmd);
	hex(buf, len, e->data, n);
	strncat(buf, " <<1", len - strlen(buf) - 1);
	hex(buf, len, v, n);
}

/*
 * The decoded readback, one line at a time to the log (y < 0) or to the
 * screen from line y; returns the next free line.
 */
static int
decoded(int y)
{
	const struct picocalc_panel_ent *e;
	char line[160], a[60], b[60];
	int i, k = 0;

	for (i = 0; i < 7; i++) {
		line[0] = '\0';
		switch (i) {
		case 0: case 1: case 5:
			e = find_ent(i == 0 ? 0x04 : i == 1 ? 0x09 : 0xd3);
			if (e != NULL)
				raw_shift(line, sizeof(line), e);
			break;
		case 2:
			if ((e = find_ent(0x09)) != NULL && e->n >= 5)
				pd_rddst(line, sizeof(line), e->data);
			break;
		case 3:
			if ((e = find_ent(0x0a)) != NULL) {
				pd_rddpm(a, sizeof(a), e->data[0]);
				snprintf(line, sizeof(line), "0a %02x %s",
				    e->data[0], a);
			}
			break;
		case 4:
			if ((e = find_ent(0x0b)) != NULL &&
			    find_ent(0x0c) != NULL) {
				pd_madctl(a, sizeof(a), e->data[0]);
				pd_colmod(b, sizeof(b), find_ent(0x0c)->data[0]);
				snprintf(line, sizeof(line), "0b %02x %s  0c %02x %s",
				    e->data[0], a, find_ent(0x0c)->data[0], b);
			}
			break;
		case 6:
			if (scan_set != cur_set)
				break;
			if (scan.ok)
				snprintf(line, sizeof(line),
				    "f %lu.%02lu+-%lu.%02lu Hz %u lines (%u unshifted)",
				    scan.chz / 100, scan.chz % 100,
				    scan.err_chz / 100, scan.err_chz % 100,
				    scan.lines, scan.lines_raw);
			else
				snprintf(line, sizeof(line),
				    "f no rate: %u reads %u wraps %u bad",
				    sc.samples, sc.wraps, sc.bad);
			break;
		}
		if (line[0] == '\0')
			continue;
		if (y < 0)
			log_line(line);
		else
			text(0, y + 10 * k, line, 15, 0);
		k++;
	}
	return y + 10 * k;
}

/*
 * Sample the scanline, then log and print the frame rate, the line count
 * and the measurement's terms.
 */
static void
measure(void)
{
	int mib[3] = { CTL_MACHDEP, CPU_PICOCALC, CPU_PICOCALC_PANEL_SCAN };
	size_t len = sizeof(sc);
	char line[200];

	if (sysctl(mib, 3, &sc, &len, NULL, 0) < 0) {
		snprintf(line, sizeof(line), "scan set %d failed", cur_set);
		scan_set = -1;
	} else {
		pd_scan(&scan, sc.samples, sc.wraps, sc.first_us, sc.last_us,
		    sc.elapsed_us, sc.max_gap_us, sc.raw_max);
		scan_set = cur_set;
		snprintf(line, sizeof(line), "scan set %d %s: %s%lu.%02lu Hz "
		    "+-%lu.%02lu, lines %u (unshifted %u, may miss %u); "
		    "reads %u every %u us, wraps %u at %u..%u us, bad %u, "
		    "gap %u us, elapsed %u us, raw_max %06x", cur_set,
		    set_names[cur_set], scan.ok ? "" : "no rate ",
		    scan.chz / 100, scan.chz % 100, scan.err_chz / 100,
		    scan.err_chz % 100, scan.lines, scan.lines_raw,
		    scan.lines_slack, sc.samples, scan.sample_us, sc.wraps,
		    sc.first_us, sc.last_us, sc.bad, sc.max_gap_us,
		    sc.elapsed_us, sc.raw_max);
	}
	log_line(line);
	printf("%s\r\n", line);
	fflush(stdout);
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
	(void)decoded(-1);
	(void)set_variant(cur_set);
}

/* The corner label: set number, name, pattern and any measured rate. */
static void
header(int y)
{
	char line[64];

	if (scan_set == cur_set && scan.ok)
		snprintf(line, sizeof(line), "%d/%d %.30s p%d %lu.%02luHz",
		    cur_set, PANELTEST_NSETS - 1, set_names[cur_set], cur_pat,
		    scan.chz / 100, scan.chz % 100);
	else
		snprintf(line, sizeof(line), "%d/%d %.30s p%d", cur_set,
		    PANELTEST_NSETS - 1, set_names[cur_set], cur_pat);
	fill(0, y, 319, y + 9, BLACK);
	text(0, y, line, 15, 0);
}

/* 1: a console text page with the latest readback. */
static void
pat_text(void)
{
	static const char *const sample[] = {
		"DiscoBSD 2.7 panel register test",
		"The quick brown fox jumps over the lazy dog 0123456789",
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz",
		"{}[]()<>|~^`'\"\\ Il1| O0o @#$%&* ;:,.!?-_=+/",
		"keys: <> n p set, ^v 1-7 pattern, r read, f rate, q",
	};
	char line[80];
	int i, y = 14;

	fill(0, 10, 319, 319, BLACK);
	for (i = 0; i < 5; i++, y += 10)
		text(0, y, sample[i], i == 4 ? 8 : 7, 0);
	y = decoded(y + 4) + 4;
	/* The raw entries in two 26-column columns. */
	for (i = 0; have_rb && i < rb.nent && i < PANEL_READ_N; i++) {
		format_ent(line, sizeof(line), &rb.ent[i]);
		line[26] = '\0';
		text(i % 2 ? 160 : 0, y + 10 * (i / 2), line, 7, 0);
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
	case 7:
		/* The glass starts at RAM line 160 here. */
		pat_lines();
		header(160);
		return;
	}
	header(0);
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
	scan_set = -1;
	snprintf(line, sizeof(line), "set %d %s", n, set_names[n]);
	log_line(line);
}

int
main(void)
{
	struct sgttyb saved, raw;
	char c, line[80];
	int k;
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
		k = (unsigned char)c;
		if (c == '\033') {
			/* ESC [ A-D or ESC O A-D; anything else is dropped. */
			if (read(0, &c, 1) != 1)
				break;
			if (c != '[' && c != 'O')
				continue;
			if (read(0, &c, 1) != 1)
				break;
			if (c < 'A' || c > 'D')
				continue;
			k = K_UP + (c - 'A');
		}
		if (k == 'q' || k == 'Q' || k == 3)
			break;
		if (k == 'n' || k == K_RIGHT)
			change_set((cur_set + 1) % PANELTEST_NSETS);
		else if (k == 'p' || k == K_LEFT)
			change_set((cur_set + PANELTEST_NSETS - 1) %
			    PANELTEST_NSETS);
		else if (k == K_UP)
			cur_pat = cur_pat == 1 ? NPAT : cur_pat - 1;
		else if (k == K_DOWN)
			cur_pat = cur_pat % NPAT + 1;
		else if (k >= '1' && k <= '0' + NPAT)
			cur_pat = k - '0';
		else if (k == 'r') {
			readback();
			cur_pat = 1;
		} else if (k == 'f')
			measure();
		else
			continue;
		draw();
	}
	(void)ioctl(0, TIOCSETP, &saved);
	close(gfx);
	printf("paneltest: log in %s\n", LOG);
	return 0;
}
