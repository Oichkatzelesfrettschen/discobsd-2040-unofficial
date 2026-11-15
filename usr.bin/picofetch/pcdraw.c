/*
 * The drawing tiers of pcdraw.h. Output goes through stdio's stdout; the
 * caller flushes. The host build (HOSTBUILD) has no glyph bank ioctl: it
 * takes the bank tier when PCDRAW_GLYPHLOG names a file and appends each
 * upload there as "code row0 .. row8" in hexadecimal, which is what the
 * host test reads back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifndef HOSTBUILD
#include <errno.h>
#include <sys/lcdcons.h>
#endif

#include "pcdraw.h"

#ifndef HOSTBUILD
static int	lit_fd = -1;		/* tty whose LITOUT pc_init() set */
#endif
static int	sgr_on;			/* colors were written */

int
pc_glyph_rows(int code, unsigned char rows[PC_GLYPH_ROWS])
{
	int i, n;

	memset(rows, 0, PC_GLYPH_ROWS);
	if (code >= PC_HBAR && code < PC_HBAR + 6) {
		n = code - PC_HBAR + 1;
		for (i = 0; i < PC_GLYPH_ROWS; i++)
			rows[i] = (unsigned char)(0xfc << (6 - n) & 0xfc);
	} else if (code >= PC_VBAR && code < PC_VBAR + 9) {
		n = code - PC_VBAR + 1;
		for (i = PC_GLYPH_ROWS - n; i < PC_GLYPH_ROWS; i++)
			rows[i] = 0xfc;
	} else if (code == PC_TRACK) {
		rows[3] = rows[4] = rows[5] = 0xfc;
	} else if (code >= PC_MOSAIC && code < PC_MOSAIC + 63) {
		n = code - PC_MOSAIC + 1;
		for (i = 0; i < PC_GLYPH_ROWS; i++) {
			int pair = n >> (i / 3 * 2);

			rows[i] = (unsigned char)((pair & 1 ? 0xe0 : 0) |
			    (pair & 2 ? 0x1c : 0));
		}
	} else
		return 0;
	return 1;
}

/* Upload every shape; 1 when the terminal on fd has a glyph bank. */
static int
bank_upload(int fd)
{
	int code;
#ifdef HOSTBUILD
	const char *path = getenv("PCDRAW_GLYPHLOG");
	FILE *log;
	unsigned char rows[PC_GLYPH_ROWS];
	int i;

	(void)fd;
	if (path == NULL || (log = fopen(path, "a")) == NULL)
		return 0;
	for (code = PC_HBAR; code < PC_MOSAIC + 63; code++) {
		if (!pc_glyph_rows(code, rows))
			continue;
		fprintf(log, "%02x", code);
		for (i = 0; i < PC_GLYPH_ROWS; i++)
			fprintf(log, " %02x", rows[i]);
		fputc('\n', log);
	}
	fclose(log);
	return 1;
#else
	struct lcd_glyph g;
	int tries;

	for (code = PC_HBAR; code < PC_MOSAIC + 63; code++) {
		if (!pc_glyph_rows(code, g.rows))
			continue;
		g.code = (u_char)code;
		/* EAGAIN: another drawer holds the panel for a moment. */
		for (tries = 0; ioctl(fd, LCDIOC_SETGLYPH, &g) < 0; tries++) {
			if (errno != EAGAIN || tries == 50)
				return 0;
			usleep(2000);
		}
	}
	return 1;
#endif
}

void
pc_init(struct pcterm *t, int fd, int color, int plain)
{
	struct winsize ws;
	const char *s;

	memset(t, 0, sizeof *t);
	t->rows = 24;
	t->cols = 80;
	t->tier = PC_PLAIN;
#ifdef HOSTBUILD
	/* PCDRAW_TTY=ROWSxCOLS: treat a pipe as a terminal of that size. */
	if (!plain && (s = getenv("PCDRAW_TTY")) != NULL &&
	    sscanf(s, "%dx%d", &t->rows, &t->cols) == 2) {
		t->tier = PC_VT;
		t->color = color < 0 ? getenv("NO_COLOR") == NULL : color;
		if (bank_upload(fd)) {
			t->tier = PC_BANK;
			t->xpixel = t->cols * 6;
			t->ypixel = t->rows * 10;
		}
		sgr_on = t->color;
		return;
	}
#endif
	if (!isatty(fd) || plain) {
		if ((s = getenv("COLUMNS")) != NULL && atoi(s) >= 20)
			t->cols = atoi(s);
		return;
	}
	if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
		t->rows = ws.ws_row;
		t->cols = ws.ws_col;
		t->xpixel = ws.ws_xpixel;
		t->ypixel = ws.ws_ypixel;
	}
	t->tier = PC_VT;
	t->color = color < 0 ? getenv("NO_COLOR") == NULL : color;
	if (bank_upload(fd)) {
		t->tier = PC_BANK;
#ifndef HOSTBUILD
		{
			int bits = LLITOUT;

			if (ioctl(fd, TIOCLBIS, &bits) == 0)
				lit_fd = fd;
		}
#endif
	}
	sgr_on = t->color;
}

void
pc_done(void)
{
#ifndef HOSTBUILD
	if (lit_fd >= 0) {
		int bits = LLITOUT;

		(void)ioctl(lit_fd, TIOCLBIC, &bits);
		lit_fd = -1;
	}
#endif
	if (sgr_on) {
		static const char off[] = "\033[0m";
		ssize_t r = write(1, off, sizeof off - 1);

		(void)r;
		sgr_on = 0;
	}
}

void
pc_fg(const struct pcterm *t, int color)
{
	if (t->color)
		printf("\033[%dm", (color & 8 ? 90 : 30) + (color & 7));
}

void
pc_bg(const struct pcterm *t, int color)
{
	if (t->color)
		printf("\033[%dm", 40 + (color & 7));
}

void
pc_bold(const struct pcterm *t)
{
	if (t->tier != PC_PLAIN)
		fputs("\033[1m", stdout);
}

void
pc_reverse(const struct pcterm *t)
{
	if (t->tier != PC_PLAIN)
		fputs("\033[7m", stdout);
}

void
pc_reset(const struct pcterm *t)
{
	if (t->tier != PC_PLAIN)
		fputs("\033[0m", stdout);
}

int
pc_sixths(int cells, long part, long total)
{
	long span = (long)cells * 6;

	if (total <= 0 || part <= 0)
		return 0;
	if (part >= total)
		return (int)span;
	while (total > 1L << 20) {
		total >>= 1;
		part >>= 1;
	}
	/* Split so the product stays inside a long for 32-bit counters. */
	return (int)(part / total * span + part % total * span / total);
}

void
pc_bar(const struct pcterm *t, int cells, int nseg, const long *val,
    const int *col, long total)
{
	int edge[8], i, c, seg;
	long sum = 0;

	if (nseg > 8)
		nseg = 8;
	for (i = 0; i < nseg; i++) {
		sum += val[i] > 0 ? val[i] : 0;
		edge[i] = pc_sixths(cells, sum, total);
	}
	for (c = 0; c < cells; c++) {
		int lo = c * 6, hi = lo + 6, here = -1, next = -1, cut = 6;

		/* The segment at the cell's left edge, and the one after. */
		for (seg = 0; seg < nseg; seg++)
			if (edge[seg] > lo) {
				here = seg;
				break;
			}
		if (here >= 0 && edge[here] < hi) {
			cut = edge[here] - lo;
			for (seg = here + 1; seg < nseg; seg++)
				if (edge[seg] > edge[here]) {
					next = seg;
					break;
				}
		}
		if (t->tier != PC_BANK) {
			if (here >= 0 && cut >= 3) {
				pc_fg(t, col[here]);
				putchar('#');
			} else if (next >= 0) {
				pc_fg(t, col[next]);
				putchar('#');
			} else {
				pc_fg(t, 8);
				putchar('.');
			}
			continue;
		}
		if (here < 0) {
			pc_fg(t, 8);
			pc_bg(t, 0);
			putchar(PC_TRACK);
			continue;
		}
		pc_fg(t, col[here]);
		pc_bg(t, next >= 0 ? col[next] : 0);
		putchar(PC_HBAR + cut - 1);
	}
	pc_reset(t);
}

void
pc_vcell(const struct pcterm *t, int level)
{
	static const char ascii[] = " ._-:=+*#@";

	if (level < 0)
		level = 0;
	if (level > 9)
		level = 9;
	if (t->tier == PC_BANK)
		putchar(level == 0 ? ' ' : PC_VBAR + level - 1);
	else
		putchar(ascii[level]);
}

void
pc_mosaic(const struct pcterm *t, int mask)
{
	int bits = 0, m;

	mask &= 63;
	if (t->tier == PC_BANK) {
		putchar(mask == 0 ? ' ' : PC_MOSAIC + mask - 1);
		return;
	}
	for (m = mask; m != 0; m >>= 1)
		bits += m & 1;
	putchar(bits >= 4 ? '#' : bits >= 2 ? '+' : bits >= 1 ? '.' : ' ');
}

void
pc_nl(const struct pcterm *t)
{
	if (t->tier == PC_BANK)
		putchar('\r');
	putchar('\n');
}

void
pc_size(char *buf, size_t len, long kb)
{
	if (kb < 0)
		kb = 0;
	if (kb < 10000)
		snprintf(buf, len, "%ldK", kb);
	else if (kb < 1000L * 1024)
		snprintf(buf, len, "%ld.%ldM", kb / 1024, kb % 1024 * 10 / 1024);
	else
		snprintf(buf, len, "%ld.%ldG", kb / (1024L * 1024),
		    kb / 1024 % 1024 * 10 / 1024);
}

void
pc_uptime(char *buf, size_t len, long secs)
{
	if (secs < 0)
		secs = 0;
	if (secs >= 86400)
		snprintf(buf, len, "%ldd %02ld:%02ld:%02ld", secs / 86400,
		    secs % 86400 / 3600, secs % 3600 / 60, secs % 60);
	else
		snprintf(buf, len, "%02ld:%02ld:%02ld", secs / 3600,
		    secs % 3600 / 60, secs % 60);
}

void
pc_rule(const struct pcterm *t, int n)
{
	/* 'q' of the DEC special graphics set is the horizontal line. */
	if (t->tier != PC_PLAIN)
		fputs("\033(0", stdout);
	while (n-- > 0)
		putchar(t->tier != PC_PLAIN ? 'q' : '-');
	if (t->tier != PC_PLAIN)
		fputs("\033(B", stdout);
}
