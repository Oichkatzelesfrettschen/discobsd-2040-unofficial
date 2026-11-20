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
 * libwin input: decoders, window-management keys and dispatch. See
 * win_input.h for the model. No system call is made here, so the host gate
 * drives every path with scripted events.
 */
#include <string.h>

#include <sys/lcdcons.h>

#include "win_input.h"

#define ST_GROUND	0
#define ST_ESC		1	/* ESC seen. */
#define ST_CSI		2	/* ESC [ and parameters seen. */
#define ST_SS3		3	/* ESC O seen. */
#define NUM_CAP		999	/* A CSI parameter saturates here. */

/* Raw key bytes (picokbd_map.c): the controller codes above the tty's. */
#define RAW_INSERT	0xd1
#define RAW_HOME	0xd2
#define RAW_DELETE	0xd4
#define RAW_END		0xd5
#define RAW_PAGE_UP	0xd6
#define RAW_PAGE_DOWN	0xd7
#define RAW_F1		0x81	/* F1-F9 are 0x81-0x89. */
#define RAW_F10		0x90

/* The one table of manager bindings; see win_input.h for the reasons. */
const struct win_binding win_bindings[] = {
	{ WIN_KEY_F1 + 5, 0, WIN_ACT_NEXT },
	{ WIN_KEY_TAB, WIN_MOD_CTRL, WIN_ACT_NEXT },
	{ WIN_KEY_F1 + 6, 0, WIN_ACT_PREV },
	{ WIN_KEY_BACKSPACE, WIN_MOD_CTRL, WIN_ACT_PREV },
	{ WIN_KEY_F1 + 7, 0, WIN_ACT_MOVE },
	{ WIN_KEY_F1 + 8, 0, WIN_ACT_RESIZE },
	{ WIN_KEY_F10, 0, WIN_ACT_CLOSE },
	{ 'w', WIN_MOD_CTRL, WIN_ACT_CLOSE },
};
const int win_nbindings = (int)(sizeof(win_bindings) / sizeof(win_bindings[0]));

void
win_input_init(struct win_input *in)
{
	memset(in, 0, sizeof(*in));
}

/* Decoders */

static int
raw_key(int code)
{
	if (code >= 0x20 && code <= 0x7e)
		return code;
	if (code >= RAW_F1 && code < RAW_F1 + 9)
		return WIN_KEY_F1 + (code - RAW_F1);
	switch (code) {
	case LCD_KEY_BACKSPACE:	return WIN_KEY_BACKSPACE;
	case LCD_KEY_TAB:	return WIN_KEY_TAB;
	case LCD_KEY_ENTER:	return WIN_KEY_ENTER;
	case LCD_KEY_ESC:	return WIN_KEY_ESC;
	case LCD_KEY_LEFT:	return WIN_KEY_LEFT;
	case LCD_KEY_UP:	return WIN_KEY_UP;
	case LCD_KEY_DOWN:	return WIN_KEY_DOWN;
	case LCD_KEY_RIGHT:	return WIN_KEY_RIGHT;
	case RAW_INSERT:	return WIN_KEY_INSERT;
	case RAW_HOME:		return WIN_KEY_HOME;
	case RAW_DELETE:	return WIN_KEY_DELETE;
	case RAW_END:		return WIN_KEY_END;
	case RAW_PAGE_UP:	return WIN_KEY_PAGE_UP;
	case RAW_PAGE_DOWN:	return WIN_KEY_PAGE_DOWN;
	case RAW_F10:		return WIN_KEY_F10;
	default:		return 0;
	}
}

static void
put(struct win_event *ev, int key, int mods, int kind)
{
	ev->key = key;
	ev->mods = (unsigned char)mods;
	ev->kind = (unsigned char)kind;
}

int
win_decode_raw(struct win_input *in, int state, int key, struct win_event *ev)
{
	int k, kind;

	key &= 0xff;
	if (key == LCD_KEY_CTRL) {
		if (state == LCD_KEY_RELEASED)
			in->mods &= (unsigned char)~WIN_MOD_CTRL;
		else if (state == LCD_KEY_PRESSED || state == LCD_KEY_HOLD)
			in->mods |= WIN_MOD_CTRL;
		return 0;
	}
	if (state == LCD_KEY_PRESSED)
		kind = WIN_EV_PRESS;
	else if (state == LCD_KEY_HOLD)
		kind = WIN_EV_REPEAT;
	else if (state == LCD_KEY_RELEASED)
		kind = WIN_EV_RELEASE;
	else
		return 0;
	k = raw_key(key);
	if (k == 0)
		return 0;
	if ((in->mods & WIN_MOD_CTRL) != 0 && k >= 'A' && k <= 'Z')
		k += 'a' - 'A';
	put(ev, k, in->mods, kind);
	return 1;
}

void
win_input_lost(struct win_input *in)
{
	in->mods = 0;
	in->held = 0;
}

/* One byte outside an escape sequence; 1 when it is a key. */
static int
tty_plain(int byte, struct win_event *ev)
{
	int k, mods = 0;

	if (byte >= 0x20 && byte <= 0x7e)
		k = byte;
	else if (byte == '\r' || byte == '\n')
		k = WIN_KEY_ENTER;
	else if (byte == '\t')
		k = WIN_KEY_TAB;
	else if (byte == 0x08 || byte == 0x7f)
		k = WIN_KEY_BACKSPACE;
	else if (byte >= 0x01 && byte <= 0x1a) {
		k = 'a' + byte - 1;
		mods = WIN_MOD_CTRL;
	} else
		return 0;
	put(ev, k, mods, WIN_EV_PRESS);
	return 1;
}

/* The key of an SS3 or CSI final byte without parameters, or 0. */
static int
final_key(int byte)
{
	switch (byte) {
	case 'A':	return WIN_KEY_UP;
	case 'B':	return WIN_KEY_DOWN;
	case 'C':	return WIN_KEY_RIGHT;
	case 'D':	return WIN_KEY_LEFT;
	case 'H':	return WIN_KEY_HOME;
	case 'F':	return WIN_KEY_END;
	default:	return 0;
	}
}

static int
tilde_key(int num)
{
	switch (num) {
	case 2:		return WIN_KEY_INSERT;
	case 3:		return WIN_KEY_DELETE;
	case 5:		return WIN_KEY_PAGE_UP;
	case 6:		return WIN_KEY_PAGE_DOWN;
	case 15:	return WIN_KEY_F1 + 4;
	case 17:	return WIN_KEY_F1 + 5;
	case 18:	return WIN_KEY_F1 + 6;
	case 19:	return WIN_KEY_F1 + 7;
	case 20:	return WIN_KEY_F1 + 8;
	case 21:	return WIN_KEY_F10;
	default:	return 0;
	}
}

int
win_decode_byte(struct win_input *in, int byte, struct win_event ev[2])
{
	int k, n = 0;

	byte &= 0xff;
	if (byte == 0x1b) {
		if (in->esc == ST_ESC)
			put(&ev[n++], WIN_KEY_ESC, 0, WIN_EV_PRESS);
		in->esc = ST_ESC;
		return n;
	}
	switch (in->esc) {
	case ST_ESC:
		if (byte == '[') {
			in->esc = ST_CSI;
			in->csi_len = 0;
			in->csi_num = 0;
			return 0;
		}
		if (byte == 'O') {
			in->esc = ST_SS3;
			return 0;
		}
		in->esc = ST_GROUND;
		put(&ev[n++], WIN_KEY_ESC, 0, WIN_EV_PRESS);
		return n + tty_plain(byte, &ev[n]);
	case ST_SS3:
		in->esc = ST_GROUND;
		k = final_key(byte);
		if (k == 0 && byte >= 'P' && byte <= 'S')
			k = WIN_KEY_F1 + (byte - 'P');
		if (k != 0)
			put(&ev[n++], k, 0, WIN_EV_PRESS);
		return n;
	case ST_CSI:
		if (byte >= '0' && byte <= '9') {
			if (in->csi_num >= 0 && in->csi_num <= NUM_CAP)
				in->csi_num = (short)(in->csi_num * 10 + byte - '0');
			if (in->csi_len < 255)
				in->csi_len++;
			return 0;
		}
		if (byte >= 0x20 && byte <= 0x3f) {
			in->csi_num = -1;	/* Another parameter byte. */
			return 0;
		}
		if (byte < 0x40 || byte > 0x7e) {
			in->esc = ST_GROUND;	/* Aborted by a control byte. */
			return 0;
		}
		in->esc = ST_GROUND;
		if (in->csi_num < 0 || in->csi_num > NUM_CAP)
			return 0;
		if (byte == '~')
			k = in->csi_len > 0 ? tilde_key(in->csi_num) : 0;
		else
			k = in->csi_len == 0 ? final_key(byte) : 0;
		if (k != 0)
			put(&ev[n++], k, 0, WIN_EV_PRESS);
		return n;
	default:
		return tty_plain(byte, &ev[0]);
	}
}

int
win_decode_idle(struct win_input *in, struct win_event *ev)
{
	int pending = in->esc;

	in->esc = ST_GROUND;
	if (pending != ST_ESC)
		return 0;
	put(ev, WIN_KEY_ESC, 0, WIN_EV_PRESS);
	return 1;
}

/* Manager */

/* A letter without its case: a Caps Lock release can differ from its press. */
static int
fold(int key)
{
	return key >= 'A' && key <= 'Z' ? key + 'a' - 'A' : key;
}

static int
clampi(int v, int lo, int hi)
{
	if (hi < lo)
		hi = lo;
	return v < lo ? lo : v > hi ? hi : v;
}

static int
shown_count(const struct win_scene *s)
{
	int k, n = 0;

	for (k = 0; k < s->nwin; k++)
		if (!s->wins[s->z[k]].hidden)
			n++;
	return n;
}

/* The lowest (dir 0) or topmost (dir 1) shown window, or NULL. */
static struct win *
shown_end(struct win_scene *s, int dir)
{
	int k;

	for (k = 0; k < s->nwin; k++) {
		struct win *w = &s->wins[s->z[dir ? s->nwin - 1 - k : k]];

		if (!w->hidden)
			return w;
	}
	return NULL;
}

static int
act_cycle(struct win_scene *s, int forward)
{
	struct win *w;

	if (shown_count(s) < 2)
		return 0;
	w = shown_end(s, !forward);
	if ((forward ? win_raise(s, w) : win_lower(s, w)) < 0)
		return -1;
	return win_focus(s, shown_end(s, 1));
}

static int
act_close(struct win_scene *s)
{
	struct win_event ev;
	struct win *w = win_focused(s);

	if (w == NULL)
		return 0;
	put(&ev, 0, 0, WIN_EV_CLOSE);
	if (w->on_event != NULL && w->on_event(s, w, &ev, w->on_event_arg) != 0)
		return 0;
	return win_destroy(s, w);
}

static int
act_mode(struct win_input *in, struct win_scene *s, int mode)
{
	struct win *w = win_focused(s);

	if (w == NULL)
		return 0;
	in->mode = (unsigned char)mode;
	in->mode_win = w;
	in->sx = w->x;
	in->sy = w->y;
	in->sw = w->w;
	in->sh = w->h;
	return 0;
}

/* One arrow key in a mode. */
static int
mode_step(struct win_input *in, struct win_scene *s, int key)
{
	struct win *w = in->mode_win;
	int dx = 0, dy = 0, nx, ny;

	switch (key) {
	case WIN_KEY_LEFT:	dx = -WIN_STEP; break;
	case WIN_KEY_RIGHT:	dx = WIN_STEP; break;
	case WIN_KEY_UP:	dy = -WIN_STEP; break;
	case WIN_KEY_DOWN:	dy = WIN_STEP; break;
	default:		return 0;
	}
	if (in->mode == WIN_MODE_MOVE) {
		nx = clampi(w->x + dx, 0, WIN_PANEL_W - w->w);
		ny = clampi(w->y + dy, 0, WIN_PANEL_H - w->h);
		return win_move(s, w, nx, ny);
	}
	nx = clampi(w->w + dx, WIN_MIN_W, WIN_PANEL_W - w->x);
	ny = clampi(w->h + dy, WIN_MIN_H, WIN_PANEL_H - w->y);
	return win_resize(s, w, nx, ny);
}

/* A key in a mode; every event is consumed. */
static int
mode_event(struct win_input *in, struct win_scene *s, const struct win_event *ev)
{
	if (ev->kind == WIN_EV_RELEASE ||
	    (ev->kind != WIN_EV_PRESS && ev->kind != WIN_EV_REPEAT))
		return 0;
	if (ev->kind == WIN_EV_PRESS && ev->key == WIN_KEY_ENTER) {
		in->mode = WIN_MODE_NONE;
		in->held = WIN_KEY_ENTER;
		return 0;
	}
	if (ev->kind == WIN_EV_PRESS && ev->key == WIN_KEY_ESC) {
		if (win_resize(s, in->mode_win, in->sw, in->sh) < 0 ||
		    win_move(s, in->mode_win, in->sx, in->sy) < 0)
			return -1;
		in->mode = WIN_MODE_NONE;
		in->held = WIN_KEY_ESC;
		return 0;
	}
	return mode_step(in, s, ev->key);
}

static int
run_action(struct win_input *in, struct win_scene *s, int action)
{
	switch (action) {
	case WIN_ACT_NEXT:	return act_cycle(s, 1);
	case WIN_ACT_PREV:	return act_cycle(s, 0);
	case WIN_ACT_MOVE:	return act_mode(in, s, WIN_MODE_MOVE);
	case WIN_ACT_RESIZE:	return act_mode(in, s, WIN_MODE_RESIZE);
	default:		return act_close(s);
	}
}

int
win_input_event(struct win_input *in, struct win_scene *s,
    const struct win_event *ev)
{
	struct win *w;
	int i;

	if (in->mode != WIN_MODE_NONE && !win_is_live(s, in->mode_win))
		in->mode = WIN_MODE_NONE;
	if (in->mode != WIN_MODE_NONE)
		(void)mode_event(in, s, ev);
	if (in->held != 0 && fold(ev->key) == fold(in->held) &&
	    ev->kind != WIN_EV_PRESS) {
		if (ev->kind == WIN_EV_RELEASE)
			in->held = 0;
		return WIN_DISP_MANAGER;
	}
	if (ev->kind == WIN_EV_PRESS) {
		for (i = 0; i < win_nbindings; i++) {
			if (win_bindings[i].key != ev->key ||
			    win_bindings[i].mods != (ev->mods & WIN_MOD_CTRL))
				continue;
			if (run_action(in, s, win_bindings[i].action) < 0)
				return -1;
			in->held = (short)ev->key;
			return WIN_DISP_MANAGER;
		}
	}
	w = win_focused(s);
	if (w == NULL || w->on_event == NULL)
		return WIN_DISP_DROPPED;
	return w->on_event(s, w, ev, w->on_event_arg) != 0 ?
	    WIN_DISP_WINDOW : WIN_DISP_DROPPED;
}

int
win_input_raw(struct win_input *in, struct win_scene *s, int state, int key)
{
	struct win_event ev;

	if (win_decode_raw(in, state, key, &ev) == 0)
		return 0;
	return win_input_event(in, s, &ev) < 0 ? -1 : 0;
}

static int
feed(struct win_input *in, struct win_scene *s, const struct win_event *ev,
    int n)
{
	int i, rc = 0;

	for (i = 0; i < n; i++)
		if (win_input_event(in, s, &ev[i]) < 0)
			rc = -1;
	return rc;
}

int
win_input_byte(struct win_input *in, struct win_scene *s, int byte)
{
	struct win_event ev[2];

	return feed(in, s, ev, win_decode_byte(in, byte, ev));
}

int
win_input_idle(struct win_input *in, struct win_scene *s)
{
	struct win_event ev;

	return feed(in, s, &ev, win_decode_idle(in, &ev));
}
