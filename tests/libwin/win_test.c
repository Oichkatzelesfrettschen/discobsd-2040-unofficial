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
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/lcdcons.h>

#include <sys/lcdgfx.h>

#include "win.h"
#include "win_input.h"
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
#define R_TITLE_IDLE	8
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
static long n_injected;			/* Backend calls the model failed. */
static int inject_every;		/* Fail one call in this many; 0 is never. */
static int fail_countdown;		/* Fail the call this many calls ahead; 0 is off. */
static uint32_t model_rng = 0x51ed270bu;

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

/* A backend call that fails abandons the window it had open, as the device does. */
static int
inject(void)
{
	int hit = 0;

	if (fail_countdown > 0 && --fail_countdown == 0)
		hit = 1;
	if (inject_every > 0) {
		model_rng ^= model_rng << 13;
		model_rng ^= model_rng >> 17;
		model_rng ^= model_rng << 5;
		if (model_rng % (uint32_t)inject_every == 0)
			hit = 1;
	}
	if (hit) {
		n_injected++;
		win_open = 0;
		errno = EIO;
	}
	return hit;
}

static int
model_ioctl(int fd, unsigned long req, void *arg)
{
	const struct lcdg_window *w = arg;
	const struct lcdg_palette *p = arg;

	if (fd != MODEL_FD)
		fail("ioctl on descriptor %d", fd);
	if (inject())
		return -1;
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
	if (inject())
		return -1;
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

/*
 * Model of the console's raw key session and its tty input queue, behind
 * the system calls win_input_dev.c makes. It follows the kernel as read:
 * LCDIOC_GETKEY returns the events whose time has come, six at a time, with
 * the count dropped before them; the tty queue receives bytes only for the
 * events that type them (a press, not a hold, a release or a modifier), and
 * select(2) is ready only while that queue is not empty. The clock advances
 * only inside select, by its timeout or to the next byte.
 */
#define KBD_FD		9
#define KBD_SCRIPT	96

struct kbev {
	long t;
	int state, key, drop, nb;
	unsigned char bytes[4];
};

static struct kbev kbs[KBD_SCRIPT];
static long kb_delivered[KBD_SCRIPT];
static int nkbs, kb_next, kb_arrived, kb_session, kb_refuse, kb_error;
static long kb_clock, kb_nselect, kb_nioctl;
static unsigned char kb_q[KBD_SCRIPT * 4];
static int kb_qn, kb_qpos;

static void
kb_reset(void)
{
	memset(kbs, 0, sizeof(kbs));
	memset(kb_delivered, 0, sizeof(kb_delivered));
	nkbs = kb_next = kb_arrived = kb_session = kb_refuse = kb_error = 0;
	kb_clock = kb_nselect = kb_nioctl = 0;
	kb_qn = kb_qpos = 0;
}

/* A scripted event; a byte string types its bytes into the tty queue. */
static void
kb_add(long t, int state, int key, int drop, const char *bytes)
{
	struct kbev *e = &kbs[nkbs++];

	e->t = t;
	e->state = state;
	e->key = key;
	e->drop = drop;
	e->nb = bytes == NULL ? 0 : (int)strlen(bytes);
	if (e->nb > 4)
		fail("script bytes");
	if (e->nb > 0)
		memcpy(e->bytes, bytes, (size_t)e->nb);
}

static void
kb_arrive(void)
{
	while (kb_arrived < nkbs && kbs[kb_arrived].t <= kb_clock) {
		memcpy(kb_q + kb_qn, kbs[kb_arrived].bytes, (size_t)kbs[kb_arrived].nb);
		kb_qn += kbs[kb_arrived].nb;
		kb_arrived++;
	}
}

static int
kb_select(int nfd, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
	long timeout = -1, next = -1;
	int j;

	(void)w;
	(void)e;
	if (nfd != KBD_FD + 1 || r == NULL || !FD_ISSET(KBD_FD, r))
		fail("select on the wrong descriptor");
	if (++kb_nselect > 400)
		fail("select was called %ld times: the loop spins", kb_nselect);
	if (tv != NULL && tv->tv_sec < 3600)
		timeout = (long)tv->tv_sec * 1000 + (long)tv->tv_usec / 1000;
	kb_arrive();
	if (kb_qn > kb_qpos) {
		FD_ZERO(r);
		FD_SET(KBD_FD, r);
		return 1;
	}
	for (j = kb_arrived; j < nkbs; j++)
		if (kbs[j].nb > 0) {
			next = kbs[j].t;
			break;
		}
	FD_ZERO(r);
	if (next < 0 || (timeout >= 0 && kb_clock + timeout < next)) {
		if (timeout < 0)
			fail("select would block forever");
		kb_clock += timeout;
		return 0;
	}
	if (next > kb_clock)
		kb_clock = next;
	kb_arrive();
	FD_SET(KBD_FD, r);
	return 1;
}

static ssize_t
kb_read(int fd, void *buf, size_t n)
{
	size_t k = (size_t)(kb_qn - kb_qpos);

	if (fd != KBD_FD)
		fail("read on descriptor %d", fd);
	if (k > n)
		k = n;
	memcpy(buf, kb_q + kb_qpos, k);
	kb_qpos += (int)k;
	if (kb_qpos == kb_qn)
		kb_qpos = kb_qn = 0;
	return (ssize_t)k;
}

static int
kb_ioctl(int fd, unsigned long req, void *arg)
{
	struct lcd_keyevs *o = arg;
	long lost = 0;

	if (fd != KBD_FD)
		fail("ioctl on descriptor %d", fd);
	kb_nioctl++;
	if (req == LCDIOC_KEYEVS) {
		if (*(int *)arg != 0 && kb_refuse) {
			errno = EINVAL;
			return -1;
		}
		kb_session = *(int *)arg;
		return 0;
	}
	if (req != LCDIOC_GETKEY)
		fail("unexpected ioctl %lx", req);
	if (!kb_session || kb_error) {
		errno = EINVAL;
		return -1;
	}
	o->n = 0;
	while (kb_next < nkbs && kbs[kb_next].t <= kb_clock && o->n < LCD_KEYEV_N) {
		if (kbs[kb_next].drop) {
			lost++;
		} else {
			o->ev[o->n].state = (unsigned char)kbs[kb_next].state;
			o->ev[o->n].key = (unsigned char)kbs[kb_next].key;
			o->n++;
			kb_delivered[kb_next] = kb_clock;
		}
		kb_next++;
	}
	o->lost = (unsigned char)lost;
	return 0;
}

#define ioctl kb_ioctl
#define read kb_read
#define select kb_select
#ifndef WIN_DEV_SOURCE
#define WIN_DEV_SOURCE "win_input_dev.c"
#endif
#include WIN_DEV_SOURCE
#undef ioctl
#undef read
#undef select

/* Test scene */

struct cop {
	int kind, x, y, n, m, color;	/* 0 fill, 1 hline, 2 vline, 3 text */
	char text[10];
};

struct twin {
	int used, x, y, w, h, nops, hidden;
	char title[40];
	struct cop ops[NOPS];
	struct win *handle;
};

static struct {
	struct win_scene s;
	unsigned char pad[PAD_BYTES];
} holder;
#define scene (holder.s)

static struct win_scene other;		/* A second scene, for foreign handles. */
static struct win_lcdgfx lcd;
static struct win_backend backend;
static struct twin tw[WIN_MAX];
static int order[WIN_MAX], norder;	/* Test z-order, bottom first. */
static int tfocus = -1;			/* Test id of the focused window, or -1. */
static int tpolicy = 1;			/* Test copy of focus_follows_raise. */
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
ref_window_pixel(const struct twin *t, int focused, int rx, int ry)
{
	const struct cop *o;
	int color, cx, cy, i, inside;

	if (rx >= 1 && rx < t->w - 1 && ry >= 1 && ry <= R_TITLE_ROWS) {
		if (glyph_on(t->title, R_TITLE_MAX, rx - 3, ry - 2))
			return R_TITLE_FG;
		return focused ? R_TITLE_BG : R_TITLE_IDLE;
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
		if (t->hidden)
			continue;
		for (y = t->y < 0 ? 0 : t->y; y < t->y + t->h && y < PH; y++)
			for (x = t->x < 0 ? 0 : t->x; x < t->x + t->w && x < PW; x++)
				ref[y * PW + x] = (unsigned char)
				    ref_window_pixel(t, order[k] == tfocus, x - t->x,
				    y - t->y);
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

/*
 * Focus invariants: the library's focus is the test's, it names a live
 * shown window or none, and the title bar of every shown window that
 * nothing covers at its top left corner is drawn in the color its focus
 * state selects (checked on the panel after a flush).
 */
static void
check_focus(void)
{
	int want = tfocus < 0 ? -1 : (int)(tw[tfocus].handle - scene.wins);

	if (scene.focus != want)
		fail("library focus slot %d, test %d", scene.focus, want);
	if (scene.focus >= 0 &&
	    (!scene.wins[scene.focus].used || scene.wins[scene.focus].hidden))
		fail("the focused window is not a shown live window");
	if (win_focused(&scene) != (want < 0 ? NULL : tw[tfocus].handle))
		fail("win_focused disagrees with the test");
}

static void
check_title_pixels(void)
{
	int k, j, px, py, covered, id;
	const struct twin *t, *u;

	for (k = 0; k < norder; k++) {
		id = order[k];
		t = &tw[id];
		px = t->x + 1;
		py = t->y + 1;
		if (t->hidden || px < 0 || px >= PW || py < 0 || py >= PH)
			continue;
		covered = 0;
		for (j = k + 1; j < norder; j++) {
			u = &tw[order[j]];
			if (!u->hidden && px >= u->x && px < u->x + u->w &&
			    py >= u->y && py < u->y + u->h)
				covered = 1;
		}
		if (covered)
			continue;
		if (panel[py * PW + px] !=
		    (scene.focus == (int)(t->handle - scene.wins) ?
		    R_TITLE_BG : R_TITLE_IDLE))
			fail("title bar of window %d is not drawn as %s", id,
			    scene.focus == (int)(t->handle - scene.wins) ?
			    "focused" : "unfocused");
	}
}

static void
check_scene_mirror(void)
{
	int k;

	if (scene.nwin != norder)
		fail("library holds %d windows, test %d", scene.nwin, norder);
	for (k = 0; k < norder; k++) {
		const struct twin *w = &tw[order[k]];
		const struct win *l = &scene.wins[scene.z[k]];

		if (l != w->handle)
			fail("z position %d holds another window", k);
		if (l->x != w->x || l->y != w->y || l->w != w->w || l->h != w->h ||
		    strncmp(l->title, w->title, R_TITLE_MAX) != 0)
			fail("window %d differs from what the calls reported", order[k]);
		if (l->hidden != w->hidden)
			fail("window %d is %s, the test has it %s", order[k],
			    l->hidden ? "hidden" : "shown", w->hidden ? "hidden" : "shown");
	}
	check_focus();
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
		check_title_pixels();
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
	tfocus = -1;
	tpolicy = 1;
	inject_every = 0;
	fail_countdown = 0;
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

static int
order_remove(int id)
{
	int k, p;

	for (k = 0; order[k] != id; k++)
		;
	p = k;
	for (; k + 1 < norder; k++)
		order[k] = order[k + 1];
	norder--;
	return p;
}

static void
mark_title(int id)
{
	const struct twin *t;

	if (id < 0)
		return;
	t = &tw[id];
	mark(t->x + 1, t->y + 1, t->x + t->w - 1, t->y + 1 + R_TITLE_ROWS);
}

/* The window that takes the focus after the one at position p left it. */
static int
model_pick(int p)
{
	int k;

	for (k = p - 1; k >= 0; k--)
		if (!tw[order[k]].hidden)
			return order[k];
	for (k = norder - 1; k >= p; k--)
		if (!tw[order[k]].hidden)
			return order[k];
	return -1;
}

static void
mark_shown(const struct twin *t)
{
	if (!t->hidden)
		mark_window(t);
}

/* A call fails exactly when the model failed a backend call during it. */
static int
call_ok(int rc, long injected_before, const char *what)
{
	if (rc == 0) {
		if (n_injected != injected_before)
			fail("%s succeeded although the backend failed", what);
		return 1;
	}
	if (n_injected == injected_before)
		fail("%s failed with no backend failure", what);
	return 0;
}

static void
op_create(void)
{
	struct twin *t;
	long inj = n_injected;
	int id;

	for (id = 0; id < WIN_MAX && tw[id].used; id++)
		;
	if (id == WIN_MAX) {
		if (win_create(&scene, 10, 10, 50, 50, "full", NULL, NULL) != NULL)
			fail("a ninth window was created");
		return;
	}
	t = &tw[id];
	memset(t, 0, sizeof(*t));
	t->x = rr(-40, PW + 10);
	t->y = rr(-40, PH + 10);
	t->w = rr(8, 150);
	t->h = rr(14, 130);
	random_title(t->title);
	random_ops(t);
	note("create %d at %d,%d %dx%d", id, t->x, t->y, t->w, t->h);
	t->handle = win_create(&scene, t->x, t->y, t->w, t->h, t->title,
	    paint_cb, t);
	if (!call_ok(t->handle != NULL ? 0 : -1, inj, "win_create")) {
		memset(t, 0, sizeof(*t));
		return;
	}
	t->used = 1;
	mark_window(t);
	order[norder++] = id;
	mark_title(tfocus);
	tfocus = id;
}

static void
op_move(void)
{
	struct twin *t;
	long inj = n_injected;
	int id = pick_window(), nx, ny;

	if (id < 0)
		return;
	t = &tw[id];
	note("move %d from %d,%d", id, t->x, t->y);
	if (rr(0, 1)) {
		nx = t->x + rr(-12, 12);
		ny = t->y + rr(-12, 12);
	} else {
		nx = rr(-40, PW + 10);
		ny = rr(-40, PH + 10);
	}
	if (!call_ok(win_move(&scene, t->handle, nx, ny), inj, "win_move"))
		return;
	mark_shown(t);
	t->x = nx;
	t->y = ny;
	mark_shown(t);
}

static void
op_resize(void)
{
	struct twin *t;
	long inj = n_injected;
	int id = pick_window(), nw, nh;

	if (id < 0)
		return;
	t = &tw[id];
	note("resize %d from %dx%d", id, t->w, t->h);
	nw = rr(8, 150);
	nh = rr(14, 130);
	if (!call_ok(win_resize(&scene, t->handle, nw, nh), inj, "win_resize"))
		return;
	mark_shown(t);
	t->w = nw;
	t->h = nh;
	mark_shown(t);
}

static void
do_raise(int id)
{
	struct twin *t = &tw[id];
	long inj = n_injected;
	int follow = tpolicy && !t->hidden && tfocus != id;

	note("raise %d", id);
	if (!call_ok(win_raise(&scene, t->handle), inj, "win_raise"))
		return;
	if (order[norder - 1] != id) {
		mark_shown(t);
		order_remove(id);
		order[norder++] = id;
	}
	if (follow) {
		mark_title(tfocus);
		tfocus = id;
		mark_title(id);
	}
}

static void
do_lower(int id)
{
	struct twin *t = &tw[id];
	long inj = n_injected;
	int k;

	note("lower %d", id);
	if (!call_ok(win_lower(&scene, t->handle), inj, "win_lower"))
		return;
	if (order[0] == id)
		return;
	mark_shown(t);
	order_remove(id);
	for (k = norder; k > 0; k--)
		order[k] = order[k - 1];
	order[0] = id;
	norder++;
	if (tpolicy && tfocus == id) {
		tfocus = model_pick(norder);
		mark_title(tfocus);
	}
}

static void
do_destroy(int id)
{
	struct twin *t = &tw[id];
	long inj = n_injected;
	int p;

	note("destroy %d", id);
	if (!call_ok(win_destroy(&scene, t->handle), inj, "win_destroy"))
		return;
	mark_shown(t);
	p = order_remove(id);
	if (tfocus == id) {
		tfocus = model_pick(p);
		mark_title(tfocus);
	}
	memset(t, 0, sizeof(*t));
}

static void
do_hide(int id)
{
	struct twin *t = &tw[id];
	long inj = n_injected;
	int k;

	note("hide %d", id);
	if (!call_ok(win_hide(&scene, t->handle), inj, "win_hide"))
		return;
	if (t->hidden)
		return;
	mark_window(t);
	t->hidden = 1;
	if (tfocus == id) {
		for (k = 0; order[k] != id; k++)
			;
		tfocus = model_pick(k);
		mark_title(tfocus);
	}
}

static void
do_show(int id)
{
	struct twin *t = &tw[id];
	long inj = n_injected;

	note("show %d", id);
	if (!call_ok(win_show(&scene, t->handle), inj, "win_show"))
		return;
	if (!t->hidden)
		return;
	t->hidden = 0;
	mark_window(t);
	if (tfocus < 0) {
		tfocus = id;
		mark_title(id);
	}
}

/* id -1 leaves no window focused. */
static void
do_focus(int id)
{
	long inj = n_injected;
	struct win *h = id < 0 ? NULL : tw[id].handle;
	int rc;

	note("focus %d", id);
	if (id >= 0 && tw[id].hidden) {
		if (win_focus(&scene, h) != -1)
			fail("a hidden window took the focus");
		return;
	}
	rc = win_focus(&scene, h);
	if (!call_ok(rc, inj, "win_focus"))
		return;
	if (tfocus != id) {
		mark_title(tfocus);
		tfocus = id;
		mark_title(id);
	}
}

static void
op_raise(void)
{
	int id = pick_window();

	if (id >= 0)
		do_raise(id);
}

static void
op_destroy(void)
{
	int id = pick_window();

	if (id >= 0)
		do_destroy(id);
}

static void
op_visibility(void)
{
	int id = pick_window();

	if (id < 0)
		return;
	if (tw[id].hidden)
		do_show(id);
	else
		do_hide(id);
}

static void
op_focus(void)
{
	int id = pick_window();

	if (rr(0, 9) == 0)
		id = -1;
	if (id >= 0 || tfocus >= 0)
		do_focus(id);
}

static void
op_lower(void)
{
	int id = pick_window();

	if (id >= 0)
		do_lower(id);
}

static void
op_invalidate(void)
{
	long inj = n_injected;
	int x = rr(-20, PW + 10), y = rr(-20, PH + 10);
	int w = rr(1, 60), h = rr(1, 40);

	if (rr(0, 20) == 0) {
		x = y = -5;
		w = h = PW + 20;
	}
	note("invalidate %d,%d %dx%d", x, y, w, h);
	if (call_ok(win_invalidate(&scene, x, y, w, h), inj, "win_invalidate"))
		mark(x, y, x + w, y + h);
}

static void
op_title(void)
{
	struct twin *t;
	long inj = n_injected;
	char title[40];
	int id = pick_window();

	if (id < 0)
		return;
	t = &tw[id];
	note("title %d", id);
	random_title(title);
	if (!call_ok(win_set_title(&scene, t->handle, title), inj, "win_set_title"))
		return;
	strcpy(t->title, title);
	if (!t->hidden)
		mark_title(id);
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
		if (r < 12)
			op_create();
		else if (r < 28)
			op_move();
		else if (r < 36)
			op_resize();
		else if (r < 46)
			op_raise();
		else if (r < 52)
			op_destroy();
		else if (r < 64)
			op_invalidate();
		else if (r < 68)
			op_title();
		else if (r < 74)
			op_visibility();
		else if (r < 78)
			op_focus();
		else if (r < 82)
			op_lower();
		else {
			long inj = n_injected;

			note("flush");
			flushed = call_ok(win_flush(&scene), inj, "win_flush");
		}
		after_op(flushed);
	}
	/* A working backend brings the panel equal to the reference. */
	op_index = nops;
	inject_every = 0;
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
	struct win *w, *foreign;
	int i;

	reset_world();
	op_index = -2;
	if (win_scene_init(&other, &backend) != 0)
		fail("win_scene_init");
	if (win_create(&scene, 0, 0, WIN_MIN_W - 1, 40, "t", NULL, NULL) != NULL ||
	    win_create(&scene, 0, 0, 40, WIN_MIN_H - 1, "t", NULL, NULL) != NULL ||
	    win_create(&scene, 0, 0, WIN_LIMIT + 1, 40, "t", NULL, NULL) != NULL ||
	    win_create(&scene, WIN_LIMIT + 1, 0, 40, 40, "t", NULL, NULL) != NULL)
		fail("win_create accepted an invalid window");
	w = win_create(&scene, 0, 0, WIN_MIN_W, WIN_MIN_H, "t", NULL, NULL);
	if (w == NULL)
		fail("smallest window refused");
	/* A window of another scene is no handle of this one. */
	foreign = win_create(&other, 5, 5, 40, 30, "theirs", NULL, NULL);
	if (foreign == NULL)
		fail("window of the second scene refused");
	if (win_set_title(&scene, foreign, "mine") != -1 ||
	    strcmp(foreign->title, "theirs") != 0)
		fail("a window of another scene was accepted");
	if (win_move(&scene, foreign, 9, 9) != -1 ||
	    win_resize(&scene, foreign, 20, 20) != -1 ||
	    win_raise(&scene, foreign) != -1 || win_destroy(&scene, foreign) != -1 ||
	    win_invalidate_client(&scene, foreign, 0, 0, 4, 4) != -1 ||
	    foreign->x != 5 || foreign->w != 40 || other.nwin != 1)
		fail("a call accepted a window of another scene");
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

/*
 * A backend that fails leaves the scene as the call reported and the damage
 * exact: the refused rectangles stay, nothing is widened, and a retry on a
 * working backend repairs the panel.
 */
static void
run_failure(void)
{
	struct twin *a = &tw[0];
	int i, before;

	reset_world();
	op_index = -4;
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	for (i = 0; i < 7; i++) {
		mark(i * 40, 0, i * 40 + 4, 4);
		if (win_invalidate(&scene, i * 40, 0, 4, 4) != 0)
			fail("win_invalidate");
	}
	before = scene.ndamage;
	if (before != 7)
		fail("seven separate rectangles left %d in the list", before);

	/* The next call must flush first; its first backend call fails. */
	fail_countdown = 1;
	if (win_create(&scene, 10, 10, 60, 40, "late", paint_cb, a) != NULL)
		fail("win_create succeeded although the backend failed");
	if (scene.nwin != 0 || scene.ndamage != before)
		fail("a failed create changed the scene or the damage list");
	fail_countdown = 1;
	if (win_invalidate(&scene, 200, 200, 8, 8) != -1 ||
	    scene.ndamage != before)
		fail("a failed invalidate was reported or recorded wrongly");
	/* A failure in the middle of a rectangle's pixels. */
	fail_countdown = 2;
	if (win_flush(&scene) != -1 || scene.ndamage != before)
		fail("a failed flush dropped or widened damage");
	check_memory();

	/* Retry: create now works, and the flush repairs everything. */
	memset(a, 0, sizeof(*a));
	a->x = 10;
	a->y = 10;
	a->w = 60;
	a->h = 40;
	strcpy(a->title, "late");
	a->handle = win_create(&scene, a->x, a->y, a->w, a->h, a->title, paint_cb, a);
	if (a->handle == NULL)
		fail("win_create refused after the backend recovered");
	a->used = 1;
	order[norder++] = 0;
	tfocus = 0;
	mark_window(a);
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	printf("PASS failure: refused calls change nothing, damage stays exact, "
	    "%ld backend failures\n", n_injected);
}

static void
rc_expect(int rc)
{
	if (rc != -1)
		fail("a call succeeded although its first flush failed");
}

static void
fx_create(int id, int x, int y, int w, int h, const char *title)
{
	struct twin *t = &tw[id];
	long inj = n_injected;

	memset(t, 0, sizeof(*t));
	t->x = x;
	t->y = y;
	t->w = w;
	t->h = h;
	strcpy(t->title, title);
	t->handle = win_create(&scene, x, y, w, h, title, paint_cb, t);
	if (!call_ok(t->handle != NULL ? 0 : -1, inj, "win_create"))
		fail("fixed window %d refused", id);
	t->used = 1;
	mark_window(t);
	order[norder++] = id;
	mark_title(tfocus);
	tfocus = id;
}

static void
expect_focus(int id, const char *what)
{
	if (tfocus != id)
		fail("%s: the test model expects focus %d, got %d", what, id, tfocus);
	if (win_focused(&scene) != (id < 0 ? NULL : tw[id].handle))
		fail("%s: the library did not focus window %d", what, id);
}

static void
fill_damage(void)
{
	int i;

	for (i = 0; i < 7; i++) {
		mark(i * 40, 300, i * 40 + 4, 304);
		if (win_invalidate(&scene, i * 40, 300, 4, 4) != 0)
			fail("win_invalidate");
	}
}

/*
 * Focus and visibility by hand: the focus follows raise and lower, moves to
 * the next window down when the focused one is destroyed or hidden, never
 * rests on a hidden window, a hidden window occludes nothing and records no
 * damage, and a call that fails at the backend changes neither the focus
 * nor the shown state.
 */
static void
run_focus(void)
{
	int i;

	reset_world();
	op_index = -5;
	fx_create(0, 10, 10, 100, 80, "a");
	fx_create(1, 60, 40, 100, 80, "b");
	fx_create(2, 40, 70, 100, 80, "c");
	expect_focus(2, "a new window takes the focus");
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	do_raise(0);
	expect_focus(0, "raise focuses");
	if (order[2] != 0)
		fail("raise did not put the window on top");
	scene.focus_follows_raise = tpolicy = 0;
	do_raise(1);
	expect_focus(0, "raise with the policy off keeps the focus");
	if (order[2] != 1)
		fail("raise with the policy off did not restack");
	scene.focus_follows_raise = tpolicy = 1;
	do_focus(2);
	expect_focus(2, "explicit focus");
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	do_destroy(2);
	expect_focus(1, "destroying the bottom focused window focuses the top");
	do_hide(1);
	expect_focus(0, "hiding the focused window focuses the next one down");
	do_hide(0);
	expect_focus(-1, "no shown window leaves no focus");
	do_focus(1);
	expect_focus(-1, "a hidden window takes no focus");
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	if (panel[11 * PW + 11] != R_DESKTOP)
		fail("a hidden window still covers the desktop");
	/* A hidden window records no damage. */
	if (win_move(&scene, tw[0].handle, 30, 30) != 0 ||
	    win_resize(&scene, tw[0].handle, 90, 70) != 0 ||
	    win_set_title(&scene, tw[0].handle, "hidden") != 0 ||
	    scene.ndamage != 0)
		fail("a change to a hidden window recorded damage");
	tw[0].x = tw[0].y = 30;
	tw[0].w = 90;
	tw[0].h = 70;
	strcpy(tw[0].title, "hidden");
	do_show(1);
	expect_focus(1, "showing a window takes the focus when none has it");
	do_show(0);
	expect_focus(1, "showing a window keeps the focus");
	do_lower(1);
	expect_focus(0, "lowering the focused window focuses the top one");
	do_destroy(0);
	do_destroy(1);
	expect_focus(-1, "an empty scene has no focus");
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);

	/* A call refused by the backend changes neither focus nor shown state. */
	for (i = 0; i < 6; i++) {
		reset_world();
		op_index = -6 - i;
		fx_create(0, 10, 10, 100, 80, "a");
		fx_create(1, 60, 40, 100, 80, "b");
		do_hide(0);
		fx_create(2, 40, 70, 100, 80, "c");
		if (win_flush(&scene) != 0)
			fail("win_flush");
		after_op(1);
		fill_damage();
		fail_countdown = 1;
		switch (i) {
		case 0: rc_expect(win_hide(&scene, tw[2].handle)); break;
		case 1: rc_expect(win_show(&scene, tw[0].handle)); break;
		case 2: rc_expect(win_raise(&scene, tw[1].handle)); break;
		case 3: rc_expect(win_lower(&scene, tw[2].handle)); break;
		case 4: rc_expect(win_focus(&scene, tw[1].handle)); break;
		default: rc_expect(win_destroy(&scene, tw[2].handle)); break;
		}
		after_op(0);
		if (win_flush(&scene) != 0)
			fail("win_flush");
		after_op(1);
	}
	printf("PASS focus: stack, hide, show and failed-call rules\n");
}


/*
 * Input layer. The test drives key strokes through both decoders into the
 * scene and keeps its own model of what the manager must do: the binding
 * list below, the mode rules and the focus rules are written here again,
 * not read from lib/libwin. After each stroke the scene's geometry, order,
 * shown state and focus, the application handlers' event logs and the
 * pixels are compared with the model.
 */
struct logent {
	int id, key, mods, kind;
};

static struct logent alog[512], elog[512];	/* Actual and expected. */
static int nalog, nelog;
static int h_has[WIN_MAX], h_eat[WIN_MAX], h_close_eats[WIN_MAX];
static struct win_input in;
static int msrc_raw;				/* 1: raw events, 0: tty bytes. */
static struct { int mode, id, sx, sy, sw, sh; } im;

static int
test_handler(struct win_scene *s, struct win *w, const struct win_event *ev,
    void *arg)
{
	int id = (int)((struct twin *)arg - tw);

	(void)s;
	(void)w;
	if (nalog < 512) {
		alog[nalog].id = id;
		alog[nalog].key = ev->key;
		alog[nalog].mods = ev->mods;
		alog[nalog].kind = ev->kind;
		nalog++;
	}
	return ev->kind == WIN_EV_CLOSE ? h_close_eats[id] : h_eat[id];
}

static void
input_window(int id, int x, int y, int w, int h)
{
	char title[8];

	title[0] = 'w';
	title[1] = (char)('0' + id);
	title[2] = '\0';
	fx_create(id, x, y, w, h, title);
	h_has[id] = rr(0, 4) != 0;
	h_eat[id] = rr(0, 2) != 0;
	h_close_eats[id] = rr(0, 2) == 0;
	if (h_has[id] &&
	    win_set_handler(&scene, tw[id].handle, test_handler, &tw[id]) != 0)
		fail("win_set_handler");
}

static void
input_world(void)
{
	int n = rr(2, 5), i;

	reset_world();
	memset(&in, 0, sizeof(in));
	win_input_init(&in);
	memset(&im, 0, sizeof(im));
	nalog = nelog = 0;
	for (i = 0; i < n; i++)
		input_window(i, rr(-20, 200), rr(-10, 220), rr(30, 150), rr(20, 120));
	if (rr(0, 2) == 0)
		do_hide(rr(0, n - 1));
}

/* The model's binding table. */
static int
model_action(int key, int ctrl)
{
	if (!ctrl) {
		switch (key) {
		case WIN_KEY_F1 + 5: return 1;
		case WIN_KEY_F1 + 6: return 2;
		case WIN_KEY_F1 + 7: return 3;
		case WIN_KEY_F1 + 8: return 4;
		case WIN_KEY_F1 + 9: return 5;
		default: return 0;
		}
	}
	if (key == WIN_KEY_TAB)
		return 1;
	if (key == WIN_KEY_BACKSPACE)
		return 2;
	return key == 'w' ? 5 : 0;
}

static int
mclamp(int v, int lo, int hi)
{
	if (hi < lo)
		hi = lo;
	return v < lo ? lo : (v > hi ? hi : v);
}

/* Model state changes, with the damage they must cause. */
static void
m_raise_top(int id)
{
	mark_shown(&tw[id]);
	order_remove(id);
	order[norder++] = id;
	if (tfocus != id) {
		mark_title(tfocus);
		tfocus = id;
		mark_title(id);
	}
}

static void
m_lower_bottom(int id)
{
	int k;

	mark_shown(&tw[id]);
	order_remove(id);
	for (k = norder; k > 0; k--)
		order[k] = order[k - 1];
	order[0] = id;
	norder++;
	if (tfocus == id) {
		tfocus = model_pick(norder);
		mark_title(tfocus);
	}
}

static void
m_stroke_action(int action)
{
	int k, id, shown = 0, p;

	for (k = 0; k < norder; k++)
		shown += !tw[order[k]].hidden;
	switch (action) {
	case 1:
	case 2:
		if (shown < 2)
			return;
		if (action == 1) {
			for (k = 0; tw[order[k]].hidden; k++)
				;
			id = order[k];
			m_raise_top(id);
		} else {
			for (k = norder - 1; tw[order[k]].hidden; k--)
				;
			id = order[k];
			m_lower_bottom(id);
			/* The focus follows to the top shown window. */
			if (tfocus != model_pick(norder)) {
				mark_title(tfocus);
				tfocus = model_pick(norder);
				mark_title(tfocus);
			}
		}
		if (action == 1 && tfocus != id)
			fail("model: next did not focus");
		return;
	case 3:
	case 4:
		if (tfocus < 0)
			return;
		im.mode = action - 2;
		im.id = tfocus;
		im.sx = tw[tfocus].x;
		im.sy = tw[tfocus].y;
		im.sw = tw[tfocus].w;
		im.sh = tw[tfocus].h;
		return;
	default:
		if (tfocus < 0)
			return;
		id = tfocus;
		if (h_has[id]) {
			elog[nelog].id = id;
			elog[nelog].key = 0;
			elog[nelog].mods = 0;
			elog[nelog].kind = WIN_EV_CLOSE;
			nelog++;
			if (h_close_eats[id])
				return;
		}
		mark_shown(&tw[id]);
		p = order_remove(id);
		tfocus = model_pick(p);
		mark_title(tfocus);
		memset(&tw[id], 0, sizeof(tw[id]));
		return;
	}
}

/* Resize clamp: the size from the minimum to the panel edge. */
static void
nw_h(struct twin *t, int dx, int dy)
{
	t->w = mclamp(t->w + dx, 8, PW - t->x);
	t->h = mclamp(t->h + dy, 14, PH - t->y);
}

static void
m_mode_key(int key, int count)
{
	struct twin *t = &tw[im.id];
	int dx = 0, dy = 0, i;

	if (key == WIN_KEY_ENTER) {
		im.mode = 0;
		return;
	}
	if (key == WIN_KEY_ESC) {
		mark_shown(t);
		t->x = im.sx;
		t->y = im.sy;
		t->w = im.sw;
		t->h = im.sh;
		mark_shown(t);
		im.mode = 0;
		return;
	}
	if (key == WIN_KEY_LEFT)
		dx = -8;
	else if (key == WIN_KEY_RIGHT)
		dx = 8;
	else if (key == WIN_KEY_UP)
		dy = -8;
	else if (key == WIN_KEY_DOWN)
		dy = 8;
	else
		return;
	for (i = 0; i < count; i++) {
		mark_shown(t);
		if (im.mode == 1) {
			t->x = mclamp(t->x + dx, 0, PW - t->w);
			t->y = mclamp(t->y + dy, 0, PH - t->h);
		} else {
			nw_h(t, dx, dy);
		}
		mark_shown(t);
	}
}


/* Independent encoders of the controller's keys. */
static int
raw_code(int key)
{
	if (key >= 0x20 && key <= 0x7e)
		return key;
	if (key >= WIN_KEY_F1 && key < WIN_KEY_F1 + 9)
		return 0x81 + (key - WIN_KEY_F1);
	switch (key) {
	case WIN_KEY_F10: return 0x90;
	case WIN_KEY_BACKSPACE: return 0x08;
	case WIN_KEY_TAB: return 0x09;
	case WIN_KEY_ENTER: return 0x0a;
	case WIN_KEY_ESC: return 0xb1;
	case WIN_KEY_LEFT: return 0xb4;
	case WIN_KEY_UP: return 0xb5;
	case WIN_KEY_DOWN: return 0xb6;
	case WIN_KEY_RIGHT: return 0xb7;
	case WIN_KEY_INSERT: return 0xd1;
	case WIN_KEY_HOME: return 0xd2;
	case WIN_KEY_DELETE: return 0xd4;
	case WIN_KEY_END: return 0xd5;
	case WIN_KEY_PAGE_UP: return 0xd6;
	default: return 0xd7;
	}
}

/* The bytes the console sends for a key; app selects the ESC O cursor form. */
static int
tty_encode(int key, int ctrl, int app, int alt, char *out)
{
	static const char *const fk[10] = { "\033OP", "\033OQ", "\033OR", "\033OS",
	    "\033[15~", "\033[17~", "\033[18~", "\033[19~", "\033[20~",
	    "\033[21~" };
	const char *s = NULL;
	char tmp[8];

	if (key >= 0x20 && key <= 0x7e) {
		out[0] = (char)(ctrl ? key - 'a' + 1 : key);
		return 1;
	}
	if (key >= WIN_KEY_F1 && key <= WIN_KEY_F10)
		s = fk[key - WIN_KEY_F1];
	switch (key) {
	case WIN_KEY_BACKSPACE: out[0] = alt ? 8 : 0x7f; return 1;
	case WIN_KEY_TAB: out[0] = '\t'; return 1;
	case WIN_KEY_ENTER: out[0] = alt ? '\n' : '\r'; return 1;
	case WIN_KEY_ESC: out[0] = 0x1b; return 1;
	case WIN_KEY_LEFT: s = "D"; break;
	case WIN_KEY_UP: s = "A"; break;
	case WIN_KEY_DOWN: s = "B"; break;
	case WIN_KEY_RIGHT: s = "C"; break;
	case WIN_KEY_HOME: s = "H"; break;
	case WIN_KEY_END: s = "F"; break;
	case WIN_KEY_INSERT: s = "\033[2~"; break;
	case WIN_KEY_DELETE: s = "\033[3~"; break;
	case WIN_KEY_PAGE_UP: s = "\033[5~"; break;
	case WIN_KEY_PAGE_DOWN: s = "\033[6~"; break;
	default: break;
	}
	if (s != NULL && s[0] != 033) {
		tmp[0] = 033;
		tmp[1] = app ? 'O' : '[';
		tmp[2] = s[0];
		memcpy(out, tmp, 3);
		return 3;
	}
	if (s == NULL)
		fail("tty_encode: key %d", key);
	strcpy(out, s);
	return (int)strlen(s);
}

static void
rawev(int state, int code)
{
	if (win_input_raw(&in, &scene, state, code) < 0)
		fail("a raw event failed with a working backend");
}

static void
model_stroke(int key, int ctrl, int holds, int early, int caps)
{
	int act, i, count = 1 + (msrc_raw ? holds : 0);
	int kinds, mods, typed;

	/* The key as the controller reports it for the release after Ctrl is up. */
	typed = key;
	if (key >= 'a' && key <= 'z' && ctrl && caps)
		typed = key - 'a' + 'A';
	if (ctrl && key >= 'A' && key <= 'Z')
		key = key - 'A' + 'a';

	if (im.mode != 0) {
		m_mode_key(key, (key == WIN_KEY_ENTER || key == WIN_KEY_ESC) ? 1 : count);
		return;
	}
	act = model_action(key, ctrl);
	if (act != 0) {
		m_stroke_action(act);
		return;
	}
	if (tfocus < 0 || !h_has[tfocus])
		return;
	kinds = msrc_raw ? count + 1 : 1;
	for (i = 0; i < kinds; i++) {
		mods = ctrl && !(i == kinds - 1 && msrc_raw && early);
		elog[nelog].id = tfocus;
		elog[nelog].key = key;
		if (i == kinds - 1 && msrc_raw && ctrl && early)
			elog[nelog].key = typed;
		elog[nelog].mods = mods;
		elog[nelog].kind = i == 0 ? WIN_EV_PRESS :
		    (msrc_raw && i == kinds - 1 ? WIN_EV_RELEASE : WIN_EV_REPEAT);
		nelog++;
	}
}

static void
feed_raw(int key, int ctrl, int holds, int early, int caps)
{
	int code = raw_code(key), h;

	if (ctrl && caps && key >= 'a' && key <= 'z')
		code = key - 'a' + 'A';
	if (rr(0, 9) == 0) {
		rawev(1, 0xa2);			/* A modifier the table ignores. */
		rawev(3, 0xa2);
	}
	if (ctrl)
		rawev(1, 0xa5);
	rawev(1, code);
	for (h = 0; h < holds; h++) {
		if (ctrl && rr(0, 2) == 0)
			rawev(2, 0xa5);
		rawev(2, code);
	}
	if (early && ctrl)
		rawev(3, 0xa5);
	rawev(3, code);
	if (ctrl && !early)
		rawev(3, 0xa5);
}

static void
feed_tty(int key, int ctrl)
{
	char b[8];
	int n = tty_encode(key, ctrl, rr(0, 1), rr(0, 1), b), i;

	for (i = 0; i < n; i++)
		if (win_input_byte(&in, &scene, (unsigned char)b[i]) < 0)
			fail("a byte failed with a working backend");
	if (in.esc != 0 && rr(0, 3) != 0 && win_input_idle(&in, &scene) < 0)
		fail("idle failed with a working backend");
}

static void
compare_logs(void)
{
	int i;

	if (nalog != nelog)
		fail("handlers saw %d events, the model expects %d", nalog, nelog);
	for (i = 0; i < nalog; i++)
		if (alog[i].id != elog[i].id || alog[i].key != elog[i].key ||
		    alog[i].mods != elog[i].mods || alog[i].kind != elog[i].kind)
			fail("event %d: window %d key %d mods %d kind %d, expected "
			    "window %d key %d mods %d kind %d", i, alog[i].id,
			    alog[i].key, alog[i].mods, alog[i].kind, elog[i].id,
			    elog[i].key, elog[i].mods, elog[i].kind);
	nalog = nelog = 0;
}

static int
pick_key(int raw, int *ctrl)
{
	static const int plain[] = { WIN_KEY_BACKSPACE, WIN_KEY_TAB, WIN_KEY_HOME,
	    WIN_KEY_END, WIN_KEY_INSERT, WIN_KEY_DELETE, WIN_KEY_PAGE_UP,
	    WIN_KEY_PAGE_DOWN };
	int r = rr(0, 99), key;

	*ctrl = 0;
	if (r < 25)
		key = WIN_KEY_F1 + (rr(0, 1) ? rr(5, 9) : rr(0, 9));
	else if (r < 50)
		key = WIN_KEY_LEFT + rr(0, 3);
	else if (r < 58)
		key = rr(0, 1) ? WIN_KEY_ENTER : WIN_KEY_ESC;
	else if (r < 68) {
		*ctrl = 1;
		key = rr(0, 1) ? 'w' : 'a' + rr(0, 25);
		if (!raw && (key == 'h' || key == 'i' || key == 'j' || key == 'm'))
			key = 'q';
		return key;
	} else if (r < 73) {
		if (!raw)
			return 'z';
		*ctrl = 1;
		return rr(0, 1) ? WIN_KEY_TAB : WIN_KEY_BACKSPACE;
	} else if (r < 85)
		key = rr(0, 2) == 0 ? '0' + rr(0, 9) : (rr(0, 1) ? 'a' : 'A') + rr(0, 25);
	else
		key = plain[rr(0, 7)];
	if (raw && rr(0, 9) == 0 && key != WIN_KEY_ESC)
		*ctrl = 1;
	return key;
}

static void
run_stream(int raw, long nstrokes)
{
	long i;
	int key, ctrl, holds, early, caps, id, k;

	input_world();
	msrc_raw = raw;
	for (i = 0; i < nstrokes; i++) {
		op_index = i;
		if (in.esc == 0 && (norder == 0 || (rr(0, 50) == 0 && scene.nwin < WIN_MAX))) {
			for (id = 0; tw[id].used; id++)
				;
			input_window(id, rr(-20, 200), rr(-10, 220), rr(30, 150),
			    rr(20, 120));
		}
		if (in.esc == 0 && norder > 0 && rr(0, 70) == 0) {
			k = order[rr(0, norder - 1)];
			if (tw[k].hidden)
				do_show(k);
			else
				do_hide(k);
		}
		key = pick_key(raw, &ctrl);
		holds = raw ? (rr(0, 3) == 0 ? rr(1, 3) : 0) : 0;
		early = rr(0, 3) == 0;
		caps = rr(0, 1);
		if (raw && !ctrl && rr(0, 40) == 0) {
			rawev(1, 0xa5);		/* Its release is lost. */
			win_input_lost(&in);
		}
		note("%s stroke key %d ctrl %d holds %d", raw ? "raw" : "tty", key,
		    ctrl, holds);
		model_stroke(key, ctrl, holds, early, caps);
		if (raw)
			feed_raw(key, ctrl, holds, early, caps);
		else
			feed_tty(key, ctrl);
		if (in.esc != 0)
			continue;
		compare_logs();
		if (in.mode != im.mode)
			fail("manager mode %d, model %d", in.mode, im.mode);
		if (rr(0, 5) == 0) {
			if (win_flush(&scene) != 0)
				fail("win_flush");
			after_op(1);
		} else {
			after_op(0);
		}
	}
	if (win_input_idle(&in, &scene) < 0)
		fail("idle");
	compare_logs();
	if (win_flush(&scene) != 0)
		fail("final win_flush");
	after_op(1);
}

/* Every key the console can send, as semantic codes. */
static int
all_keys(int *keys)
{
	int n = 0, k;

	for (k = 0x20; k <= 0x7e; k++)
		keys[n++] = k;
	for (k = WIN_KEY_BACKSPACE; k <= WIN_KEY_PAGE_DOWN; k++)
		keys[n++] = k;
	for (k = WIN_KEY_F1; k <= WIN_KEY_F10; k++)
		keys[n++] = k;
	return n;
}

static struct win_event canary_ev[4];

static int
dec_byte(int byte)
{
	int n;

	canary_ev[2].key = canary_ev[3].key = -77;
	canary_ev[2].kind = canary_ev[3].kind = 99;
	n = win_decode_byte(&in, byte, canary_ev);
	if (n < 0 || n > 2 || canary_ev[2].key != -77 || canary_ev[3].key != -77 ||
	    canary_ev[2].kind != 99 || canary_ev[3].kind != 99)
		fail("win_decode_byte wrote %d events or past its array", n);
	return n;
}

static void
expect_plain_a(const char *what)
{
	if (in.esc != 0)
		fail("%s: the decoder is not back at its start", what);
	if (dec_byte('a') != 1 || canary_ev[0].key != 'a' ||
	    canary_ev[0].kind != WIN_EV_PRESS)
		fail("%s: the byte after it did not decode as a plain key", what);
}

/* Both decoders: tables, truncations, garbage and fuzz. */
static void
run_decoders(void)
{
	static const char *const garbage[] = { "\033[1;5A", "\033[99~", "\033[1A",
	    "\033OZ", "\033[", "\033[?25h", "\033[2;~", "\033[65541~",
	    "\033[999999999A", "\033[~", "\033[;A", "\033O", "\033[2", "\033[1;" ,
	    "\033[ A", "\033[\177", "\033[\200" };
	int keys[160], nk, i, c, cut, len, n, app, alt, total = 0;
	char seq[8];
	long f;

	op_index = -20;
	win_input_init(&in);
	nk = all_keys(keys);
	for (i = 0; i < nk; i++) {
		for (app = 0; app < 2; app++) {
			len = tty_encode(keys[i], 0, app, 0, seq);
			win_input_init(&in);
			for (c = 0; c < len; c++) {
				n = dec_byte((unsigned char)seq[c]);
				if (n != (c == len - 1 && keys[i] != WIN_KEY_ESC))
					fail("key %d byte %d gave %d events", keys[i], c, n);
			}
			if (keys[i] == WIN_KEY_ESC) {
				if (in.esc == 0 || win_decode_idle(&in, canary_ev) != 1)
					fail("a lone ESC did not wait for the idle call");
			}
			if (canary_ev[0].key != keys[i] || canary_ev[0].mods != 0 ||
			    canary_ev[0].kind != WIN_EV_PRESS)
				fail("key %d decoded as %d", keys[i], canary_ev[0].key);
			if (in.esc != 0)
				fail("key %d left the decoder mid-sequence", keys[i]);
			/* Every strict prefix of a sequence makes no key. */
			for (cut = 1; cut < len; cut++) {
				win_input_init(&in);
				for (c = 0; c < cut; c++)
					if (dec_byte((unsigned char)seq[c]) != 0)
						fail("a prefix of key %d made a key", keys[i]);
				n = win_decode_idle(&in, canary_ev);
				if (n != (cut == 1 ? 1 : 0) ||
				    (cut == 1 && canary_ev[0].key != WIN_KEY_ESC))
					fail("prefix %d of key %d: idle gave %d events",
					    cut, keys[i], n);
				expect_plain_a("a truncated sequence");
				total++;
			}
		}
	}
	for (alt = 0; alt < 2; alt++) {
		for (c = 1; c <= 26; c++) {
			if (c == 8 || c == 9 || c == 10 || c == 13)
				continue;
			win_input_init(&in);
			seq[0] = (char)c;
			if (dec_byte(c) != 1 || canary_ev[0].key != 'a' + c - 1 ||
			    canary_ev[0].mods != WIN_MOD_CTRL)
				fail("Ctrl-letter byte %d", c);
		}
		win_input_init(&in);
		if (dec_byte(0x08) != 1 || canary_ev[0].key != WIN_KEY_BACKSPACE ||
		    dec_byte(0x00) != 0 || dec_byte(0x80) != 0 || dec_byte(0xff) != 0 ||
		    dec_byte(0x1c) != 0)
			fail("controls and high bytes");
	}
	for (i = 0; i < (int)(sizeof(garbage) / sizeof(garbage[0])); i++) {
		win_input_init(&in);
		for (c = 0; garbage[i][c] != '\0'; c++)
			if (dec_byte((unsigned char)garbage[i][c]) != 0)
				fail("garbage sequence %d made a key at byte %d", i, c);
		if (win_decode_idle(&in, canary_ev) != 0)
			fail("idle after garbage sequence %d made a key", i);
		expect_plain_a("garbage");
	}
	/* ESC then an ordinary byte, ESC ESC, and a sequence cut by ESC. */
	win_input_init(&in);
	if (dec_byte(0x1b) != 0 || dec_byte('x') != 2 ||
	    canary_ev[0].key != WIN_KEY_ESC || canary_ev[1].key != 'x')
		fail("ESC then x");
	if (dec_byte(0x1b) != 0 || dec_byte(0x1b) != 1 ||
	    canary_ev[0].key != WIN_KEY_ESC || dec_byte('[') != 0 ||
	    dec_byte('A') != 1 || canary_ev[0].key != WIN_KEY_UP)
		fail("ESC ESC [ A");
	if (dec_byte(0x1b) != 0 || dec_byte('[') != 0 || dec_byte('1') != 0 ||
	    dec_byte(0x1b) != 0 || dec_byte('O') != 0 || dec_byte('P') != 1 ||
	    canary_ev[0].key != WIN_KEY_F1)
		fail("a sequence cut by ESC");
	/* Fuzz: random bytes with random idle calls never leave the bounds. */
	win_input_init(&in);
	for (f = 0; f < 200000; f++) {
		static const unsigned char alpha[] = { 0x1b, '[', 'O', '0', '1', '2',
		    '5', '9', ';', '~', 'A', 'B', 'P', 'Z', 0x7f, 0x80, 0xff, 0, '\r', ' ',
		    '?', 'q' };
		int b = rr(0, 9) == 0 ? rr(0, 255) : alpha[rr(0, (int)sizeof(alpha) - 1)];

		if (rr(0, 12) == 0)
			(void)win_decode_idle(&in, canary_ev);
		else
			(void)dec_byte(b);
		if (in.esc > 3)
			fail("decoder state %d out of range", in.esc);
	}
	(void)win_decode_idle(&in, canary_ev);
	expect_plain_a("the fuzz run");
	/* The raw decoder: states and key bytes, all of them. */
	win_input_init(&in);
	for (f = 0; f < 70000; f++) {
		int state = (int)(f / 256) % 5, code = (int)(f % 256);
		struct win_event e;

		e.key = -5;
		n = win_decode_raw(&in, state, code, &e);
		if (n != 0 && n != 1)
			fail("win_decode_raw returned %d", n);
		if (n == 1 && (e.kind < WIN_EV_PRESS || e.kind > WIN_EV_RELEASE ||
		    (e.mods & ~WIN_MOD_CTRL) != 0 || e.key == 0 ||
		    (e.key >= 0x100 && (e.key < WIN_KEY_BACKSPACE ||
		    e.key > WIN_KEY_F10)) || (e.key < 0x100 && (e.key < 0x20 ||
		    e.key > 0x7e))))
			fail("win_decode_raw state %d code %d made a bad event", state, code);
	}
	printf("PASS decoders: %d truncations, %d garbage sequences, 200,000 fuzzed "
	    "bytes and every raw state and code\n", total,
	    (int)(sizeof(garbage) / sizeof(garbage[0])));
}

/* The window the fixed cases act on. */
static void
fixed_world(int nwin)
{
	int i;

	reset_world();
	win_input_init(&in);
	memset(&im, 0, sizeof(im));
	nalog = nelog = 0;
	for (i = 0; i < nwin; i++) {
		fx_create(i, 20 + 30 * i, 20 + 20 * i, 100, 70, "w");
		h_has[i] = 1;
		h_eat[i] = 1;
		h_close_eats[i] = 0;
		if (win_set_handler(&scene, tw[i].handle, test_handler, &tw[i]) != 0)
			fail("win_set_handler");
	}
}

static void
fixed_check(const char *what)
{
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	compare_logs();
	(void)what;
}

static void
expect_geom(int id, int x, int y, int w, int h, const char *what)
{
	const struct win *l = tw[id].handle;

	if (l->x != x || l->y != y || l->w != w || l->h != h)
		fail("%s: window %d is %d,%d %dx%d, expected %d,%d %dx%d", what, id,
		    l->x, l->y, l->w, l->h, x, y, w, h);
	tw[id].x = x;
	tw[id].y = y;
	tw[id].w = w;
	tw[id].h = h;
	mark_window(&tw[id]);
}

static void
rawkey(int code)
{
	rawev(1, code);
	rawev(3, code);
}

static void
run_input_fixed(void)
{
	int i, rc;

	op_index = -30;
	msrc_raw = 1;
	note("fixed: held close");
	/* A held Ctrl-W closes once; its repeat and release stay with the manager. */
	fixed_world(2);
	h_close_eats[1] = 0;
	rawev(1, 0xa5);
	rawev(1, 'w');
	rawev(2, 'w');
	rawev(2, 'w');
	rawev(3, 'w');
	rawev(3, 0xa5);
	tfocus = 0;
	mark_window(&tw[1]);
	order_remove(1);
	memset(&tw[1], 0, sizeof(tw[1]));
	elog[nelog].id = 1; elog[nelog].key = 0; elog[nelog].mods = 0;
	elog[nelog].kind = WIN_EV_CLOSE; nelog++;
	if (scene.nwin != 1 || nalog != 1)
		fail("a held Ctrl-W closed %d windows after %d requests", 2 - scene.nwin, nalog);
	fixed_check("held close");
	/* A handler that eats the close request keeps the window. */
	h_close_eats[0] = 1;
	rawev(1, 0xa5);
	rawev(1, 'w');
	rawev(3, 'w');
	rawev(3, 0xa5);
	elog[nelog].id = 0; elog[nelog].key = 0; elog[nelog].mods = 0;
	elog[nelog].kind = WIN_EV_CLOSE; nelog++;
	if (scene.nwin != 1)
		fail("a handler that took the close request lost its window");
	fixed_check("eaten close");

	note("fixed: lost");
	/* A modifier whose release the kernel dropped does not stay latched. */
	fixed_world(2);
	rawev(1, 0xa5);
	win_input_lost(&in);
	rawkey('w');
	if (scene.nwin != 2)
		fail("a Ctrl whose release was lost closed a window");
	elog[nelog].id = 1; elog[nelog].key = 'w'; elog[nelog].mods = 0;
	elog[nelog].kind = WIN_EV_PRESS; nelog++;
	elog[nelog] = elog[nelog - 1];
	elog[nelog].kind = WIN_EV_RELEASE; nelog++;
	fixed_check("lost");
	/* Ctrl still held re-asserts itself with its next HOLD. */
	rawev(2, 0xa5);
	rawkey('w');
	elog[nelog].id = 1; elog[nelog].key = 0; elog[nelog].mods = 0;
	elog[nelog].kind = WIN_EV_CLOSE; nelog++;
	if (nalog != 1)
		fail("a held Ctrl did not return after its HOLD");
	tfocus = 0;
	mark_window(&tw[1]);
	order_remove(1);
	memset(&tw[1], 0, sizeof(tw[1]));
	compare_logs();

	note("fixed: move");
	/* Move mode: clamp at every edge, accept keeps, cancel restores exactly. */
	fixed_world(1);
	rawkey(0x88 + 0);			/* F8 (0x81 + 7 = 0x88). */
	for (i = 0; i < 20; i++)
		rawkey(0xb4);			/* Left: clamps at x = 0. */
	expect_geom(0, 0, 20, 100, 70, "move left clamp");
	for (i = 0; i < 60; i++)
		rawkey(0xb7);			/* Right: clamps at 320 - 100. */
	expect_geom(0, 220, 20, 100, 70, "move right clamp");
	for (i = 0; i < 60; i++)
		rawkey(0xb6);
	expect_geom(0, 220, 250, 100, 70, "move down clamp");
	rawkey(0xb1);				/* Esc: back to 20,20. */
	expect_geom(0, 20, 20, 100, 70, "cancel restores");
	rawkey(0x88);
	rawkey(0xb7);
	rawkey(0x0a);				/* Enter accepts. */
	expect_geom(0, 28, 20, 100, 70, "accept keeps");
	rawkey(0xb7);				/* Mode over: the key reaches the window. */
	elog[nelog].id = 0; elog[nelog].key = WIN_KEY_RIGHT; elog[nelog].mods = 0;
	elog[nelog].kind = WIN_EV_PRESS; nelog++;
	elog[nelog] = elog[nelog - 1];
	elog[nelog].kind = WIN_EV_RELEASE; nelog++;
	fixed_check("mode over");

	note("fixed: resize");
	/* Resize mode, including a window already off the panel. */
	fixed_world(1);
	rawkey(0x89);				/* F9. */
	for (i = 0; i < 40; i++)
		rawkey(0xb4);
	for (i = 0; i < 40; i++)
		rawkey(0xb5);
	expect_geom(0, 20, 20, 8, 14, "resize to the minimum");
	for (i = 0; i < 60; i++) {
		rawkey(0xb7);
		rawkey(0xb6);
	}
	expect_geom(0, 20, 20, 300, 300, "resize to the panel edge");
	rawkey(0x0a);
	if (win_move(&scene, tw[0].handle, -30, 300) != 0 ||
	    win_resize(&scene, tw[0].handle, 150, 100) != 0)
		fail("setup");
	tw[0].x = -30;
	tw[0].y = 300;
	tw[0].w = 150;
	tw[0].h = 100;
	mark_window(&tw[0]);
	rawkey(0x88);
	rawkey(0xb4);
	rawkey(0xb5);
	rawkey(0xb1);
	expect_geom(0, -30, 300, 150, 100, "cancel restores an off-panel window");
	rawkey(0x89);
	rawkey(0xb7);
	rawkey(0xb1);
	expect_geom(0, -30, 300, 150, 100, "cancel restores a size");
	fixed_check("resize");

	note("fixed: refused step");
	/* A refused step keeps the mode and the geometry; the key then works. */
	fixed_world(1);
	rawkey(0x88);
	if (win_flush(&scene) != 0)
		fail("win_flush");
	after_op(1);
	fill_damage();
	fail_countdown = 1;
	rc = win_input_raw(&in, &scene, 1, 0xb7);
	if (rc != -1 || tw[0].handle->x != 20 || in.mode != WIN_MODE_MOVE)
		fail("a refused move step changed the window or ended the mode");
	mark_window(&tw[0]);
	rawkey(0xb7);
	expect_geom(0, 28, 20, 100, 70, "retry after a refused step");
	rawkey(0x0a);
	fixed_check("refused step");

	note("fixed: tty");
	/* The tty decoder carries the same bindings. */
	msrc_raw = 0;
	fixed_world(3);
	win_input_byte(&in, &scene, 0x1b);
	win_input_byte(&in, &scene, '[');
	win_input_byte(&in, &scene, '1');
	win_input_byte(&in, &scene, '7');
	win_input_byte(&in, &scene, '~');		/* F6: next window. */
	m_raise_top(0);
	fixed_check("tty F6");
	win_input_byte(&in, &scene, 0x17);		/* Ctrl-W. */
	elog[nelog].id = 0; elog[nelog].key = 0; elog[nelog].mods = 0;
	elog[nelog].kind = WIN_EV_CLOSE; nelog++;
	{
		int p;

		mark_window(&tw[0]);
		p = order_remove(0);
		mark_title(model_pick(p));
		tfocus = model_pick(p);
		memset(&tw[0], 0, sizeof(tw[0]));
	}
	fixed_check("tty Ctrl-W");
	win_input_byte(&in, &scene, 0x1b);
	win_input_idle(&in, &scene);			/* A lone Esc reaches the window. */
	elog[nelog].id = tfocus; elog[nelog].key = WIN_KEY_ESC; elog[nelog].mods = 0;
	elog[nelog].kind = WIN_EV_PRESS; nelog++;
	fixed_check("tty Esc");
	printf("PASS input: held keys, lost events, move and resize modes, tty "
	    "bindings\n");
}


static void
dev_logs(int id, int key, int mods, int kind)
{
	elog[nelog].id = id;
	elog[nelog].key = key;
	elog[nelog].mods = mods;
	elog[nelog].kind = kind;
	nelog++;
}

/*
 * The descriptor side against the kernel model: a raw session wakes on
 * typing presses only and is polled for the rest, drains tty bytes so
 * select cannot spin, drains every batch, applies a lost count, and the tty
 * fallback resolves a lone ESC when its wait runs out.
 */
static void
run_dev(void)
{
	struct win_input_src src;
	int i, r;

	op_index = -40;
	msrc_raw = 1;

	/* Latency: modifier, hold and release events are seen within a poll. */
	kb_reset();
	fixed_world(2);
	kb_add(5, 1, 0xa5, 0, NULL);
	kb_add(8, 1, 'w', 0, "\027");
	kb_add(9, 2, 'w', 0, NULL);
	kb_add(12, 3, 'w', 0, NULL);
	kb_add(13, 3, 0xa5, 0, NULL);
	kb_add(40, 1, 'q', 0, "q");
	kb_add(45, 3, 'q', 0, NULL);
	for (i = 0; i < 8; i++)
		kb_add(70, 1, 'a' + i, 0, (const char[]){ (char)('a' + i), 0 });
	if (win_input_open(&src, KBD_FD) != 0 || !src.raw || !kb_session)
		fail("win_input_open did not start the session");
	r = win_input_wait(&src, &in, &scene, 100);
	if (r < 1 || kb_clock != 8 || scene.nwin != 1)
		fail("the first wait returned %d at %ld ms with %d windows", r,
		    kb_clock, scene.nwin);
	mark_window(&tw[1]);
	order_remove(1);
	memset(&tw[1], 0, sizeof(tw[1]));
	tfocus = 0;
	elog[nelog].id = 1; elog[nelog].key = 0; elog[nelog].mods = 0;
	elog[nelog].kind = WIN_EV_CLOSE; nelog++;
	compare_logs();
	r = win_input_wait(&src, &in, &scene, 100);
	if (r < 1 || kb_clock > 28 || nalog != 0)
		fail("the held keys were not drained by a poll: %d at %ld ms", r, kb_clock);
	r = win_input_wait(&src, &in, &scene, 100);
	if (r < 1 || kb_clock != 40)
		fail("a typing press did not wake select: %d at %ld ms", r, kb_clock);
	dev_logs(0, 'q', 0, WIN_EV_PRESS);
	r = win_input_wait(&src, &in, &scene, 100);
	if (r < 1)
		fail("the release was not polled");
	dev_logs(0, 'q', 0, WIN_EV_RELEASE);
	compare_logs();
	r = win_input_wait(&src, &in, &scene, 100);
	if (nalog != 8)
		fail("a batch of eight presses reached the handler as %d events", nalog);
	for (i = 0; i < 8; i++)
		dev_logs(0, 'a' + i, 0, WIN_EV_PRESS);
	compare_logs();
	for (i = 0; i < nkbs; i++)
		if (!kbs[i].drop && kb_delivered[i] - kbs[i].t > WIN_POLL_MS)
			fail("event %d waited %ld ms", i, kb_delivered[i] - kbs[i].t);
	win_input_close(&src);
	if (kb_session)
		fail("win_input_close left the session open");
	if (kb_nselect > 30)
		fail("%ld select calls for 26 events", kb_nselect);

	/* A lost count clears the modifier table before its batch. */
	kb_reset();
	fixed_world(2);
	kb_add(5, 1, 0xa5, 0, NULL);
	kb_add(25, 3, 0xa5, 1, NULL);
	kb_add(30, 1, 'a', 0, "a");
	if (win_input_open(&src, KBD_FD) != 0)
		fail("open");
	r = win_input_wait(&src, &in, &scene, 100);
	if (kb_clock != 20 || in.mods == 0)
		fail("the first poll did not hold Ctrl (%ld ms)", kb_clock);
	r = win_input_wait(&src, &in, &scene, 100);
	if (r < 1)
		fail("the batch with a lost count was not read");
	dev_logs(1, 'a', 0, WIN_EV_PRESS);
	compare_logs();
	win_input_close(&src);

	/* The tty fallback: sequences, a lone ESC, and the wait's own timeout. */
	kb_reset();
	kb_refuse = 1;
	fixed_world(2);
	kb_add(5, 0, 0, 0, "\033[A");
	kb_add(30, 0, 0, 0, "\033");
	kb_add(80, 0, 0, 0, "x");
	if (win_input_open(&src, KBD_FD) != -1 || src.raw)
		fail("a refused session did not select the tty source");
	r = win_input_wait(&src, &in, &scene, 100);
	if (r < 1 || kb_clock != 5)
		fail("tty arrow: %d at %ld ms", r, kb_clock);
	dev_logs(1, WIN_KEY_UP, 0, WIN_EV_PRESS);
	r = win_input_wait(&src, &in, &scene, 100);
	if (kb_clock != 50)
		fail("a lone ESC was resolved at %ld ms, expected 50", kb_clock);
	dev_logs(1, WIN_KEY_ESC, 0, WIN_EV_PRESS);
	r = win_input_wait(&src, &in, &scene, 100);
	dev_logs(1, 'x', 0, WIN_EV_PRESS);
	compare_logs();

	/* Timeouts are exact in both sources, and a dead session reports. */
	kb_reset();
	fixed_world(1);
	if (win_input_open(&src, KBD_FD) != 0)
		fail("open");
	if (win_input_wait(&src, &in, &scene, 30) != 0 || kb_clock != 30)
		fail("a raw wait of 30 ms ended at %ld", kb_clock);
	kb_error = 1;
	if (win_input_wait(&src, &in, &scene, 30) != -1 || src.raw)
		fail("a dead session was not reported");
	kb_reset();
	kb_refuse = 1;
	(void)win_input_open(&src, KBD_FD);
	if (win_input_wait(&src, &in, &scene, 30) != 0 || kb_clock != 30 ||
	    kb_nselect != 1)
		fail("a tty wait of 30 ms ended at %ld after %ld selects", kb_clock,
		    kb_nselect);
	printf("PASS device: raw session polling, tty drain, lost count, tty "
	    "fallback and timeouts\n");
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
	inject_every = 150;
	run_random(nops);
	if (n_injected < 20)
		fail("only %ld backend failures injected", n_injected);
	printf("PASS random: %ld operations, %ld windows and %ld bytes sent, "
	    "%ld writes, %ld backend failures\n", nops, n_ioctl_window, n_bytes,
	    n_writes, n_injected);
	run_burst();
	run_api();
	run_failure();
	run_focus();
	run_decoders();
	run_stream(1, 2500);
	printf("PASS raw stream: 2500 key strokes through the raw decoder\n");
	run_stream(0, 2500);
	printf("PASS tty stream: 2500 key strokes through the byte decoder\n");
	run_input_fixed();
	run_dev();
	run_measure();
	printf("sizeof(struct win_scene) = %zu\n", sizeof(struct win_scene));
	printf("PASS libwin\n");
	return 0;
}
