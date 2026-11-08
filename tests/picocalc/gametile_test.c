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
 * Host test of the tile library (games/gametile.h): the shadow map, the
 * bitmap of unsent tiles and the emission of one LCDG_TILEWIN per run.
 *
 * The library's two calls into the device are bound to a model of
 * /dev/lcdgfx before the header is included. The model applies the kernel's
 * contract: an ioctl range-checked as lcdgfx.c checks it, a following write
 * of tile numbers that fills a block row by row, and an atlas upload. It
 * logs every window and keeps the tile number each cell of the panel shows,
 * so the test compares what the panel shows with what the program asked for.
 *
 * The oracle for the number and shape of the windows is a boolean array the
 * test keeps beside the library: a cell is touched when its number differs
 * from the previous number asked for, and the expected windows are the
 * maximal runs of touched cells in each row, counted by a loop that shares
 * nothing with the library's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/lcdgfx.h>

static int model_ioctl(int fd, unsigned long cmd, void *arg);
static long model_write(int fd, const void *buf, size_t n);

#define GTILE_IOCTL(fd, cmd, arg)	model_ioctl((fd), (cmd), (arg))
#define GTILE_WRITE(fd, buf, n)		model_write((fd), (buf), (n))

#include "gametile.h"

#define	UNSEEN		0x100		/* A cell the panel was never sent. */
#define	LOG_MAX		4096

static int failures;

static void
expect(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

/* The model device. */
static int screen[GTILE_CELLS];		/* Tile number each cell shows. */
static unsigned char atlas[LCDG_ATLAS_N][LCDG_TILE_BYTES];
static struct {
	int tx, ty, tw, th;
} wlog[LOG_MAX];
static int nwin, ntiles, nset, nioctl, nwrite;
static int fail_window = -1;		/* Fail the ioctl of this window (0-based). */
static int short_write_at = -1;		/* Return a short count for this window. */
static int wx, wy, ww, wh, wleft, wfill;	/* The block being written. */
static unsigned char wrow[GTILE_COLS];
static unsigned tset_pos, tset_left;

static void
model_reset(void)
{
	int i;

	for (i = 0; i < GTILE_CELLS; i++)
		screen[i] = UNSEEN;
	memset(atlas, 0, sizeof(atlas));
	nwin = ntiles = nset = nioctl = nwrite = 0;
	fail_window = short_write_at = -1;
	wleft = 0;
	tset_left = 0;
}

static int
model_ioctl(int fd, unsigned long cmd, void *arg)
{
	const struct lcdg_tilewin *w = arg;
	const struct lcdg_tileset *t = arg;

	(void)fd;
	nioctl++;
	wleft = 0;
	tset_left = 0;
	if (cmd == LCDG_TILEWIN) {
		if (w->tw == 0 || w->th == 0 || w->tx + w->tw > LCDG_GRID ||
		    w->ty + w->th > LCDG_GRID) {
			fprintf(stderr, "FAIL model: LCDG_TILEWIN %d,%d %dx%d out of range\n",
			    w->tx, w->ty, w->tw, w->th);
			failures++;
			return -1;
		}
		if (nwin == fail_window)
			return -1;
		if (nwin < LOG_MAX) {
			wlog[nwin].tx = w->tx;
			wlog[nwin].ty = w->ty;
			wlog[nwin].tw = w->tw;
			wlog[nwin].th = w->th;
		}
		wx = w->tx;
		wy = w->ty;
		ww = w->tw;
		wh = w->th;
		wleft = ww * wh;
		wfill = 0;
		nwin++;
		return 0;
	}
	if (cmd == LCDG_TILESET) {
		if (t->count == 0 || t->first + t->count > LCDG_ATLAS_N) {
			fprintf(stderr, "FAIL model: LCDG_TILESET %d,%d out of range\n",
			    t->first, t->count);
			failures++;
			return -1;
		}
		tset_pos = (unsigned)t->first * LCDG_TILE_BYTES;
		tset_left = (unsigned)t->count * LCDG_TILE_BYTES;
		nset++;
		return 0;
	}
	fprintf(stderr, "FAIL model: unexpected command %lx\n", cmd);
	failures++;
	return -1;
}

static long
model_write(int fd, const void *buf, size_t n)
{
	const unsigned char *p = buf;
	size_t i;

	(void)fd;
	nwrite++;
	if (tset_left != 0) {
		if (n > tset_left)
			n = tset_left;
		memcpy((unsigned char *)atlas + tset_pos, p, n);
		tset_pos += (unsigned)n;
		tset_left -= (unsigned)n;
		return (long)n;
	}
	if (wleft == 0) {
		fprintf(stderr, "FAIL model: write with no stream open\n");
		failures++;
		return -1;
	}
	if (nwin - 1 == short_write_at && n > 1) {
		short_write_at = -1;
		n--;
	}
	if ((int)n > wleft)
		n = (size_t)wleft;
	for (i = 0; i < n; i++) {
		wrow[wfill++] = p[i];
		wleft--;
		if (wfill == ww) {
			int c;

			for (c = 0; c < ww; c++) {
				screen[wy * GTILE_COLS + wx + c] = wrow[c];
				ntiles++;
			}
			wfill = 0;
			wy++;
		}
	}
	return (long)n;
}

/* The test's own record: what was asked for, and which cells are owed. */
static int want[GTILE_CELLS];
static int touched[GTILE_CELLS];

static void
shadow_reset(struct gtile *g)
{
	int i;

	model_reset();
	gtile_init(g, 3);
	for (i = 0; i < GTILE_CELLS; i++) {
		want[i] = 0;
		touched[i] = 1;
	}
}

static int
set(struct gtile *g, int x, int y, int t)
{
	int r = gtile_set(g, x, y, t);

	if (r == 0 && want[y * GTILE_COLS + x] != t) {
		want[y * GTILE_COLS + x] = t;
		touched[y * GTILE_COLS + x] = 1;
	}
	return r;
}

/* Windows expected from the touched cells: maximal runs per row, raster order. */
static int
expected_windows(int out_x[], int out_y[], int out_w[])
{
	int x, y, n = 0;

	for (y = 0; y < GTILE_ROWS; y++) {
		for (x = 0; x < GTILE_COLS; x++) {
			if (touched[y * GTILE_COLS + x] &&
			    (x == 0 || !touched[y * GTILE_COLS + x - 1])) {
				int w = 0;

				while (x + w < GTILE_COLS && touched[y * GTILE_COLS + x + w])
					w++;
				out_x[n] = x;
				out_y[n] = y;
				out_w[n] = w;
				n++;
			}
		}
	}
	return n;
}

static int
count_touched(void)
{
	int i, n = 0;

	for (i = 0; i < GTILE_CELLS; i++)
		n += touched[i] != 0;
	return n;
}

/* Flush, and require the windows, the tiles sent and the panel to be right. */
static void
flush_check(const char *what, struct gtile *g)
{
	static int ex[GTILE_CELLS], ey[GTILE_CELLS], ew[GTILE_CELLS];
	int n, i, bad = 0, owed = count_touched();

	n = expected_windows(ex, ey, ew);
	nwin = ntiles = nioctl = nwrite = 0;
	expect(what, gtile_flush(g), n);
	expect("windows sent", nwin, n);
	expect("one ioctl per window", nioctl, n);
	expect("one write per window", nwrite, n);
	expect("tiles sent equal the tiles owed", ntiles, owed);
	for (i = 0; i < n && i < LOG_MAX; i++)
		if (wlog[i].tx != ex[i] || wlog[i].ty != ey[i] ||
		    wlog[i].tw != ew[i] || wlog[i].th != 1)
			bad++;
	expect("window geometry", bad, 0);
	bad = 0;
	for (i = 0; i < GTILE_CELLS; i++) {
		if (touched[i] && screen[i] != want[i])
			bad++;
		touched[i] = 0;
	}
	expect("every owed cell shows its number", bad, 0);
	expect("nothing pending after a flush", g->pending, 0);
}

static void
test_init_and_noop(void)
{
	static struct gtile g;
	int i, bad = 0;

	shadow_reset(&g);
	expect("init marks every cell", g.pending, GTILE_CELLS);
	flush_check("first flush paints the grid", &g);
	expect("one window per full row", nwin, GTILE_ROWS);
	for (i = 0; i < GTILE_CELLS; i++)
		if (screen[i] != 0)
			bad++;
	expect("the grid shows tile 0", bad, 0);

	nwin = nioctl = nwrite = 0;
	expect("flush with nothing pending", gtile_flush(&g), 0);
	expect("no call", nioctl + nwrite, 0);

	expect("set the same tile", set(&g, 3, 4, 0), 0);
	expect("an unchanged tile marks nothing", g.pending, 0);
	expect("flush after an unchanged set", gtile_flush(&g), 0);
	expect("no call after an unchanged set", nioctl + nwrite, 0);
}

static void
test_runs(void)
{
	static struct gtile g;
	int x;

	shadow_reset(&g);
	flush_check("clean", &g);

	/* One cell. */
	set(&g, 17, 9, 5);
	expect("one cell pending", g.pending, 1);
	flush_check("one cell", &g);
	expect("one window", nwin, 1);
	expect("window column", wlog[0].tx, 17);
	expect("window row", wlog[0].ty, 9);
	expect("window width", wlog[0].tw, 1);

	/* A run of eight is one window. */
	for (x = 5; x < 13; x++)
		set(&g, x, 3, 7 + x);
	flush_check("run of eight", &g);
	expect("one window for a run", nwin, 1);
	expect("run width", wlog[0].tw, 8);

	/* A gap of one clean cell makes two windows and sends no clean cell. */
	for (x = 5; x < 10; x++)
		set(&g, x, 6, 20);
	for (x = 11; x < 15; x++)
		set(&g, x, 6, 21);
	flush_check("gap", &g);
	expect("two windows around a gap", nwin, 2);
	expect("tiles sent skip the gap", ntiles, 9);

	/* The last cell of a row and the first of the next are two windows. */
	set(&g, 39, 12, 30);
	set(&g, 0, 13, 31);
	flush_check("row edge", &g);
	expect("two windows across a row edge", nwin, 2);

	/* A full row is one window of 40; a column is forty windows of 1. */
	for (x = 0; x < GTILE_COLS; x++)
		set(&g, x, 20, 40);
	flush_check("full row", &g);
	expect("one window for a full row", nwin, 1);
	expect("full row width", wlog[0].tw, 40);
	for (x = 0; x < GTILE_ROWS; x++)
		set(&g, 8, x, 50);
	flush_check("column", &g);
	expect("one window per row of a column", nwin, 40);

	/* A cell set to a new tile and back stays owed, the cost of comparing with the shadow. */
	set(&g, 2, 2, 60);
	set(&g, 2, 2, 50);
	set(&g, 2, 2, 60);
	flush_check("changed twice", &g);
	expect("one window", nwin, 1);
}

static void
test_random(void)
{
	static struct gtile g;
	int round, k, n, x, y, w, t;

	shadow_reset(&g);
	flush_check("clean", &g);
	for (round = 0; round < 300; round++) {
		n = (int)(rand() % 60);
		for (k = 0; k < n; k++) {
			x = rand() % GTILE_COLS;
			y = rand() % GTILE_ROWS;
			if (rand() % 3 == 0) {
				/* A run. */
				w = 1 + rand() % 12;
				t = rand() % LCDG_ATLAS_N;
				while (w-- > 0 && x < GTILE_COLS)
					set(&g, x++, y, t);
			} else {
				set(&g, x, y, rand() % LCDG_ATLAS_N);
			}
		}
		flush_check("random round", &g);
	}
}

static void
test_range(void)
{
	static struct gtile g;
	int i;

	shadow_reset(&g);
	flush_check("clean", &g);
	expect("x 40", gtile_set(&g, 40, 0, 1), -1);
	expect("y 40", gtile_set(&g, 0, 40, 1), -1);
	expect("x -1", gtile_set(&g, -1, 0, 1), -1);
	expect("y -1", gtile_set(&g, 0, -1, 1), -1);
	expect("tile 128", gtile_set(&g, 0, 0, 128), -1);
	expect("tile -1", gtile_set(&g, 0, 0, -1), -1);
	expect("tile 255", gtile_set(&g, 0, 0, 255), -1);
	expect("a refused set marks nothing", g.pending, 0);
	expect("a refused set changes nothing", gtile_get(&g, 0, 0), 0);
	expect("last cell, last tile", gtile_set(&g, 39, 39, 127), 0);
	expect("last cell reads back", gtile_get(&g, 39, 39), 127);
	expect("get outside", gtile_get(&g, 40, 0), -1);
	expect("get negative", gtile_get(&g, 0, -1), -1);
	want[39 * 40 + 39] = 127;
	touched[39 * 40 + 39] = 1;
	flush_check("last cell", &g);

	/* Fill: a rectangle, range checked as a whole. */
	expect("fill outside the grid", gtile_fill(&g, 35, 35, 6, 5, 1), -1);
	expect("fill outside, height", gtile_fill(&g, 35, 35, 5, 6, 1), -1);
	expect("fill with a bad tile", gtile_fill(&g, 0, 0, 2, 2, 128), -1);
	expect("fill negative origin", gtile_fill(&g, -1, 0, 2, 2, 1), -1);
	expect("a refused fill marks nothing", g.pending, 0);
	expect("fill the corner", gtile_fill(&g, 35, 35, 5, 5, 9), 0);
	for (i = 0; i < 25; i++) {
		want[(35 + i / 5) * 40 + 35 + i % 5] = 9;
		touched[(35 + i / 5) * 40 + 35 + i % 5] = 1;
	}
	flush_check("filled corner", &g);
	expect("five rows, five windows", nwin, 5);
	expect("an empty fill", gtile_fill(&g, 0, 0, 0, 0, 1), 0);
	expect("an empty fill marks nothing", g.pending, 0);
}

static void
test_errors(void)
{
	static struct gtile g;
	int x, i, bad = 0;

	/* The third window's ioctl fails: two runs are sent, the rest stay owed. */
	shadow_reset(&g);
	flush_check("clean", &g);
	for (i = 0; i < 5; i++)
		for (x = 0; x < 3; x++)
			set(&g, x, i * 2, 70 + i);
	nwin = 0;
	fail_window = 2;
	expect("flush stops at the failed ioctl", gtile_flush(&g), -1);
	expect("two windows were sent", nwin, 2);
	expect("nine cells stay owed", g.pending, 9);
	fail_window = -1;
	expect("the retry sends the rest", gtile_flush(&g), 3);
	for (i = 0; i < 5; i++)
		for (x = 0; x < 3; x++)
			if (screen[(i * 2) * GTILE_COLS + x] != 70 + i)
				bad++;
	expect("the panel is complete after the retry", bad, 0);
	expect("nothing owed", g.pending, 0);
	for (i = 0; i < GTILE_CELLS; i++)
		touched[i] = 0;

	/* A short write is an error and leaves its run owed. */
	for (x = 0; x < 4; x++)
		set(&g, 10 + x, 30, 80);
	nwin = 0;
	short_write_at = 0;
	expect("a short write fails the flush", gtile_flush(&g), -1);
	expect("the short run stays owed", g.pending, 4);
	short_write_at = -1;
	nwin = 0;
	expect("the retry sends the run", gtile_flush(&g), 1);
	expect("nothing owed after the retry", g.pending, 0);
	expect("the run is on the panel", screen[30 * GTILE_COLS + 13], 80);
}

static void
test_load(void)
{
	static struct gtile g;
	unsigned char data[LCDG_TILE_BYTES * 8];
	int i;

	shadow_reset(&g);
	flush_check("clean", &g);
	for (i = 0; i < (int)sizeof(data); i++)
		data[i] = (unsigned char)(i * 7 + 1);
	set(&g, 1, 1, 10);
	set(&g, 2, 1, 11);
	set(&g, 3, 1, 12);
	set(&g, 4, 1, 13);
	flush_check("tiles in use", &g);

	/* Loading tiles 11 and 12 marks the cells that show them, and no others. */
	expect("load tiles 11..12", gtile_load(&g, 11, 2, data), 0);
	expect("one tileset", nset, 1);
	expect("the atlas holds the data", memcmp(atlas[11], data, 2 * LCDG_TILE_BYTES), 0);
	expect("the neighbors are untouched", atlas[10][0] | atlas[13][0], 0);
	expect("two cells owed", g.pending, 2);
	touched[1 * 40 + 2] = touched[1 * 40 + 3] = 1;
	flush_check("cells of the loaded tiles", &g);
	expect("one run of two", nwin, 1);

	/* Loading a tile no cell shows marks nothing. */
	expect("load tile 99", gtile_load(&g, 99, 1, data), 0);
	expect("no cell shows tile 99", g.pending, 0);

	/* Range errors send nothing. */
	nioctl = nwrite = 0;
	expect("first 128", gtile_load(&g, 128, 1, data), -1);
	expect("count 0", gtile_load(&g, 0, 0, data), -1);
	expect("127 + 2", gtile_load(&g, 127, 2, data), -1);
	expect("negative first", gtile_load(&g, -1, 1, data), -1);
	expect("count 129", gtile_load(&g, 0, 129, data), -1);
	expect("no call for a refused range", nioctl + nwrite, 0);
	expect("the last tile", gtile_load(&g, 127, 1, data), 0);
	expect("the whole atlas upload size is 4096", LCDG_ATLAS_N * LCDG_TILE_BYTES, 4096);

	/* touch_all marks everything once. */
	gtile_touch_all(&g);
	expect("touch all", g.pending, GTILE_CELLS);
	for (i = 0; i < GTILE_CELLS; i++)
		touched[i] = 1;
	flush_check("repaint", &g);
	expect("a repaint of identical rows is forty windows", nwin, 40);
}

int
main(void)
{
	test_init_and_noop();
	test_runs();
	test_random();
	test_range();
	test_errors();
	test_load();
	if (failures != 0) {
		fprintf(stderr, "gametile_test: %d failures\n", failures);
		return 1;
	}
	printf("gametile_test: ok\n");
	return 0;
}
