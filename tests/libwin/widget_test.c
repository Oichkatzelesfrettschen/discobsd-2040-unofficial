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
 * Host gate for the libwin widget layer (win_widget.c). The library paints
 * a form into the strip compositor and a model backend records the pixels
 * and counts the bytes; a reference painter, written from the geometry in
 * win_widget.h and sharing only the font table with the library, draws the
 * expected panel from the test's own model of the controls. The model
 * applies the keys by its own rules (string model for fields, selection and
 * scroll arithmetic for lists, traversal order, mnemonics, menus), so a
 * disagreement is a finding in one of the two.
 *
 * What runs: golden images of every control in every state; paints into
 * clipped strips with guard bytes; traversal and mnemonic cases; seeded key
 * streams compared with the model and the reference image after every
 * event; byte counts of no-op sets and one-character changes; a
 * failing-backend sweep over every operation at every damage-list fill; and
 * dialogs. Fixed seeds; deterministic.
 *
 * usage: widget_test
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win.h"
#include "win_input.h"
#include "win_widget.h"
#include "lcdfont.h"

#define PW		WIN_PANEL_W
#define PH		WIN_PANEL_H
#define NITEM		11
#define UNWRITTEN	0xff

/* Model backend */

static unsigned char panel[PW * PH];
static long n_bytes, n_windows, n_bits_windows, last_windows;
static int fail_after;			/* Fail the Nth backend call from now on; 0 is off. */
static long n_injected;
static int bits_enabled = 1;
static char trail[4][96];
static long trail_n;
static long op_index;

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
inject(void)
{
	if (fail_after > 0 && --fail_after == 0) {
		n_injected++;
		return 1;
	}
	return 0;
}

static int
be_rect(void *self, int x, int y, int w, int h, const unsigned char *px)
{
	int r, c;

	(void)self;
	if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > PW || y + h > PH)
		fail("rect %d,%d %dx%d leaves the panel", x, y, w, h);
	if (inject())
		return -1;
	for (r = 0; r < h; r++)
		for (c = 0; c < w; c++) {
			if (px[r * w + c] > 15)
				fail("pixel byte %u", px[r * w + c]);
			panel[(y + r) * PW + x + c] = px[r * w + c];
		}
	n_bytes += (long)w * h;
	n_windows++;
	return 0;
}

static int
be_bits(void *self, int x, int y, int w, int h, int fg, int bg,
    const unsigned char *rows)
{
	int r, c, rb = (w + 7) / 8;

	(void)self;
	if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > PW || y + h > PH ||
	    fg < 0 || fg > 15 || bg < 0 || bg > 15)
		fail("bits %d,%d %dx%d colors %d %d", x, y, w, h, fg, bg);
	if (inject())
		return -1;
	for (r = 0; r < h; r++)
		for (c = 0; c < w; c++)
			panel[(y + r) * PW + x + c] = (unsigned char)
			    ((rows[r * rb + c / 8] & (0x80 >> (c % 8))) != 0 ? fg : bg);
	n_bytes += (long)rb * h;
	n_windows++;
	n_bits_windows++;
	return 0;
}

/* Reference painter */

static unsigned char ref[PW * PH];
static int rc0, rc1, rc2, rc3;		/* Clip: x0, y0, x1, y1, upper bounds excluded. */

static void
rf(int x, int y, int w, int h, int color)
{
	int r, c, x0 = x < rc0 ? rc0 : x, y0 = y < rc1 ? rc1 : y;
	int x1 = x + w > rc2 ? rc2 : x + w, y1 = y + h > rc3 ? rc3 : y + h;

	for (r = y0; r < y1; r++)
		for (c = x0; c < x1; c++)
			if (r >= 0 && r < PH && c >= 0 && c < PW)
				ref[r * PW + c] = (unsigned char)color;
}

static void
rglyph(int x, int y, int ch, int color)
{
	int row, col;

	if (ch < 32 || ch > 126)
		ch = '?';
	for (row = 0; row < 9; row++)
		for (col = 0; col < 6; col++)
			if ((lcdfont[ch - 32][row] & (0x80 >> col)) != 0)
				rf(x + col, y + row, 1, 1, color);
}

static int
rcells(const char *t)
{
	int n = 0;

	for (; *t; t++)
		n += *t != '&';
	return n;
}

/* Text with '&': the first marked character gets a 6-pixel line at row 9. */
static void
rmarked(int x, int y, const char *t, int color)
{
	int cell = 0, marked = 0, seen = 0;

	for (; *t; t++) {
		if (*t == '&') {
			if (!seen)
				marked = 1;
			seen = 1;
			continue;
		}
		rglyph(x + 6 * cell, y, *t, color);
		if (marked)
			rf(x + 6 * cell, y + 9, 6, 1, color);
		marked = 0;
		cell++;
	}
}

/* The test's model of a control. */
struct mw {
	int kind, x, y, w, h, id;
	int hidden, disabled;
	char text[48];
	int checked;
	char buf[32];
	int cap, len, cur, scroll, vis;
	const char *const *items;
	int n, sel, top, rows;
	const struct wgt_menu *menus;
	int nm, mcur, mentry, mopen;
};

struct mf {
	int x, y, w, h;
	char title[24];
	int cw, ch;
	struct mw it[NITEM + 1];
	int n;
	int focus;
};

static int
m_titlew(const struct wgt_menu *m)
{
	return 6 * rcells(m->title) + 12;
}

static int
m_titlex(const struct mw *b, int i)
{
	int k, x = 0;

	for (k = 0; k < i; k++)
		x += m_titlew(&b->menus[k]);
	return x;
}

static int
m_popw(const struct wgt_menu *m)
{
	int j, n = 0;

	for (j = 0; j < m->n; j++)
		if (rcells(m->entries[j].text) > n)
			n = rcells(m->entries[j].text);
	return 6 * n + 12;
}

static void
ref_widget(const struct mf *f, int idx)
{
	const struct mw *w = &f->it[idx];
	int ox = f->x + 1 + w->x, oy = f->y + 1 + WIN_TITLE_H + w->y;
	int foc = idx == f->focus, k, i, r, n, hi, th, ty, track, color;
	int cx = f->x + 1, cy = f->y + 1 + WIN_TITLE_H;

	if (w->hidden)
		return;
	rc0 = ox > cx ? ox : cx;
	rc1 = oy > cy ? oy : cy;
	rc2 = ox + w->w < cx + f->cw ? ox + w->w : cx + f->cw;
	rc3 = oy + w->h < cy + f->ch ? oy + w->h : cy + f->ch;
	color = w->disabled ? 8 : 0;
	switch (w->kind) {
	case WGT_LABEL:
		rmarked(ox, oy, w->text, color);
		break;
	case WGT_STATUS:
		rf(ox, oy, w->w, 1, 8);
		rmarked(ox + 2, oy + 2, w->text, color);
		break;
	case WGT_BUTTON:
		rf(ox, oy, w->w, w->h, 0);
		rf(ox + 1, oy + 1, w->w - 2, w->h - 2, foc ? 4 : 15);
		rmarked(ox + (w->w - 6 * rcells(w->text)) / 2, oy + 2, w->text,
		    foc ? 15 : color);
		break;
	case WGT_CHECK:
		rf(ox, oy + 1, 9, 9, 0);
		rf(ox + 1, oy + 2, 7, 7, 15);
		if (w->checked)
			rf(ox + 2, oy + 3, 5, 5, 0);
		if (foc)
			rf(ox + 12, oy, 6 * rcells(w->text) + 4, 12, 4);
		rmarked(ox + 14, oy + 1, w->text, foc ? 15 : color);
		break;
	case WGT_FIELD:
		rf(ox, oy, w->w, w->h, foc ? 4 : 0);
		rf(ox + 1, oy + 1, w->w - 2, w->h - 2, w->disabled ? 7 : 15);
		for (k = 0; k < w->vis; k++) {
			i = w->scroll + k;
			hi = foc && i == w->cur;
			if (hi)
				rf(ox + 2 + 6 * k, oy + 2, 6, 10, 4);
			if (i < w->len)
				rglyph(ox + 2 + 6 * k, oy + 2, w->buf[i],
				    hi ? 15 : color);
		}
		break;
	case WGT_LIST:
		rf(ox, oy, w->w, w->h, 0);
		rf(ox + 1, oy + 1, w->w - 2, w->h - 2, 15);
		track = w->h - 2;
		th = track * w->rows / (w->n ? w->n : 1);
		if (th < 4)
			th = 4;
		if (w->n <= w->rows)
			th = track;
		ty = w->n <= w->rows ? 0 : (track - th) * w->top / (w->n - w->rows);
		rf(ox + w->w - 8, oy + 1, 7, track, 8);
		rf(ox + w->w - 8, oy + 1 + ty, 7, th, 15);
		for (r = 0; r < w->rows && w->top + r < w->n; r++) {
			i = w->top + r;
			rc0 = ox + 1;
			rc1 = oy + 1 + 10 * r;
			rc2 = ox + w->w - 8;
			rc3 = oy + 11 + 10 * r;
			if (i == w->sel)
				rf(ox + 1, oy + 1 + 10 * r, w->w - 9, 10, foc ? 4 : 8);
			for (k = 0, n = (int)strlen(w->items[i]); k < n; k++)
				rglyph(ox + 3 + 6 * k, oy + 1 + 10 * r, w->items[i][k],
				    i == w->sel ? 15 : color);
		}
		break;
	default:
		rf(ox, oy, w->w, 12, 15);
		rf(ox, oy + 11, w->w, 1, 8);
		for (i = 0; i < w->nm; i++) {
			hi = i == w->mcur && (foc || w->mopen);
			if (hi)
				rf(ox + m_titlex(w, i), oy, m_titlew(&w->menus[i]), 11, 4);
			rmarked(ox + m_titlex(w, i) + 6, oy + 1, w->menus[i].title,
			    hi ? 15 : color);
		}
		break;
	}
}

static void
ref_popup(const struct mf *f, int idx)
{
	const struct mw *b = &f->it[idx];
	const struct wgt_menu *m = &b->menus[b->mcur];
	int px = f->x + 1 + b->x + m_titlex(b, b->mcur);
	int py = f->y + 1 + WIN_TITLE_H + b->y + 12;
	int pw = m_popw(m), ph = 2 + 10 * m->n, j;
	int cy = f->y + 1 + WIN_TITLE_H;

	rc0 = px > f->x + 1 ? px : f->x + 1;
	rc1 = py > cy ? py : cy;
	rc2 = px + pw < f->x + 1 + f->cw ? px + pw : f->x + 1 + f->cw;
	rc3 = py + ph < cy + f->ch ? py + ph : cy + f->ch;
	rf(px, py, pw, ph, 0);
	rf(px + 1, py + 1, pw - 2, ph - 2, 15);
	for (j = 0; j < m->n; j++) {
		if (j == b->mentry)
			rf(px + 1, py + 1 + 10 * j, pw - 2, 10, 4);
		rmarked(px + 6, py + 1 + 10 * j, m->entries[j].text,
		    j == b->mentry ? 15 : 0);
	}
}

static const struct mf *zlist[3];
static int nz;

static void
ref_window(const struct mf *f, int focused)
{
	const char *t;
	int i, cell = 0, tx = f->x + 1, ty = f->y + 1, cx = f->x + 1;
	int cy = f->y + 1 + WIN_TITLE_H;

	rc0 = f->x;
	rc1 = f->y;
	rc2 = f->x + f->w;
	rc3 = f->y + f->h;
	rf(f->x, f->y, f->w, f->h, 0);
	rc0 = tx;
	rc1 = ty;
	rc2 = f->x + f->w - 1;
	rc3 = ty + WIN_TITLE_H;
	rf(tx, ty, f->w - 2, WIN_TITLE_H, focused ? 4 : 8);
	for (t = f->title; *t; t++, cell++)
		rglyph(tx + 2 + 6 * cell, ty + 1, *t, 15);
	rc0 = cx;
	rc1 = cy;
	rc2 = f->x + f->w - 1;
	rc3 = f->y + f->h - 1;
	rf(cx, cy, f->w - 2, f->h - WIN_TITLE_H - 2, 7);
	for (i = 0; i < f->n; i++)
		ref_widget(f, i);
	for (i = 0; i < f->n; i++)
		if (f->it[i].kind == WGT_MENUBAR && !f->it[i].hidden && f->it[i].mopen)
			ref_popup(f, i);
}

static void
paint_ref(void)
{
	int i;

	rc0 = 0;
	rc1 = 0;
	rc2 = PW;
	rc3 = PH;
	rf(0, 0, PW, PH, WIN_C_DESKTOP);
	for (i = 0; i < nz; i++)
		ref_window(zlist[i], i == nz - 1);
}

/* Model of the controls and their keys */

static char log_lib[1024], log_mod[1024];
static int veto_id = -1;		/* Activation of this id is vetoed. */

static void
logadd(char *log, int id, int value)
{
	size_t n = strlen(log);

	if (n < sizeof(log_lib) - 24)
		snprintf(log + n, 24, "%d:%d;", id, value);
}

static int
m_mnem(const char *t)
{
	for (; *t; t++)
		if (*t == '&')
			return t[1] >= 'A' && t[1] <= 'Z' ? t[1] + 32 : t[1];
	return 0;
}

static int
m_focusable(const struct mw *w)
{
	return w->kind >= WGT_BUTTON && !w->hidden && !w->disabled;
}

static void
m_setfocus(struct mf *f, int to)
{
	if (to == f->focus)
		return;
	if (f->focus >= 0 && f->it[f->focus].kind == WGT_MENUBAR)
		f->it[f->focus].mopen = 0;
	f->focus = to;
}

/* The next focusable control after from in a direction, wrapping, or -1. */
static int
m_pick(const struct mf *f, int from, int forward)
{
	int cand[NITEM + 1], nc = 0, i, k;

	for (i = 0; i < f->n; i++)
		if (m_focusable(&f->it[i]))
			cand[nc++] = i;
	if (nc == 0)
		return -1;
	if (from < 0)
		return forward ? cand[0] : cand[nc - 1];
	if (forward) {
		for (k = 0; k < nc; k++)
			if (cand[k] > from)
				return cand[k];
		return cand[0];
	}
	for (k = nc - 1; k >= 0; k--)
		if (cand[k] < from)
			return cand[k];
	return cand[nc - 1];
}

static int
m_adjust(const struct mw *w, int scroll)
{
	if (w->cur < scroll)
		return w->cur;
	if (w->cur > scroll + w->vis - 1)
		return w->cur - w->vis + 1;
	return scroll;
}

static int
m_hook(int id, int value)
{
	logadd(log_mod, id, value);
	return id == veto_id ? -1 : 0;
}

static void
m_select(struct mw *w, int to)
{
	w->sel = to;
	if (to < w->top)
		w->top = to;
	else if (to > w->top + w->rows - 1)
		w->top = to - w->rows + 1;
}

static int
m_menu_run(struct mw *b, int j)
{
	if (m_hook(b->menus[b->mcur].entries[j].id, 0) < 0)
		return -1;
	b->mopen = 0;
	b->mentry = 0;
	return 1;
}

static int
m_menu_key(struct mw *b, const struct win_event *ev)
{
	const struct wgt_menu *m = &b->menus[b->mcur];
	int k = ev->key, press = ev->kind == WIN_EV_PRESS, j;

	if (!b->mopen) {
		if (k == WIN_KEY_LEFT) {
			b->mcur = (b->mcur + b->nm - 1) % b->nm;
		} else if (k == WIN_KEY_RIGHT) {
			b->mcur = (b->mcur + 1) % b->nm;
		} else if (k == WIN_KEY_DOWN || (k == WIN_KEY_ENTER && press)) {
			b->mopen = 1;
			b->mentry = 0;
		} else if (k != WIN_KEY_ENTER) {
			return 0;
		}
		return 1;
	}
	if (k == WIN_KEY_ESC) {
		if (press) {
			b->mopen = 0;
			b->mentry = 0;
		}
	} else if (k == WIN_KEY_LEFT || k == WIN_KEY_RIGHT) {
		b->mcur = (b->mcur + (k == WIN_KEY_RIGHT ? 1 : b->nm - 1)) % b->nm;
		b->mentry = 0;
	} else if (k == WIN_KEY_UP) {
		if (b->mentry > 0)
			b->mentry--;
	} else if (k == WIN_KEY_DOWN) {
		if (b->mentry < m->n - 1)
			b->mentry++;
	} else if (k == WIN_KEY_HOME) {
		b->mentry = 0;
	} else if (k == WIN_KEY_END) {
		b->mentry = m->n - 1;
	} else if (k == WIN_KEY_ENTER) {
		if (press)
			return m_menu_run(b, b->mentry);
	} else if (press && k >= 32 && k <= 126) {
		for (j = 0; j < m->n; j++)
			if (m_mnem(m->entries[j].text) == (k >= 'A' && k <= 'Z' ? k + 32 : k))
				return m_menu_run(b, j);
	}
	return 1;
}

static int
m_mnemonic(struct mf *f, int letter)
{
	int i, k, t;
	struct mw *w;

	for (i = 0; i < f->n; i++) {
		w = &f->it[i];
		if (w->hidden || w->disabled)
			continue;
		if (w->kind == WGT_MENUBAR) {
			for (k = 0; k < w->nm; k++)
				if (m_mnem(w->menus[k].title) == letter) {
					m_setfocus(f, i);
					w->mcur = k;
					w->mopen = 1;
					w->mentry = 0;
					return 1;
				}
			continue;
		}
		if ((w->kind != WGT_LABEL && w->kind != WGT_BUTTON &&
		    w->kind != WGT_CHECK) || m_mnem(w->text) != letter)
			continue;
		if (w->kind == WGT_LABEL) {
			t = m_pick(f, i, 1);
			if (t >= 0)
				m_setfocus(f, t);
			return 1;
		}
		if (w->kind == WGT_BUTTON) {
			if (m_hook(w->id, 0) < 0)
				return -1;
			m_setfocus(f, i);
			return 1;
		}
		if (m_hook(w->id, !w->checked) < 0)
			return -1;
		m_setfocus(f, i);
		w->checked = !w->checked;
		return 1;
	}
	return 0;
}

static void
m_field_edit(struct mw *w, int op, int ch)
{
	switch (op) {
	case 0:			/* insert */
		memmove(w->buf + w->cur + 1, w->buf + w->cur, (size_t)(w->len - w->cur) + 1);
		w->buf[w->cur++] = (char)ch;
		w->len++;
		break;
	case 1:			/* backspace */
		memmove(w->buf + w->cur - 1, w->buf + w->cur, (size_t)(w->len - w->cur) + 1);
		w->cur--;
		w->len--;
		break;
	case 2:			/* delete */
		memmove(w->buf + w->cur, w->buf + w->cur + 1, (size_t)(w->len - w->cur));
		w->len--;
		break;
	default:
		w->cur = ch;
		break;
	}
	w->scroll = m_adjust(w, w->scroll);
}

/* The model's handling of one event; 1 consumed, 0 not, -1 vetoed. */
static int
m_key(struct mf *f, const struct win_event *ev)
{
	struct mw *w;
	int k = ev->key, press = ev->kind == WIN_EV_PRESS, to;

	if (ev->kind != WIN_EV_PRESS && ev->kind != WIN_EV_REPEAT)
		return 0;
	if ((ev->mods & WIN_MOD_CTRL) != 0) {
		if (!press || k < 'a' || k > 'z')
			return 0;
		return m_mnemonic(f, k);
	}
	if (k == WIN_KEY_TAB || k == WIN_KEY_F1 + 4) {
		to = m_pick(f, f->focus, k == WIN_KEY_TAB);
		if (to >= 0)
			m_setfocus(f, to);
		return 1;
	}
	if (f->focus < 0)
		return 0;
	w = &f->it[f->focus];
	switch (w->kind) {
	case WGT_BUTTON:
		if (k != WIN_KEY_ENTER && k != ' ')
			return 0;
		if (press && m_hook(w->id, 0) < 0)
			return -1;
		return 1;
	case WGT_CHECK:
		if (k != ' ')
			return 0;
		if (!press)
			return 1;
		if (m_hook(w->id, !w->checked) < 0)
			return -1;
		w->checked = !w->checked;
		return 1;
	case WGT_FIELD:
		if (k == WIN_KEY_ENTER) {
			if (press && m_hook(w->id, 0) < 0)
				return -1;
			return 1;
		}
		if (k >= 32 && k <= 126) {
			if (w->len < w->cap - 1)
				m_field_edit(w, 0, k);
		} else if (k == WIN_KEY_BACKSPACE) {
			if (w->cur > 0)
				m_field_edit(w, 1, 0);
		} else if (k == WIN_KEY_DELETE) {
			if (w->cur < w->len)
				m_field_edit(w, 2, 0);
		} else if (k == WIN_KEY_LEFT) {
			m_field_edit(w, 3, w->cur > 0 ? w->cur - 1 : 0);
		} else if (k == WIN_KEY_RIGHT) {
			m_field_edit(w, 3, w->cur < w->len ? w->cur + 1 : w->len);
		} else if (k == WIN_KEY_HOME) {
			m_field_edit(w, 3, 0);
		} else if (k == WIN_KEY_END) {
			m_field_edit(w, 3, w->len);
		} else {
			return 0;
		}
		return 1;
	case WGT_LIST:
		if (k == WIN_KEY_ENTER) {
			if (press && w->n > 0 && m_hook(w->id, w->sel) < 0)
				return -1;
			return 1;
		}
		if (w->n == 0)
			return 0;
		to = w->sel;
		if (k == WIN_KEY_UP)
			to--;
		else if (k == WIN_KEY_DOWN)
			to++;
		else if (k == WIN_KEY_PAGE_UP)
			to -= w->rows;
		else if (k == WIN_KEY_PAGE_DOWN)
			to += w->rows;
		else if (k == WIN_KEY_HOME)
			to = 0;
		else if (k == WIN_KEY_END)
			to = w->n - 1;
		else
			return 0;
		to = to < 0 ? 0 : to > w->n - 1 ? w->n - 1 : to;
		m_select(w, to);
		return 1;
	case WGT_MENUBAR:
		return m_menu_key(w, ev);
	default:
		return 0;
	}
}

/* The library side of the main form, and the model built from the same table. */

static struct win_scene scene;
static struct win_input inp;
static struct wgt_form form;
static struct win *mainwin;
static struct mf M;			/* Model of the main form. */

static const struct wgt_entry file_entries[] = {
	{ "&New", 10 }, { "&Open", 11 }, { "E&xit", 12 }
};
static const struct wgt_entry edit_entries[] = { { "Cu&t", 20 }, { "&Copy", 21 } };
static const struct wgt_entry help_entries[] = { { "&About", 30 } };
static const struct wgt_menu menus[] = {
	{ "&File", file_entries, 3 }, { "&Edit", edit_entries, 2 },
	{ "Hel&p", help_entries, 1 }
};
static const char *const names[] = {
	"alpha", "bravo", "charlie", "delta-text-much-too-long-for-the-row", "echo",
	"foxtrot", "golf", "hotel", "india", "juliet", "kilo", "lima"
};
#define NNAMES	12

static struct wgt_menubar bar;
static struct wgt_label lab_name, lab_addr, status;
static struct wgt_field f_name, f_addr;
static char name_buf[16], addr_buf[24];
static struct wgt_check c_verbose, c_auto;
static struct wgt_button b_apply, b_reset;
static struct wgt_list lst;
static struct wgt *ctl[NITEM];

static int
act_hook(struct wgt_form *f, struct wgt *w, int id, int value)
{
	(void)f;
	(void)w;
	logadd(log_lib, id, value);
	return id == veto_id ? -1 : 0;
}

static void
mset(struct mw *w, int kind, int id, int x, int y, int cw, int h)
{
	memset(w, 0, sizeof(*w));
	w->kind = kind;
	w->id = id;
	w->x = x;
	w->y = y;
	w->w = cw;
	w->h = h;
}

static void
mtext(struct mw *w, const char *t)
{
	snprintf(w->text, sizeof(w->text), "%s", t);
}

static void
mfield(struct mw *w, int cap, const char *t)
{
	w->cap = cap;
	w->vis = (w->w - 4) / 6;
	snprintf(w->buf, sizeof(w->buf), "%s", t);
	w->len = (int)strlen(t);
	w->cur = w->len;
	w->scroll = m_adjust(w, 0);
}

static void
build_model(struct mf *f)
{
	struct mw *w = f->it;

	memset(f, 0, sizeof(*f));
	f->x = 10;
	f->y = 10;
	f->w = 300;
	f->h = 300;
	snprintf(f->title, sizeof(f->title), "form");
	f->cw = 298;
	f->ch = 287;
	mset(&w[0], WGT_MENUBAR, 1, 0, 0, 298, 12);
	w[0].menus = menus;
	w[0].nm = 3;
	mset(&w[1], WGT_LABEL, 0, 4, 18, 56, 10);
	mtext(&w[1], "&Name:");
	mset(&w[2], WGT_FIELD, 2, 62, 16, 120, 14);
	mfield(&w[2], 16, "");
	mset(&w[3], WGT_LABEL, 0, 4, 38, 56, 10);
	mtext(&w[3], "Ad&dress:");
	mset(&w[4], WGT_FIELD, 4, 62, 36, 66, 14);
	mfield(&w[4], 24, "hello");
	mset(&w[5], WGT_CHECK, 5, 4, 60, 100, 12);
	mtext(&w[5], "&Verbose");
	mset(&w[6], WGT_CHECK, 6, 4, 76, 100, 12);
	mtext(&w[6], "Au&to save");
	w[6].checked = 1;
	mset(&w[7], WGT_BUTTON, 7, 4, 96, 52, 14);
	mtext(&w[7], "&Apply");
	mset(&w[8], WGT_BUTTON, 8, 62, 96, 52, 14);
	mtext(&w[8], "&Reset");
	mset(&w[9], WGT_LIST, 9, 150, 60, 100, 42);
	w[9].items = names;
	w[9].n = NNAMES;
	w[9].rows = 4;
	mset(&w[10], WGT_STATUS, 0, 0, 273, 298, 14);
	mtext(&w[10], "ready");
	f->n = NITEM;
	f->focus = 0;
}

static void
new_scene(void)
{
	struct win_backend be;

	memset(panel, UNWRITTEN, sizeof(panel));
	n_bytes = n_windows = n_bits_windows = 0;
	fail_after = 0;
	be.rect = be_rect;
	be.palette = NULL;
	be.self = NULL;
	if (win_scene_init(&scene, &be) != 0)
		fail("win_scene_init");
	win_scene_set_bits(&scene, bits_enabled ? be_bits : NULL);
	win_input_init(&inp);
}

static void
build_world(void)
{
	int i;

	memset(name_buf, 0, sizeof(name_buf));
	strcpy(addr_buf, "hello");
	log_lib[0] = log_mod[0] = '\0';
	veto_id = -1;
	new_scene();
	mainwin = win_create(&scene, 10, 10, 300, 300, "form", wgt_form_paint, &form);
	if (mainwin == NULL || wgt_form_init(&form, &scene, mainwin, act_hook, NULL) != 0)
		fail("main window");
	if (wgt_menubar_init(&bar, 1, 0, 0, 298, menus, 3) != 0 ||
	    wgt_label_init(&lab_name, 4, 18, 56, "&Name:") != 0 ||
	    wgt_field_init(&f_name, 2, 62, 16, 120, name_buf, 16) != 0 ||
	    wgt_label_init(&lab_addr, 4, 38, 56, "Ad&dress:") != 0 ||
	    wgt_field_init(&f_addr, 4, 62, 36, 66, addr_buf, 24) != 0 ||
	    wgt_check_init(&c_verbose, 5, 4, 60, 100, "&Verbose", 0) != 0 ||
	    wgt_check_init(&c_auto, 6, 4, 76, 100, "Au&to save", 1) != 0 ||
	    wgt_button_init(&b_apply, 7, 4, 96, 52, "&Apply") != 0 ||
	    wgt_button_init(&b_reset, 8, 62, 96, 52, "&Reset") != 0 ||
	    wgt_list_init(&lst, 9, 150, 60, 100, 4, names, NNAMES) != 0 ||
	    wgt_status_init(&status, 0, 273, 298, "ready") != 0)
		fail("control constructors");
	ctl[0] = &bar.head;
	ctl[1] = &lab_name.head;
	ctl[2] = &f_name.head;
	ctl[3] = &lab_addr.head;
	ctl[4] = &f_addr.head;
	ctl[5] = &c_verbose.head;
	ctl[6] = &c_auto.head;
	ctl[7] = &b_apply.head;
	ctl[8] = &b_reset.head;
	ctl[9] = &lst.head;
	ctl[10] = &status.head;
	for (i = 0; i < NITEM; i++)
		if (wgt_add(&form, ctl[i]) != 0)
			fail("wgt_add %d", i);
	build_model(&M);
	zlist[0] = &M;
	nz = 1;
}

static const char *
kind_name(int k)
{
	static const char *const n[] = { "?", "label", "status", "button", "check",
	    "field", "list", "menubar" };

	return k >= 1 && k <= 7 ? n[k] : "?";
}

/* The library's form state against the model's. */
static void
check_state(const char *what)
{
	int i;
	const struct mw *m;
	const struct wgt *w;

	if (form.focus != M.focus)
		fail("%s: focus %d, model %d", what, form.focus, M.focus);
	for (i = 0; i < NITEM; i++) {
		m = &M.it[i];
		w = ctl[i];
		if (((w->flags & WGT_F_HIDDEN) != 0) != m->hidden ||
		    ((w->flags & WGT_F_DISABLED) != 0) != m->disabled)
			fail("%s: control %d (%s) flags %d, model hidden %d disabled %d",
			    what, i, kind_name(m->kind), w->flags, m->hidden, m->disabled);
		switch (m->kind) {
		case WGT_LABEL:
		case WGT_STATUS:
			if (strcmp(((const struct wgt_label *)w)->text, m->text) != 0)
				fail("%s: control %d text '%s', model '%s'", what, i,
				    ((const struct wgt_label *)w)->text, m->text);
			break;
		case WGT_BUTTON:
			if (strcmp(((const struct wgt_button *)w)->text, m->text) != 0)
				fail("%s: button %d text", what, i);
			break;
		case WGT_CHECK:
			if (((const struct wgt_check *)w)->checked != m->checked ||
			    strcmp(((const struct wgt_check *)w)->text, m->text) != 0)
				fail("%s: check %d state %d, model %d", what, i,
				    ((const struct wgt_check *)w)->checked, m->checked);
			break;
		case WGT_FIELD: {
			const struct wgt_field *fl = (const struct wgt_field *)w;

			if (fl->len != m->len || fl->cur != m->cur ||
			    fl->scroll != m->scroll || strcmp(fl->buf, m->buf) != 0)
				fail("%s: field %d text '%s' len %d cursor %d scroll %d, "
				    "model '%s' %d %d %d", what, i, fl->buf, fl->len,
				    fl->cur, fl->scroll, m->buf, m->len, m->cur, m->scroll);
			break;
		}
		case WGT_LIST: {
			const struct wgt_list *l = (const struct wgt_list *)w;

			if (l->sel != m->sel || l->top != m->top || l->n != m->n)
				fail("%s: list selection %d top %d, model %d %d", what,
				    l->sel, l->top, m->sel, m->top);
			break;
		}
		default: {
			const struct wgt_menubar *b = (const struct wgt_menubar *)w;

			if (b->cur != m->mcur || b->open != m->mopen ||
			    (b->open && b->entry != m->mentry))
				fail("%s: menu cur %d open %d entry %d, model %d %d %d", what,
				    b->cur, b->open, b->entry, m->mcur, m->mopen, m->mentry);
			break;
		}
		}
	}
	if (strcmp(log_lib, log_mod) != 0)
		fail("%s: activations '%s', model '%s'", what, log_lib, log_mod);
}

/* Flush on a working backend and compare the panel with the reference. */
static void
check_image(const char *what)
{
	int i, bad = 0, bx = 0, by = 0;

	fail_after = 0;
	if (win_flush(&scene) != 0)
		fail("%s: flush", what);
	paint_ref();
	for (i = 0; i < PW * PH; i++)
		if (panel[i] != ref[i] && bad++ == 0) {
			bx = i % PW;
			by = i / PW;
		}
	if (bad != 0)
		fail("%s: %d pixels differ from the reference, first at %d,%d "
		    "(panel %u, reference %u)", what, bad, bx, by, panel[by * PW + bx],
		    ref[by * PW + bx]);
}

static void
check_all(const char *what)
{
	check_state(what);
	check_image(what);
}

static struct win_event
key(int k, int mods, int kind)
{
	struct win_event ev;

	ev.key = k;
	ev.mods = (unsigned char)mods;
	ev.kind = (unsigned char)kind;
	return ev;
}

/* Send one event through the library and the model; compare their answers. */
static void
send_event(const struct win_event *ev, struct mf *model)
{
	int rl, rm;

	rl = wgt_form_event(&scene, mainwin, ev, &form);
	rm = m_key(model, ev);
	if (rl != rm)
		fail("key %d mods %d kind %d: library returned %d, model %d", ev->key,
		    ev->mods, ev->kind, rl, rm);
}

/* Model-side flag changes, mirroring the library's focus rule. */
static void
m_set_flag(struct mf *f, int i, int flag, int on)
{
	struct mw *w = &f->it[i];
	int *p = flag == WGT_F_HIDDEN ? &w->hidden : &w->disabled;
	int lose;

	if (*p == on)
		return;
	*p = on;
	lose = i == f->focus && on;
	if (w->kind == WGT_MENUBAR && on)
		w->mopen = 0;
	if (lose)
		f->focus = m_pick(f, i, 1);
}

static void
set_flag_both(int i, int flag, int on)
{
	int r = flag == WGT_F_HIDDEN ? wgt_set_shown(&form, ctl[i], !on) :
	    wgt_set_enabled(&form, ctl[i], !on);

	if (r != 0)
		fail("set flag %d on control %d: %d", flag, i, r);
	m_set_flag(&M, i, flag, on);
}

static void
focus_both(int i)
{
	if (wgt_focus(&form, ctl[i]) != 0)
		fail("wgt_focus %d", i);
	m_setfocus(&M, i);
}

static uint32_t rs = 0x2545f491u;

static int
rnd(int n)
{
	rs ^= rs << 13;
	rs ^= rs >> 17;
	rs ^= rs << 5;
	return (int)(rs % (uint32_t)n);
}

static void
run_golden(void)
{
	int i, j, k, n;
	static const char long_text[] = "abcdefghijklmnopqrstuvw";

	op_index = -10;
	bits_enabled = 1;
	build_world();
	check_all("initial");
	for (i = 0; i < NITEM; i++) {
		if (!m_focusable(&M.it[i]))
			continue;
		focus_both(i);
		note("focus %d", i);
		check_all("focus each control");
	}
	for (i = 0; i < NITEM; i++) {
		note("disable %d", i);
		set_flag_both(i, WGT_F_DISABLED, 1);
		check_all("disabled control");
		set_flag_both(i, WGT_F_DISABLED, 0);
		set_flag_both(i, WGT_F_HIDDEN, 1);
		check_all("hidden control");
		set_flag_both(i, WGT_F_HIDDEN, 0);
		check_all("shown again");
	}
	focus_both(0);
	for (i = 0; i < 3; i++) {
		if (wgt_menu_open(&form, &bar, i) != 0)
			fail("wgt_menu_open %d", i);
		M.it[0].mcur = i;
		M.it[0].mopen = 1;
		M.it[0].mentry = 0;
		check_all("menu open");
		for (j = 1; j < menus[i].n; j++) {
			struct win_event ev = key(WIN_KEY_DOWN, 0, WIN_EV_PRESS);

			send_event(&ev, &M);
			check_all("menu entry");
		}
		if (wgt_menu_close(&form, &bar) != 0)
			fail("wgt_menu_close");
		M.it[0].mopen = 0;
		M.it[0].mentry = 0;
		check_all("menu closed");
	}
	focus_both(9);
	for (i = 1; i < NNAMES; i++) {
		if (wgt_list_select(&form, &lst, i) != 0)
			fail("wgt_list_select %d", i);
		m_select(&M.it[9], i);
		check_all("list scroll down");
	}
	for (i = NNAMES - 2; i >= 0; i--) {
		wgt_list_select(&form, &lst, i);
		m_select(&M.it[9], i);
		check_all("list scroll up");
	}
	focus_both(4);
	if (wgt_field_set_text(&form, &f_addr, long_text) != 0)
		fail("field set text");
	snprintf(M.it[4].buf, sizeof(M.it[4].buf), "%s", long_text);
	M.it[4].len = M.it[4].cur = (int)strlen(long_text);
	M.it[4].scroll = m_adjust(&M.it[4], M.it[4].scroll);
	check_all("long field text");
	n = M.it[4].len;
	for (k = n; k >= 0; k--) {
		wgt_field_move(&form, &f_addr, k);
		m_field_edit(&M.it[4], 3, k);
		check_all("field cursor");
	}
	for (k = 0; k <= n; k++) {
		wgt_field_move(&form, &f_addr, k);
		m_field_edit(&M.it[4], 3, k);
		check_all("field cursor up");
	}
	if (wgt_set_text(&form, &status.head, "busy now") != 0)
		fail("set status text");
	mtext(&M.it[10], "busy now");
	check_all("status text");
	if (wgt_set_text(&form, &b_apply.head, "A&pply") != 0 &&
	    wgt_set_text(&form, &b_apply.head, "&Aply") != 0)
		fail("set button text");
	printf("PASS golden: every control in focus, disabled, hidden, menu, list "
	    "scroll and field cursor states equals the reference image\n");
}

/* Paint through hand-built contexts: nothing outside the clip changes. */
static void
run_clip(void)
{
	static unsigned char sb[WIN_GUARD + PW * 16 + WIN_GUARD];
	struct win_ctx c;
	int it, i, bad;

	op_index = -11;
	build_world();
	check_all("clip fixture");
	focus_both(0);
	wgt_menu_open(&form, &bar, 0);
	for (it = 0; it < 400; it++) {
		memset(sb, 0xee, sizeof(sb));
		memset(sb, WIN_GUARD_BYTE, WIN_GUARD);
		memset(sb + WIN_GUARD + PW * 16, WIN_GUARD_BYTE, WIN_GUARD);
		c.buf = sb + WIN_GUARD;
		c.sx = rnd(200);
		c.sw = 1 + rnd(PW - c.sx);
		c.sy = rnd(300);
		c.sh = 1 + rnd(16);
		c.ox = 11;
		c.oy = 22;
		c.w = 298;
		c.h = 287;
		c.cx0 = c.sx + rnd(c.sw);
		c.cx1 = c.cx0 + 1 + rnd(c.sx + c.sw - c.cx0);
		c.cy0 = c.sy + rnd(c.sh);
		c.cy1 = c.cy0 + 1 + rnd(c.sy + c.sh - c.cy0);
		wgt_form_paint(&c, &form);
		bad = 0;
		for (i = 0; i < WIN_GUARD; i++)
			bad += sb[i] != WIN_GUARD_BYTE ||
			    sb[WIN_GUARD + PW * 16 + i] != WIN_GUARD_BYTE;
		for (i = 0; i < PW * 16; i++) {
			int x = c.sx + i % c.sw, y = c.sy + i / c.sw;

			if (i >= c.sw * c.sh) {
				bad += c.buf[i] != 0xee;
				continue;
			}
			if ((x < c.cx0 || x >= c.cx1 || y < c.cy0 || y >= c.cy1) &&
			    c.buf[i] != 0xee)
				bad++;
		}
		if (bad != 0)
			fail("paint wrote %d bytes outside its clip %d,%d-%d,%d of strip "
			    "%d,%d %dx%d", bad, c.cx0, c.cy0, c.cx1, c.cy1, c.sx, c.sy,
			    c.sw, c.sh);
	}
	printf("PASS clip: 400 paints into clipped strips left every byte outside "
	    "the clip and the guards intact\n");
}

static void
run_traversal(void)
{
	int cfg, i, cnt, k, dir, start, seen[NITEM], here;

	op_index = -12;
	build_world();
	check_all("traversal fixture");
	for (cfg = 0; cfg < 150; cfg++) {
		for (i = 0; i < NITEM; i++) {
			set_flag_both(i, WGT_F_HIDDEN, rnd(5) == 0);
			set_flag_both(i, WGT_F_DISABLED, rnd(5) == 0);
		}
		cnt = 0;
		start = -1;
		for (i = 0; i < NITEM; i++)
			if (m_focusable(&M.it[i])) {
				cnt++;
				if (start < 0)
					start = i;
			}
		if (cnt == 0) {
			struct win_event ev = key(WIN_KEY_TAB, 0, WIN_EV_PRESS);

			send_event(&ev, &M);
			if (form.focus != -1)
				fail("Tab focused control %d with none focusable", form.focus);
			continue;
		}
		focus_both(start);
		for (dir = 0; dir < 2; dir++) {
			struct win_event ev = key(dir ? WIN_KEY_F1 + 4 : WIN_KEY_TAB, 0,
			    WIN_EV_PRESS);

			memset(seen, 0, sizeof(seen));
			seen[start] = 1;
			for (k = 1; k <= cnt; k++) {
				send_event(&ev, &M);
				here = form.focus;
				if (here < 0 || !wgt_is_focusable(ctl[here]))
					fail("traversal reached control %d, not focusable", here);
				if (k < cnt && seen[here]++)
					fail("traversal visited control %d twice in one cycle", here);
				if (k == cnt && here != start)
					fail("traversal did not return to %d after %d keys (at %d)",
					    start, cnt, here);
			}
			for (i = 0; i < NITEM; i++)
				if (m_focusable(&M.it[i]) && !seen[i])
					fail("traversal skipped control %d", i);
		}
		check_state("traversal");
	}
	for (i = 0; i < NITEM; i++) {
		set_flag_both(i, WGT_F_HIDDEN, 0);
		set_flag_both(i, WGT_F_DISABLED, 0);
	}
	printf("PASS traversal: 150 flag layouts, Tab and F5 visit each enabled shown "
	    "control once per cycle and no other\n");
}

static void
run_mnemonics(void)
{
	static const struct { int letter, focus, toggles, act, open; } exp[] = {
		{ 'n', 2, 0, 0, -1 }, { 'd', 4, 0, 0, -1 }, { 'v', 5, 1, 5, -1 },
		{ 't', 6, 1, 6, -1 }, { 'a', 7, 0, 7, -1 }, { 'r', 8, 0, 8, -1 },
		{ 'f', 0, 0, 0, 0 }, { 'e', 0, 0, 0, 1 }, { 'p', 0, 0, 0, 2 },
	};
	size_t t;
	int i;
	struct win_event ev;

	op_index = -13;
	for (t = 0; t < sizeof(exp) / sizeof(exp[0]); t++) {
		build_world();
		check_all("mnemonic fixture");
		focus_both(9);
		log_lib[0] = log_mod[0] = '\0';
		ev = key(exp[t].letter, WIN_MOD_CTRL, WIN_EV_PRESS);
		if (wgt_form_event(&scene, mainwin, &ev, &form) != 1)
			fail("Ctrl-%c not consumed", exp[t].letter);
		if (form.focus != exp[t].focus)
			fail("Ctrl-%c focused control %d, expected %d", exp[t].letter,
			    form.focus, exp[t].focus);
		if (exp[t].open >= 0 && (!bar.open || bar.cur != exp[t].open))
			fail("Ctrl-%c opened title %d (open %d), expected %d", exp[t].letter,
			    bar.cur, bar.open, exp[t].open);
		if (exp[t].open < 0 && bar.open)
			fail("Ctrl-%c opened a menu", exp[t].letter);
		if (exp[t].toggles && ((struct wgt_check *)ctl[exp[t].focus])->checked ==
		    (exp[t].letter == 't'))
			fail("Ctrl-%c did not toggle its check box", exp[t].letter);
		if (exp[t].act != 0) {
			char want[24];

			snprintf(want, sizeof(want), "%d:%d;", exp[t].act,
			    exp[t].toggles ? (exp[t].letter == 'v') : 0);
			if (strcmp(log_lib, want) != 0)
				fail("Ctrl-%c activations '%s', expected '%s'", exp[t].letter,
				    log_lib, want);
		}
		if (exp[t].act == 0 && log_lib[0] != '\0')
			fail("Ctrl-%c activated '%s'", exp[t].letter, log_lib);
	}
	/* A disabled or hidden control has no mnemonic; Ctrl-W stays free. */
	build_world();
	set_flag_both(7, WGT_F_DISABLED, 1);
	set_flag_both(8, WGT_F_HIDDEN, 1);
	log_lib[0] = log_mod[0] = '\0';
	for (i = 0; i < 2; i++) {
		ev = key(i ? 'r' : 'a', WIN_MOD_CTRL, WIN_EV_PRESS);
		if (wgt_form_event(&scene, mainwin, &ev, &form) != 0 || log_lib[0])
			fail("a disabled or hidden control answered its mnemonic");
	}
	/* A pull-down entry's letter runs the entry only while the menu is open. */
	ev = key('x', 0, WIN_EV_PRESS);
	focus_both(0);
	wgt_form_event(&scene, mainwin, &ev, &form);
	if (log_lib[0])
		fail("a closed menu ran an entry on a bare letter");
	wgt_menu_open(&form, &bar, 0);
	if (wgt_form_event(&scene, mainwin, &ev, &form) != 1 || strcmp(log_lib, "12:0;") != 0 ||
	    bar.open)
		fail("letter x in the File menu logged '%s' open %d", log_lib, bar.open);
	/* A vetoed activation changes nothing: no toggle, no focus move. */
	build_world();
	focus_both(9);
	veto_id = 5;
	ev = key('v', WIN_MOD_CTRL, WIN_EV_PRESS);
	if (wgt_form_event(&scene, mainwin, &ev, &form) != -1 || c_verbose.checked ||
	    form.focus != 9)
		fail("a vetoed mnemonic toggled the check box or moved the focus");
	focus_both(5);
	ev = key(' ', 0, WIN_EV_PRESS);
	if (wgt_form_event(&scene, mainwin, &ev, &form) != -1 || c_verbose.checked)
		fail("a vetoed Space toggled the check box");
	veto_id = -1;
	printf("PASS mnemonics: each Ctrl-letter reaches its control, menu letters "
	    "act only in an open menu, disabled and hidden controls are skipped\n");
}

static void
run_stream(int n)
{
	static const int base[] = { WIN_KEY_BACKSPACE, WIN_KEY_TAB, WIN_KEY_ENTER,
	    WIN_KEY_ESC, WIN_KEY_LEFT, WIN_KEY_UP, WIN_KEY_DOWN, WIN_KEY_RIGHT,
	    WIN_KEY_HOME, WIN_KEY_END, WIN_KEY_DELETE, WIN_KEY_PAGE_UP,
	    WIN_KEY_PAGE_DOWN, WIN_KEY_F1 + 4, ' ', WIN_KEY_INSERT };
	static const int vetoes[] = { -1, 7, 8, 5, 10, 11, 2, 9 };
	struct win_event ev;
	int it, r, j, k, mods, kind;

	op_index = -14;
	build_world();
	check_all("stream fixture");
	for (it = 0; it < n; it++) {
		op_index = it;
		r = rnd(100);
		if (r < 80) {
			k = rnd(10) < 3 ? 'a' + rnd(26) : rnd(10) < 4 ?
			    base[rnd((int)(sizeof(base) / sizeof(base[0])))] :
			    0x20 + rnd(95);
			mods = k >= 'a' && k <= 'z' && rnd(2) ? WIN_MOD_CTRL : 0;
			kind = rnd(20) == 0 ? WIN_EV_RELEASE : rnd(8) == 0 ? WIN_EV_REPEAT :
			    WIN_EV_PRESS;
			ev = key(k, mods, kind);
			note("key %d mods %d kind %d", k, mods, kind);
			send_event(&ev, &M);
		} else if (r < 88) {
			j = rnd(NITEM);
			note("enabled %d", j);
			set_flag_both(j, WGT_F_DISABLED, !M.it[j].disabled);
		} else if (r < 94) {
			j = rnd(NITEM);
			note("shown %d", j);
			set_flag_both(j, WGT_F_HIDDEN, !M.it[j].hidden);
		} else if (r < 97) {
			j = 5 + rnd(2);
			k = rnd(2);
			note("check %d", j);
			if (wgt_check_set(&form, (struct wgt_check *)ctl[j], k) != 0)
				fail("wgt_check_set");
			M.it[j].checked = k;
		} else {
			veto_id = vetoes[rnd(8)];
		}
		check_all("stream");
	}
	printf("PASS stream: %d seeded keys and state changes match the model of "
	    "traversal, mnemonics, fields, lists and menus and the reference image\n", n);
}

/* Damage bookkeeping: bytes for no-op sets and one-character changes. */
static long
flush_bytes(void)
{
	long b = n_bytes, w = n_windows;

	if (win_flush(&scene) != 0)
		fail("flush");
	last_windows = n_windows - w;
	return n_bytes - b;
}

static void
run_bytes(void)
{
	long b;
	int pass;

	op_index = -15;
	for (pass = 0; pass < 2; pass++) {
		bits_enabled = pass;
		build_world();
		check_all("bytes fixture");
		focus_both(0);
		check_all("bytes settled");
		(void)flush_bytes();
		/* A state set that changes nothing writes nothing. */
		wgt_check_set(&form, &c_verbose, 0);
		wgt_field_move(&form, &f_addr, f_addr.cur);
		wgt_list_select(&form, &lst, 0);
		wgt_set_text(&form, &status.head, "ready");
		wgt_set_text(&form, &b_apply.head, "&Apply");
		wgt_field_set_text(&form, &f_name, "");
		wgt_set_enabled(&form, &b_apply.head, 1);
		wgt_set_shown(&form, &b_apply.head, 1);
		wgt_focus(&form, ctl[0]);
		wgt_menu_close(&form, &bar);
		if (scene.ndamage != 0)
			fail("no-op sets left %d damage rectangles", scene.ndamage);
		if ((b = flush_bytes()) != 0 || last_windows != 0)
			fail("no-op sets wrote %ld bytes in %ld windows", b, last_windows);
		/* One changed character of an unfocused field: one glyph cell. */
		wgt_field_set_text(&form, &f_addr, "hello");
		(void)flush_bytes();
		wgt_set_text(&form, &status.head, "ready");
		/* status "ready" -> "reedy": cell 2 only (6 x 10 pixels). */
		if (wgt_set_text(&form, &status.head, "reedy") != 0)
			fail("status set text");
		b = flush_bytes();
		if (last_windows != 1 || b != (pass ? 10 : 60))
			fail("one changed status character: %ld bytes in %ld windows, "
			    "expected %d in 1 (bits %s)", b, last_windows, pass ? 10 : 60,
			    pass ? "on" : "off");
		/* One typed character in the focused field: one run of two cells. */
		focus_both(4);
		(void)flush_bytes();
		if (wgt_field_insert(&form, &f_addr, 'x') != 0)
			fail("field insert");
		b = flush_bytes();
		if (last_windows != 1 || b != 120)
			fail("one typed character: %ld bytes in %ld windows, expected "
			    "120 in 1 (bits %s)", b, last_windows, pass ? "on" : "off");
		/* One check box toggle: the 9 x 9 box. */
		wgt_check_set(&form, &c_verbose, 1);
		b = flush_bytes();
		if (last_windows != 1 || b != (pass ? 18 : 81))
			fail("one check box: %ld bytes in %ld windows, expected %d in 1",
			    b, last_windows, pass ? 18 : 81);
		printf("MEASURE widgets bits %s: no-op set 0 bytes; one changed "
		    "character of a status line 6x10 = %d bytes; one typed character "
		    "in a focused field 12x10 = 120 bytes; one check box 9x9 = %d bytes\n",
		    pass ? "on" : "off", pass ? 10 : 60, pass ? 18 : 81);
	}
	bits_enabled = 1;
	printf("PASS bytes: no-op sets wrote nothing; one changed character wrote one "
	    "glyph cell and one typed character one run\n");
}

/* Failing-backend sweep */

struct snap {
	struct wgt_form form;
	struct wgt_menubar bar;
	struct wgt_label l1, l2, st;
	struct wgt_field f1, f2;
	char nb[sizeof(name_buf)], ab[sizeof(addr_buf)];
	struct wgt_check c1, c2;
	struct wgt_button b1, b2;
	struct wgt_list lst;
	int ndamage, focus, nwin;
	struct win_rect dmg[WIN_DAMAGE_MAX];
	unsigned char z[WIN_MAX];
};

static void
take_snap(struct snap *s)
{
	memset(s, 0, sizeof(*s));
	memcpy(&s->form, &form, sizeof(form));
	memcpy(&s->bar, &bar, sizeof(bar));
	memcpy(&s->l1, &lab_name, sizeof(lab_name));
	memcpy(&s->l2, &lab_addr, sizeof(lab_addr));
	memcpy(&s->st, &status, sizeof(status));
	memcpy(&s->f1, &f_name, sizeof(f_name));
	memcpy(&s->f2, &f_addr, sizeof(f_addr));
	memcpy(s->nb, name_buf, sizeof(name_buf));
	memcpy(s->ab, addr_buf, sizeof(addr_buf));
	memcpy(&s->c1, &c_verbose, sizeof(c_verbose));
	memcpy(&s->c2, &c_auto, sizeof(c_auto));
	memcpy(&s->b1, &b_apply, sizeof(b_apply));
	memcpy(&s->b2, &b_reset, sizeof(b_reset));
	memcpy(&s->lst, &lst, sizeof(lst));
	s->ndamage = scene.ndamage;
	s->focus = scene.focus;
	s->nwin = scene.nwin;
	memcpy(s->dmg, scene.damage, sizeof(s->dmg));
	memcpy(s->z, scene.z, sizeof(s->z));
}

#define NSWEEP	21

static int
sweep_op(int op)
{
	struct win_event ev;

	switch (op) {
	case 0:	return wgt_focus(&form, &f_addr.head);
	case 1:	return wgt_traverse(&form, 1);
	case 2:	return wgt_traverse(&form, 0);
	case 3:	return wgt_set_text(&form, &lab_name.head, "Other");
	case 4:	return wgt_set_enabled(&form, &c_verbose.head, 0);
	case 5:	return wgt_set_shown(&form, &b_apply.head, 0);
	case 6:	return wgt_check_set(&form, &c_verbose, 1);
	case 7:	return wgt_field_set_text(&form, &f_name, "abc");
	case 8:	return wgt_field_insert(&form, &f_addr, 'z');
	case 9:	return wgt_field_delete(&form, &f_addr, 0);
	case 10: return wgt_field_move(&form, &f_addr, 0);
	case 11: return wgt_list_select(&form, &lst, 7);
	case 12: return wgt_list_select(&form, &lst, 1);
	case 13: return wgt_list_set(&form, &lst, names, 5);
	case 14: return wgt_menu_open(&form, &bar, 1);
	case 15: return wgt_menu_close(&form, &bar);
	case 16: ev = key(WIN_KEY_TAB, 0, WIN_EV_PRESS); break;
	case 17: ev = key('a', WIN_MOD_CTRL, WIN_EV_PRESS); break;
	case 18: ev = key(WIN_KEY_DOWN, 0, WIN_EV_PRESS); break;
	case 19: return wgt_form_refresh(&form);
	case 20: return wgt_set_text(&form, &status.head, "busy");
	default: return -1;
	}
	return wgt_form_event(&scene, mainwin, &ev, &form) < 0 ? -1 : 0;
}

static void
run_failure(void)
{
	static unsigned char before[PW * PH];
	struct snap a, b;
	int op, fill, i, r, nfail[NSWEEP];
	long cases = 0, failed = 0;

	op_index = -20;
	memset(nfail, 0, sizeof(nfail));
	for (op = 0; op < NSWEEP; op++)
		for (fill = 0; fill <= 7; fill++) {
			bits_enabled = fill & 1;
			build_world();
			focus_both(0);
			check_all("sweep fixture");
			for (i = 0; i < fill; i++)
				if (win_invalidate(&scene, 0, i * 30, 4, 4) != 0)
					fail("fill");
			take_snap(&a);
			note("sweep op %d fill %d", op, fill);
			fail_after = 1;
			r = sweep_op(op);
			fail_after = 0;
			cases++;
			if (scene.ndamage > WIN_DAMAGE_MAX)
				fail("damage list overflow");
			if (r < 0) {
				failed++;
				nfail[op]++;
				take_snap(&b);
				if (memcmp(&a, &b, sizeof(a)) != 0)
					fail("op %d at fill %d failed at the backend but changed "
					    "state or damage", op, fill);
				check_image("after a failed operation");
				continue;
			}
			/* Success: what the incremental repaint drew equals a full repaint. */
			if (win_flush(&scene) != 0)
				fail("flush");
			memcpy(before, panel, sizeof(before));
			if (win_invalidate(&scene, 0, 0, PW, PH) != 0 || win_flush(&scene) != 0)
				fail("full repaint");
			if (memcmp(before, panel, sizeof(before)) != 0)
				fail("op %d at fill %d: the incremental repaint differs from "
				    "a repaint of the retained state", op, fill);
		}
	for (op = 0; op < NSWEEP; op++)
		if (nfail[op] == 0 && op != 15)
			fail("operation %d never failed at the backend", op);
	bits_enabled = 1;
	printf("PASS failure sweep: %d operations at 8 damage fills, %ld of %ld "
	    "cases failed at the backend and each left controls, focus, scene and "
	    "damage unchanged; each success repainted exactly the retained state\n",
	    NSWEEP, failed, cases);
}

/* Dialogs */

static struct wgt_dialog dlg;
static struct mf D;

static void
build_dlg_model(struct mf *d, const char *title, const char *text, int cancel)
{
	int len = (int)strlen(text), w = 6 * len + 18, ox, cw;

	if (w < 130)
		w = 130;
	memset(d, 0, sizeof(*d));
	d->w = w;
	d->h = 70;
	d->x = (PW - w) / 2;
	d->y = (PH - 70) / 2;
	snprintf(d->title, sizeof(d->title), "%s", title);
	d->cw = w - 2;
	d->ch = 70 - 13;
	cw = d->cw;
	mset(&d->it[0], WGT_LABEL, 0, 8, 10, cw - 16, 10);
	mtext(&d->it[0], text);
	ox = cancel ? cw / 2 - 52 - 4 : (cw - 52) / 2;
	mset(&d->it[1], WGT_BUTTON, 1, ox, 37, 52, 14);
	mtext(&d->it[1], "&OK");
	d->n = 2;
	if (cancel) {
		mset(&d->it[2], WGT_BUTTON, 2, ox + 60, 37, 52, 14);
		mtext(&d->it[2], "&Cancel");
		d->n = 3;
	}
	d->focus = 1;
}

static void
dlg_image(const char *what)
{
	check_image(what);
}

static struct win_event
press(int k, int mods)
{
	return key(k, mods, WIN_EV_PRESS);
}

static int
feed(struct win_event ev)
{
	return win_input_event(&inp, &scene, &ev);
}

static void
open_box(int cancel)
{
	if (wgt_msgbox_open(&dlg, &scene, &inp, "Confirm", "Save settings?", cancel) != 0)
		fail("wgt_msgbox_open");
	build_dlg_model(&D, "Confirm", "Save settings?", cancel);
	zlist[1] = &D;
	nz = 2;
	if (win_focused(&scene) != dlg.form.win || dlg.result != WGT_RES_NONE ||
	    dlg.form.focus != 1)
		fail("a dialog must take the focus (focus %p, result %d, control %d)",
		    (void *)win_focused(&scene), dlg.result, dlg.form.focus);
	dlg_image("dialog opened");
}

static void
closed(int result, const char *what)
{
	if (dlg.result != result)
		fail("%s: result %d, expected %d", what, dlg.result, result);
	if (win_focused(&scene) != mainwin)
		fail("%s: the dialog left the focus behind", what);
	if (scene.nwin != 1 || inp.modal != NULL)
		fail("%s: dialog window or modal state left (windows %d)", what,
		    scene.nwin);
	nz = 1;
	check_image(what);
}

static void
run_dialog(void)
{
	struct win *b;
	int i, r;

	op_index = -30;
	build_world();
	check_all("dialog fixture");
	/* OK and Cancel; Tab reaches Cancel; Enter on it cancels. */
	open_box(1);
	if (feed(press(WIN_KEY_F1 + 5, 0)) != WIN_DISP_MANAGER || win_focused(&scene) != dlg.form.win)
		fail("F6 left a modal dialog");
	(void)feed(key(WIN_KEY_F1 + 5, 0, WIN_EV_RELEASE));
	if (feed(press(WIN_KEY_TAB, 0)) != WIN_DISP_WINDOW || dlg.form.focus != 2)
		fail("Tab did not reach Cancel (control %d)", dlg.form.focus);
	D.focus = 2;
	dlg_image("Cancel focused");
	if (feed(press(WIN_KEY_ENTER, 0)) != WIN_DISP_WINDOW)
		fail("Enter on Cancel");
	closed(WGT_RES_CANCEL, "Enter on Cancel");
	/* Esc cancels. */
	open_box(1);
	(void)feed(press(WIN_KEY_ESC, 0));
	closed(WGT_RES_CANCEL, "Esc");
	/* Ctrl-O presses OK by mnemonic. */
	open_box(1);
	(void)feed(press('o', WIN_MOD_CTRL));
	closed(WGT_RES_OK, "Ctrl-O");
	/* Enter with OK focused. */
	open_box(1);
	(void)feed(press(WIN_KEY_ENTER, 0));
	closed(WGT_RES_OK, "Enter on OK");
	/* A close request cancels. */
	open_box(1);
	(void)feed(press(WIN_KEY_F1 + 9, 0));
	closed(WGT_RES_CANCEL, "close request");
	/* No Cancel button: Esc answers OK. */
	if (wgt_msgbox_open(&dlg, &scene, &inp, "Note", "Done", 0) != 0)
		fail("box without cancel");
	build_dlg_model(&D, "Note", "Done", 0);
	zlist[1] = &D;
	nz = 2;
	dlg_image("box without Cancel");
	(void)feed(press(WIN_KEY_ESC, 0));
	closed(WGT_RES_OK, "Esc without Cancel");
	/* After a dialog the window keys work again. */
	b = win_create(&scene, 20, 20, 80, 60, "b", NULL, NULL);
	if (b == NULL)
		fail("create b");
	zlist[1] = NULL;
	if (feed(press(WIN_KEY_F1 + 5, 0)) != WIN_DISP_MANAGER || win_focused(&scene) != mainwin)
		fail("F6 stayed blocked after the dialog closed");
	(void)feed(key(WIN_KEY_F1 + 5, 0, WIN_EV_RELEASE));
	/* Focus goes back to the opener, not to the window below the dialog. */
	scene.focus_follows_raise = 0;
	if (win_lower(&scene, mainwin) != 0 || win_focus(&scene, mainwin) != 0)
		fail("focus main");
	if (scene.z[scene.nwin - 1] == scene.focus)
		fail("the opener must not be the top window in this case");
	if (wgt_msgbox_open(&dlg, &scene, &inp, "Confirm", "Again?", 1) != 0)
		fail("open over two windows");
	(void)feed(press(WIN_KEY_ESC, 0));
	if (win_focused(&scene) != mainwin || dlg.result != WGT_RES_CANCEL)
		fail("the dialog did not give the focus back to its opener");
	if (win_destroy(&scene, b) != 0)
		fail("destroy b");
	/* Enter in a field of a dialog without a hook presses OK. */
	{
		static struct wgt_field fld;
		static char fbuf[8];

		if (wgt_dialog_init(&dlg, &inp, "Edit", 130, 80, 1) != 0 ||
		    wgt_field_init(&fld, 3, 8, 10, 100, fbuf, sizeof(fbuf)) != 0 ||
		    wgt_add(&dlg.form, &fld.head) != 0 ||
		    wgt_dialog_open(&dlg, &scene, 95, 100) != 0)
			fail("dialog with a field");
		if (dlg.form.focus != 0)
			fail("the field must hold the focus first");
		(void)feed(press('a', 0));
		if (feed(press(WIN_KEY_ENTER, 0)) != WIN_DISP_WINDOW ||
		    dlg.result != WGT_RES_OK || fbuf[0] != 'a')
			fail("Enter in a dialog field did not press OK (result %d)", dlg.result);
		/* A hook that consumes Enter keeps the dialog open. */
		dlg.result = WGT_RES_NONE;
	}
	/* Capacity: both buttons fit or neither is added; a retry after freeing works. */
	{
		static struct wgt_label fill[WGT_MAX];
		int ncaller, cancel;

		for (ncaller = 10; ncaller <= WGT_MAX; ncaller++)
			for (cancel = 0; cancel < 2; cancel++) {
				int fits = ncaller + 1 + cancel <= WGT_MAX;

				build_world();
				if (wgt_dialog_init(&dlg, &inp, "Full", 130, 80, cancel) != 0)
					fail("capacity init");
				for (i = 0; i < ncaller; i++)
					if (wgt_label_init(&fill[i], 0, 0, 10, "x") != 0 ||
					    wgt_add(&dlg.form, &fill[i].head) != 0)
						fail("capacity filler %d", i);
				r = wgt_dialog_open(&dlg, &scene, 95, 100);
				if ((r == 0) != fits)
					fail("%d controls, cancel %d: open returned %d", ncaller,
					    cancel, r);
				if (!fits) {
					if (dlg.form.n != ncaller || scene.nwin != 1)
						fail("a refused open changed the form (%d controls) or scene",
						    dlg.form.n);
					dlg.form.n--;	/* Free one control and retry. */
					r = wgt_dialog_open(&dlg, &scene, 95, 100);
					if ((r == 0) != (ncaller - 1 + 1 + cancel <= WGT_MAX))
						fail("the retry after freeing a control returned %d", r);
				}
			}
	}
	/* A backend failure: open and finish fail whole and can be repeated. */
	for (r = 0; r < 2; r++) {
		build_world();
		check_all("failure fixture");
		for (i = 0; i < 7; i++)
			(void)win_invalidate(&scene, 0, i * 30, 4, 4);
		fail_after = 1;
		if (wgt_msgbox_open(&dlg, &scene, &inp, "Confirm", "Save?", 1) == 0)
			fail("an open that hit a backend failure reported success");
		fail_after = 0;
		if (scene.nwin != 1 || win_focused(&scene) != mainwin || inp.modal != NULL)
			fail("a failed open changed the scene");
		if (wgt_msgbox_open(&dlg, &scene, &inp, "Confirm", "Save?", 1) != 0)
			fail("the open could not be repeated");
		if (win_flush(&scene) != 0)
			fail("flush");
		for (i = 0; i < 7; i++)
			(void)win_invalidate(&scene, 0, i * 30, 4, 4);
		fail_after = 1;
		if (feed(press(WIN_KEY_ESC, 0)) != -1)
			fail("a finish that hit a backend failure did not report it");
		fail_after = 0;
		if (dlg.result != WGT_RES_NONE || scene.nwin != 2 ||
		    win_focused(&scene) != dlg.form.win || inp.modal != dlg.form.win)
			fail("a failed finish changed the dialog");
		if (feed(press(WIN_KEY_ESC, 0)) != WIN_DISP_WINDOW || dlg.result != WGT_RES_CANCEL)
			fail("the finish could not be repeated");
	}
	printf("PASS dialogs: results, Tab, mnemonic, Esc, close request, focus "
	    "return to the opener, modal keys, and backend failures in open and "
	    "finish leave the scene as it was\n");
}

int
main(void)
{
	run_golden();
	run_clip();
	run_traversal();
	run_mnemonics();
	run_stream(4000);
	run_bytes();
	run_failure();
	run_dialog();
	printf("sizeof: label %zu button %zu check %zu field %zu list %zu menubar %zu "
	    "form %zu dialog %zu\n", sizeof(struct wgt_label), sizeof(struct wgt_button),
	    sizeof(struct wgt_check), sizeof(struct wgt_field), sizeof(struct wgt_list),
	    sizeof(struct wgt_menubar), sizeof(struct wgt_form), sizeof(struct wgt_dialog));
	printf("PASS widgets\n");
	return 0;
}
