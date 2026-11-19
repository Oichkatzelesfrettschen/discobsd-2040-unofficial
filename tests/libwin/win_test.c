/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Host gate for lib/libwin: the library, with its /dev/lcdgfx backend, runs
 * against a model of the panel and an independent per-pixel reference.
 *
 * The model panel implements the LCDG_WINDOW / LCDG_WIN_INDEX / LCDG_PALETTE
 * subset of sys/lcdgfx.h at the system-call boundary: the backend source is
 * compiled into this program with ioctl(2) and write(2) replaced by the
 * model's. The model fails the run on a window outside the panel, a command
 * issued inside a half-filled window, a write with no window open or past
 * its last pixel, a pixel byte above 15, and any pixel written outside the
 * damage the test has itself recorded since the last flush.
 *
 * The reference painter applies the painter's algorithm over the test's own
 * copy of the scene (windows bottom to top, each pixel decided by the
 * window's geometry rules and its list of client operations). It shares
 * the font table with the library and nothing else: not the clipping, the
 * damage list, the strip, or the drawing calls.
 *
 * After every operation the test requires that the model's pixels differ
 * from the reference only inside the pending damage, that the strip's guard
 * bytes and the memory after the scene are intact, and, after a flush, that
 * the two images are equal. Fixed seed; one pass is deterministic.
 *
 * usage: win_test [OPERATIONS]
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/lcdgfx.h>

#include "win.h"
#include "lcdfont.h"

#define PW		WIN_PANEL_W
#define PH		WIN_PANEL_H
#define MODEL_FD	7
#define WRITE_CAP	1000		/* The model accepts short writes. */
#define NOPS		5
#define PAD_BYTES	2048
#define UNWRITTEN	0xff

/* The test's own copy of the library's drawing rules. */
#define R_DESKTOP	6
#define R_FRAME		0
#define R_TITLE_BG	4
#define R_TITLE_FG	15
#define R_CLIENT	7
#define R_TITLE_ROWS	11
#define R_TITLE_MAX	23

/* Model panel */

static unsigned char panel[PW * PH];
static unsigned char mask[PW * PH];	/* Pending damage, from the test's own rules. */
static int mask_all;			/* Measurements do not check the mask. */
static int win_open, win_x, win_y, win_w, win_h;
static long win_pos;
static long n_ioctl_window, n_bytes, n_ioctl_palette, n_writes;
static int palette_seen[16][3];
static long op_index;

static char trail[4][96];		/* The last four operations. */
static long trail_n;

static void
note(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(trail[trail_n++ % 4], sizeof(trail[0]), fmt, ap);
	va_end(ap);
}

static void
fail(const char *fmt, ...)
{
	va_list ap;
	long i;

	for (i = trail_n > 4 ? trail_n - 4 : 0; i < trail_n; i++)
		printf("  after: %s\n", trail[i % 4]);
	printf("FAIL op %ld: ", op_index);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	exit(1);
}

static int
model_ioctl(int fd, unsigned long req, void *arg)
{
	const struct lcdg_window *w = arg;
	const struct lcdg_palette *p = arg;

	if (fd != MODEL_FD)
		fail("ioctl on descriptor %d", fd);
	if (win_open && win_pos != (long)win_w * win_h)
		fail("command inside a half-filled window (%ld of %ld bytes)",
		    win_pos, (long)win_w * win_h);
	win_open = 0;
	if (req == LCDG_WINDOW) {
		if (w->w == 0 || w->h == 0 || w->x + w->w > PW ||
		    w->y + w->h > PH)
			fail("window %u,%u %ux%u leaves the panel", w->x, w->y,
			    w->w, w->h);
		if (w->fmt != LCDG_WIN_INDEX)
			fail("window format %u, expected LCDG_WIN_INDEX", w->fmt);
		win_open = 1;
		win_x = w->x;
		win_y = w->y;
		win_w = w->w;
		win_h = w->h;
		win_pos = 0;
		n_ioctl_window++;
		return 0;
	}
	if (req == LCDG_PALETTE) {
		if (p->index >= 16)
			fail("palette index %u", p->index);
		palette_seen[p->index][0] = p->r;
		palette_seen[p->index][1] = p->g;
		palette_seen[p->index][2] = p->b;
		n_ioctl_palette++;
		return 0;
	}
	fail("unexpected ioctl %lx", req);
	return -1;
}

static ssize_t
model_write(int fd, const void *buf, size_t n)
{
	const unsigned char *b = buf;
	long left, i;
	int x, y;

	if (fd != MODEL_FD)
		fail("write on descriptor %d", fd);
	if (!win_open) {
		errno = EINVAL;
		return -1;
	}
	left = (long)win_w * win_h - win_pos;
	if ((long)n > left)
		fail("write of %zu bytes with %ld pixels left", n, left);
	if (n > WRITE_CAP)
		n = WRITE_CAP;
	for (i = 0; i < (long)n; i++, win_pos++) {
		x = win_x + (int)(win_pos % win_w);
		y = win_y + (int)(win_pos / win_w);
		if (b[i] > 15)
			fail("pixel byte %u at %d,%d", b[i], x, y);
		if (!mask_all && !mask[y * PW + x])
			fail("pixel %d,%d written outside the damage", x, y);
		panel[y * PW + x] = b[i];
	}
	n_bytes += (long)n;
	n_writes++;
	return (ssize_t)n;
}

#define ioctl model_ioctl
#define write model_write
#include "win_lcdgfx.c"
#undef ioctl
#undef write

/* Test scene */

struct cop {
	int kind, x, y, n, m, color;	/* 0 fill, 1 hline, 2 vline, 3 text */
	char text[10];
};

struct twin {
	int used, x, y, w, h, nops;
	char title[40];
	struct cop ops[NOPS];
	struct win *handle;
};

static struct {
	struct win_scene s;
	unsigned char pad[PAD_BYTES];
} holder;
#define scene (holder.s)

static struct win_lcdgfx lcd;
static struct win_backend backend;
static struct twin tw[WIN_MAX];
static int order[WIN_MAX], norder;	/* Test z-order, bottom first. */
static unsigned char ref[PW * PH];

static uint32_t rng = 0x2f6e2b1u;

static uint32_t
rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

static int
rr(int lo, int hi)
{
	return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

static void
paint_cb(struct win_ctx *c, void *arg)
{
	const struct twin *t = arg;
	const struct cop *o;
	int i;

	/*
	 * A flush that a call forces renders the library's geometry, which
	 * the test's copy matches only after the call returns; the image
	 * checks compare the sizes through the pixels.
	 */
	if (c->w < 1 || c->h < 1)
		fail("client size %dx%d", c->w, c->h);
	for (i = 0; i < t->nops; i++) {
		o = &t->ops[i];
		switch (o->kind) {
		case 0:
			win_fill(c, o->x, o->y, o->n, o->m, o->color);
			break;
		case 1:
			win_hline(c, o->x, o->y, o->n, o->color);
			break;
		case 2:
			win_vline(c, o->x, o->y, o->n, o->color);
			break;
		default:
			win_text(c, o->x, o->y, o->text, o->color);
			break;
		}
	}
}

/* Reference painter */

static int
glyph_on(const char *s, int maxlen, int px, int py)
{
	unsigned ch;
	int i, len;

	if (py < 0 || py >= 9 || px < 0)
		return 0;
	for (len = 0; len < maxlen && s[len] != '\0'; len++)
		;
	i = px / 6;
	if (i >= len)
		return 0;
	ch = (unsigned char)s[i];
	if (ch < 32 || ch > 126)
		ch = '?';
	return (lcdfont[ch - 32][py] >> (7 - px % 6)) & 1;
}

/* The color of window t at (rx, ry) from its top left corner. */
static int
ref_window_pixel(const struct twin *t, int rx, int ry)
{
	const struct cop *o;
	int color, cx, cy, i, inside;

	if (rx >= 1 && rx < t->w - 1 && ry >= 1 && ry <= R_TITLE_ROWS) {
		if (glyph_on(t->title, R_TITLE_MAX, rx - 3, ry - 2))
			return R_TITLE_FG;
		return R_TITLE_BG;
	}
	if (rx < 1 || rx >= t->w - 1 || ry < R_TITLE_ROWS + 1 ||
	    ry >= t->h - 1)
		return R_FRAME;
	color = R_CLIENT;
	cx = rx - 1;
	cy = ry - (R_TITLE_ROWS + 1);
	for (i = 0; i < t->nops; i++) {
		o = &t->ops[i];
		inside = 0;
		switch (o->kind) {
		case 0:
			inside = o->n > 0 && o->m > 0 && cx >= o->x &&
			    cx < o->x + o->n && cy >= o->y && cy < o->y + o->m;
			break;
		case 1:
			inside = o->n > 0 && cx >= o->x && cx < o->x + o->n &&
			    cy == o->y;
			break;
		case 2:
			inside = o->n > 0 && cy >= o->y && cy < o->y + o->n &&
			    cx == o->x;
			break;
		default:
			inside = glyph_on(o->text, 10, cx - o->x, cy - o->y);
			break;
		}
		if (inside)
			color = o->color & 15;
	}
	return color;
}

static void
ref_render(void)
{
	const struct twin *t;
	int k, x, y;

	memset(ref, R_DESKTOP, sizeof(ref));
	for (k = 0; k < norder; k++) {
		t = &tw[order[k]];
		for (y = t->y < 0 ? 0 : t->y; y < t->y + t->h && y < PH; y++)
			for (x = t->x < 0 ? 0 : t->x; x < t->x + t->w && x < PW; x++)
				ref[y * PW + x] = (unsigned char)
				    ref_window_pixel(t, x - t->x, y - t->y);
	}
}

/* Checks */

static void
mark(int x0, int y0, int x1, int y1)
{
	int x, y;

	for (y = y0 < 0 ? 0 : y0; y < y1 && y < PH; y++)
		for (x = x0 < 0 ? 0 : x0; x < x1 && x < PW; x++)
			mask[y * PW + x] = 1;
}

static void
mark_window(const struct twin *t)
{
	mark(t->x, t->y, t->x + t->w, t->y + t->h);
}

static void
check_memory(void)
{
	int i;

	for (i = 0; i < WIN_GUARD; i++)
		if (scene.buf[i] != WIN_GUARD_BYTE ||
		    scene.buf[WIN_GUARD + WIN_STRIP_BYTES + i] != WIN_GUARD_BYTE)
			fail("strip guard byte %d overwritten", i);
	for (i = 0; i < PAD_BYTES; i++)
		if (holder.pad[i] != 0xcc)
			fail("memory after the scene overwritten at +%d", i);
	if (scene.ndamage < 0 || scene.ndamage > WIN_DAMAGE_MAX)
		fail("damage count %d", scene.ndamage);
}

/* On a mismatch, name the test's windows that cover the pixel. */
static void
describe_pixel(int x, int y)
{
	const struct twin *t;
	int k;

	for (k = 0; k < norder; k++) {
		t = &tw[order[k]];
		if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h)
			printf("  window %d at %d,%d %dx%d covers it; library has "
			    "%d,%d %dx%d\n", order[k], t->x, t->y, t->w, t->h,
			    t->handle->x, t->handle->y, t->handle->w, t->handle->h);
		if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h) {
			int i;

			printf("    title \"%s\"\n", t->title);
			for (i = 0; i < t->nops; i++)
				printf("    op %d kind %d at %d,%d n %d m %d color %d \"%s\"\n",
				    i, t->ops[i].kind, t->ops[i].x, t->ops[i].y,
				    t->ops[i].n, t->ops[i].m, t->ops[i].color,
				    t->ops[i].text);
		}
	}
}

static void
check_image(int exact)
{
	int p;

	ref_render();
	for (p = 0; p < PW * PH; p++) {
		if (panel[p] == ref[p])
			continue;
		if (exact || !mask[p]) {
			describe_pixel(p % PW, p / PW);
			fail("pixel %d,%d is %u, reference %u%s", p % PW, p / PW,
			    panel[p], ref[p],
			    exact ? " after a flush" : " outside the pending damage");
		}
	}
}

static void
check_scene_mirror(void)
{
	int k;

	if (scene.nwin != norder)
		fail("library holds %d windows, test %d", scene.nwin, norder);
	for (k = 0; k < norder; k++)
		if (&scene.wins[scene.z[k]] != tw[order[k]].handle)
			fail("z position %d holds another window", k);
}

static void
after_op(int flushed)
{
	check_memory();
	check_scene_mirror();
	if (flushed) {
		if (scene.ndamage != 0)
			fail("damage list not empty after a flush");
		memset(mask, 0, sizeof(mask));
		check_image(1);
	} else {
		check_image(0);
	}
}

/* Operations */

static void
reset_world(void)
{
	memset(panel, UNWRITTEN, sizeof(panel));
	memset(mask, 1, sizeof(mask));
	memset(tw, 0, sizeof(tw));
	memset(holder.pad, 0xcc, sizeof(holder.pad));
	norder = 0;
	win_open = 0;
	n_ioctl_window = n_bytes = n_ioctl_palette = n_writes = 0;
	win_lcdgfx_init(&backend, &lcd, MODEL_FD);
	if (win_scene_init(&scene, &backend) != 0)
		fail("win_scene_init");
}

static void
random_title(char *t)
{
	int n = rr(0, 30), i;

	for (i = 0; i < n; i++)
		t[i] = (char)(rr(0, 40) == 0 ? rr(1, 255) : rr(32, 126));
	t[n] = '\0';
	for (i = 0; i < n; i++)
		if (t[i] == '\0')
			t[i] = 'x';
}

static void
random_ops(struct twin *t)
{
	struct cop *o;
	int i, k;

	t->nops = rr(0, NOPS);
	for (i = 0; i < t->nops; i++) {
		o = &t->ops[i];
		o->kind = rr(0, 3);
		o->x = rr(-12, t->w + 6);
		o->y = rr(-12, t->h + 6);
		o->n = rr(-3, 3 * 40);
		o->m = rr(-3, 80);
		o->color = rr(0, 31);
		for (k = 0; k < 9; k++)
			o->text[k] = (char)(rr(0, 30) == 0 ? rr(1, 255) : rr(32, 126));
		o->text[9] = '\0';
		o->text[rr(1, 9)] = '\0';
	}
}

static int
pick_window(void)
{
	return norder == 0 ? -1 : order[rr(0, norder - 1)];
}

static void
order_remove(int id)
{
	int k;

	for (k = 0; order[k] != id; k++)
		;
	for (; k + 1 < norder; k++)
		order[k] = order[k + 1];
	norder--;
}

static void
op_create(void)
{
	struct twin *t;
	int id;

	for (id = 0; id < WIN_MAX && tw[id].used; id++)
		;
	if (id == WIN_MAX) {
		if (win_create(&scene, 10, 10, 50, 50, "full", NULL, NULL) != NULL)
			fail("a ninth window was created");
		return;
	}
	t = &tw[id];
	t->used = 1;
	t->x = rr(-40, PW + 10);
	t->y = rr(-40, PH + 10);
	t->w = rr(8, 150);
	t->h = rr(14, 130);
	random_title(t->title);
	random_ops(t);
	mark_window(t);
	note("create %d at %d,%d %dx%d", id, t->x, t->y, t->w, t->h);
	t->handle = win_create(&scene, t->x, t->y, t->w, t->h, t->title,
	    paint_cb, t);
	if (t->handle == NULL)
		fail("win_create refused a valid window");
	order[norder++] = id;
}

static void
op_move(void)
{
	struct twin *t;
	int id = pick_window();

	if (id < 0)
		return;
	t = &tw[id];
	mark_window(t);
	note("move %d from %d,%d", id, t->x, t->y);
	if (rr(0, 1)) {
		t->x += rr(-12, 12);
		t->y += rr(-12, 12);
	} else {
		t->x = rr(-40, PW + 10);
		t->y = rr(-40, PH + 10);
	}
	mark_window(t);
	if (win_move(&scene, t->handle, t->x, t->y) != 0)
		fail("win_move");
}

static void
op_resize(void)
{
	struct twin *t;
	int id = pick_window();

	if (id < 0)
		return;
	t = &tw[id];
	mark_window(t);
	note("resize %d from %dx%d", id, t->w, t->h);
	t->w = rr(8, 150);
	t->h = rr(14, 130);
	mark_window(t);
	if (win_resize(&scene, t->handle, t->w, t->h) != 0)
		fail("win_resize");
}

static void
op_raise(void)
{
	struct twin *t;
	int id = pick_window();

	if (id < 0)
		return;
	t = &tw[id];
	mark_window(t);
	note("raise %d", id);
	order_remove(id);
	order[norder++] = id;
	if (win_raise(&scene, t->handle) != 0)
		fail("win_raise");
}

static void
op_destroy(void)
{
	struct twin *t;
	int id = pick_window();

	if (id < 0)
		return;
	t = &tw[id];
	mark_window(t);
	note("destroy %d", id);
	order_remove(id);
	if (win_destroy(&scene, t->handle) != 0)
		fail("win_destroy");
	memset(t, 0, sizeof(*t));
}

static void
op_invalidate(void)
{
	int x = rr(-20, PW + 10), y = rr(-20, PH + 10);
	int w = rr(1, 60), h = rr(1, 40);

	if (rr(0, 20) == 0) {
		x = y = -5;
		w = h = PW + 20;
	}
	mark(x, y, x + w, y + h);
	note("invalidate %d,%d %dx%d", x, y, w, h);
	if (win_invalidate(&scene, x, y, w, h) != 0)
		fail("win_invalidate");
}

static void
op_title(void)
{
	struct twin *t;
	int id = pick_window();

	if (id < 0)
		return;
	t = &tw[id];
	note("title %d", id);
	mark(t->x + 1, t->y + 1, t->x + t->w - 1, t->y + 1 + R_TITLE_ROWS);
	random_title(t->title);
	if (win_set_title(&scene, t->handle, t->title) != 0)
		fail("win_set_title");
}

static void
run_random(long nops)
{
	long i;
	int r, flushed;

	for (i = 0; i < nops; i++) {
		op_index = i;
		r = rr(0, 99);
		flushed = 0;
		if (r < 14)
			op_create();
		else if (r < 34)
			op_move();
		else if (r < 44)
			op_resize();
		else if (r < 56)
			op_raise();
		else if (r < 63)
			op_destroy();
		else if (r < 80)
			op_invalidate();
		else if (r < 85)
			op_title();
		else {
			note("flush");
			if (win_flush(&scene) != 0)
				fail("win_flush");
			flushed = 1;
		}
		after_op(flushed);
	}
	op_index = nops;
	if (win_flush(&scene) != 0)
		fail("final win_flush");
	after_op(1);
}

/* Invalidate two rectangles, flush, and count the windows and bytes sent. */
static void
damage_pair(int x0, int y0, int w0, int h0, int x1, int y1, int w1, int h1,
    long windows, long bytes)
{
	long w, b;

	mark(x0, y0, x0 + w0, y0 + h0);
	mark(x1, y1, x1 + w1, y1 + h1);
	w = n_ioctl_window;
	b = n_bytes;
	if (win_invalidate(&scene, x0, y0, w0, h0) != 0 ||
	    win_invalidate(&scene, x1, y1, w1, h1) != 0 || win_flush(&scene) != 0)
		fail("damage pair");
	if (n_ioctl_window - w != windows || n_bytes - b != bytes)
		fail("rectangles %d,%d %dx%d and %d,%d %dx%d sent %ld windows and %ld "
		    "bytes, expected %ld and %ld", x0, y0, w0, h0, x1, y1, w1, h1,
		    n_ioctl_window - w, n_bytes - b, windows, bytes);
	after_op(1);
}

/* Twenty scattered rectangles overflow the damage list. */
static void
run_burst(void)
{
	int i, flushes_before;
	long burst_windows;

	op_index = -1;
	reset_world();
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	flushes_before = (int)n_ioctl_window;
	for (i = 0; i < 20; i++) {
		mark(i * 15, (i * 37) % 300, i * 15 + 4, (i * 37) % 300 + 4);
		if (win_invalidate(&scene, i * 15, (i * 37) % 300, 4, 4) != 0)
			fail("win_invalidate");
		check_memory();
	}
	if ((int)n_ioctl_window == flushes_before)
		fail("a full damage list did not flush");
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	burst_windows = n_ioctl_window - flushes_before;

	/* Rectangles with a gap are not merged: only their pixels are written. */
	damage_pair(10, 10, 20, 10, 35, 10, 20, 10, 2, 400);
	damage_pair(100, 100, 10, 20, 100, 125, 10, 20, 2, 400);
	/* Touching rectangles merge into one window. */
	damage_pair(10, 10, 20, 10, 30, 10, 20, 10, 1, 400);
	damage_pair(100, 100, 10, 20, 100, 120, 10, 20, 1, 400);
	/* A partial overlap is trimmed, so the shared pixels go out once. */
	damage_pair(0, 0, 50, 50, 25, 10, 50, 30, 2, 2500 + 25 * 30);
	damage_pair(25, 10, 50, 30, 0, 0, 50, 50, 2, 2500 + 25 * 30);
	printf("PASS burst: 20 scattered rectangles sent %ld windows; gap, "
	    "touch and overlap cases exact\n", burst_windows);
}

/* Calls that must refuse. */
static void
run_api(void)
{
	struct win *w;
	int i;

	reset_world();
	op_index = -2;
	if (win_create(&scene, 0, 0, WIN_MIN_W - 1, 40, "t", NULL, NULL) != NULL ||
	    win_create(&scene, 0, 0, 40, WIN_MIN_H - 1, "t", NULL, NULL) != NULL ||
	    win_create(&scene, 0, 0, WIN_LIMIT + 1, 40, "t", NULL, NULL) != NULL ||
	    win_create(&scene, WIN_LIMIT + 1, 0, 40, 40, "t", NULL, NULL) != NULL)
		fail("win_create accepted an invalid window");
	w = win_create(&scene, 0, 0, WIN_MIN_W, WIN_MIN_H, "t", NULL, NULL);
	if (w == NULL)
		fail("smallest window refused");
	if (win_resize(&scene, w, 7, 40) != -1 || win_move(&scene, w, 9999, 0) != -1 ||
	    win_destroy(&scene, NULL) != -1 || win_raise(&scene, NULL) != -1)
		fail("an invalid call succeeded");
	if (w->w != WIN_MIN_W || w->x != 0)
		fail("a refused call changed the window");
	for (i = 1; i < WIN_MAX; i++)
		if (win_create(&scene, i, i, 20, 20, "t", NULL, NULL) == NULL)
			fail("window %d refused", i);
	if (win_create(&scene, 0, 0, 20, 20, "t", NULL, NULL) != NULL)
		fail("a ninth window was created");
	if (win_destroy(&scene, w) != 0 || win_destroy(&scene, w) != -1)
		fail("destroy twice");
	if (win_set_palette(&scene, 16, 0, 0, 0) != -1 ||
	    win_set_palette(&scene, 3, 256, 0, 0) != -1 ||
	    win_set_palette(&scene, 3, 1, 2, 3) != 0 || palette_seen[3][0] != 1 ||
	    palette_seen[3][1] != 2 || palette_seen[3][2] != 3)
		fail("palette calls");
	printf("PASS api: invalid calls refused, palette forwarded\n");
}

/* Backend bytes for three workloads, as the model counts them. */
static void
run_measure(void)
{
	struct twin *a = &tw[0], *b = &tw[1], *c = &tw[2];
	long w0, b0;

	reset_world();
	mask_all = 1;
	op_index = -3;
	/* One 6x9 character inside a settled window. */
	a->used = 1; a->x = 50; a->y = 50; a->w = 100; a->h = 60;
	strcpy(a->title, "one");
	a->nops = 1; a->ops[0].kind = 3; a->ops[0].x = 10; a->ops[0].y = 10;
	a->ops[0].color = 1; strcpy(a->ops[0].text, "W");
	a->handle = win_create(&scene, a->x, a->y, a->w, a->h, a->title, paint_cb, a);
	win_flush(&scene);
	w0 = n_ioctl_window; b0 = n_bytes;
	win_invalidate_client(&scene, a->handle, 10, 10, 6, 9);
	win_flush(&scene);
	printf("MEASURE one 6x9 character: %ld bytes, %ld windows\n",
	    n_bytes - b0, n_ioctl_window - w0);

	/* A 100x60 window moved 8 pixels right over two others. */
	reset_world();
	mask_all = 1;
	a->used = b->used = c->used = 1;
	a->x = 60; a->y = 60; a->w = 120; a->h = 90;
	b->x = 100; b->y = 40; b->w = 100; b->h = 100;
	c->x = 80; c->y = 70; c->w = 100; c->h = 60;
	strcpy(a->title, "a"); strcpy(b->title, "b"); strcpy(c->title, "c");
	a->handle = win_create(&scene, a->x, a->y, a->w, a->h, a->title, paint_cb, a);
	b->handle = win_create(&scene, b->x, b->y, b->w, b->h, b->title, paint_cb, b);
	c->handle = win_create(&scene, c->x, c->y, c->w, c->h, c->title, paint_cb, c);
	win_flush(&scene);
	w0 = n_ioctl_window; b0 = n_bytes;
	win_move(&scene, c->handle, c->x + 8, c->y);
	win_flush(&scene);
	printf("MEASURE 100x60 window moved 8 pixels over two windows: %ld bytes, %ld windows\n",
	    n_bytes - b0, n_ioctl_window - w0);

	/* Full repaint. */
	w0 = n_ioctl_window; b0 = n_bytes;
	win_invalidate(&scene, 0, 0, PW, PH);
	win_flush(&scene);
	printf("MEASURE full repaint: %ld bytes, %ld windows\n",
	    n_bytes - b0, n_ioctl_window - w0);
	mask_all = 0;
}

int
main(int argc, char **argv)
{
	long nops = 10000;

	if (argc > 1)
		nops = atol(argv[1]);
	if (nops < 1) {
		fprintf(stderr, "usage: win_test [OPERATIONS]\n");
		return 2;
	}
	reset_world();
	run_random(nops);
	printf("PASS random: %ld operations, %ld windows and %ld bytes sent, "
	    "%ld writes\n", nops, n_ioctl_window, n_bytes, n_writes);
	run_burst();
	run_api();
	run_measure();
	printf("sizeof(struct win_scene) = %zu\n", sizeof(struct win_scene));
	printf("PASS libwin\n");
	return 0;
}
