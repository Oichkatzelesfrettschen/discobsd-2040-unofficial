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
 * libwin input: key events, two decoders, window-management keys and
 * dispatch. This layer draws nothing itself; it calls the scene functions of
 * win.h and delivers events to the focused window's handler.
 *
 * Event. A key is an ASCII character from 0x20 to 0x7e, or one of the
 * WIN_KEY_* codes (all above 0xff, so no code collides with a character).
 * kind is WIN_EV_PRESS, WIN_EV_REPEAT or WIN_EV_RELEASE, or WIN_EV_CLOSE (a
 * close request, key 0). mods holds WIN_MOD_CTRL while Ctrl is down; Shift,
 * Caps Lock and the symbol layer are applied by the keyboard controller
 * before the key byte arrives (sys/arch/rp2040/doc/PICOCALC.md), and Ctrl
 * is the only modifier key the tree names (KEY_MOD_CTRL, picokbd_map.c), so
 * the table holds it alone. With Ctrl down a letter is delivered in lower
 * case.
 *
 * Source 1, raw key events (LCDIOC_GETKEY, sys/sys/lcdcons.h): win_decode_raw()
 * takes the controller's state and key byte. PRESSED, HOLD and RELEASED map
 * to press, repeat and release as reported: repeat comes from the
 * controller's HOLD events, a PRESSED event is always a press, and the
 * toolkit runs no repeat timer. A Ctrl event updates the modifier table and
 * yields no event. win_input_lost() clears the table and the swallowed key
 * when the kernel reports dropped events; a held Ctrl re-asserts itself with
 * its next HOLD.
 *
 * Source 2, tty bytes (the fallback when no raw session is available):
 * win_decode_byte() decodes the bytes the console sends, as picokbd_map.c
 * builds them: arrows and Home and End as ESC [ x or ESC O x, Insert,
 * Delete, Page Up and Down and F5-F10 as ESC [ n ~, F1-F4 as ESC O P-S,
 * Ctrl-letters as 0x01-0x1a. This source has presses only, no release and
 * no repeat. An ESC byte stays pending until the next byte or
 * win_decode_idle(), which the caller invokes when its wait times out with
 * no byte: a pending lone ESC then becomes the Esc key, and a pending
 * partial sequence is dropped. A sequence that is not in the table, one
 * with a malformed parameter and a sequence cut by another ESC produce no
 * key.
 *
 * Window-management keys. win_input_event() offers every event to the
 * binding table (win_bindings) before the application. The table names the
 * keys the PicoCalc keyboard can produce on both sources: F6-F10 need no
 * modifier on either, and Ctrl-W is a plain Ctrl-letter byte on the tty.
 * Ctrl-Tab and Ctrl-Backspace exist only as raw events (on the tty they are
 * Tab and DEL), so they are aliases of F6 and F7. Ctrl-M, Ctrl-I, Ctrl-H and
 * Ctrl-[ are Enter, Tab, Backspace and Esc on the tty, so no letter binding
 * uses them.
 *
 *	F6, Ctrl-Tab		next window: raise the lowest shown window
 *	F7, Ctrl-Backspace	previous window: lower the top shown window
 *	F8			move mode
 *	F9			resize mode
 *	F10, Ctrl-W		close request to the focused window
 *
 * In move or resize mode the arrows change the focused window by
 * WIN_STEP pixels, clamped so the window stays on the panel (a move keeps
 * x and y in 0 .. panel - size; a resize keeps the size from the minimum to
 * the panel edge). Enter accepts and Esc restores the geometry the window
 * had when the mode began. Every event is consumed while a mode is active.
 * A binding acts on a press only; the repeat and release of a key the
 * manager consumed are consumed too, so a held Ctrl-W closes one window.
 * The close request is delivered as a WIN_EV_CLOSE event to the window's
 * handler; a window with no handler, or a handler that returns 0, is
 * destroyed.
 *
 * Dispatch. An event the manager does not consume goes to the focused
 * window's handler (win_set_handler()); with no focused window it is
 * dropped. A handler returns nonzero when it consumed the event and -1 when
 * a scene call failed at the backend; win_input_event() then returns -1
 * under the failure contract below. The return value of win_input_event()
 * names who took the event.
 *
 * Failure contract. When a scene call fails at the backend, win_input_event()
 * and the helpers built on it return -1 at once. The scene, the focus, the
 * geometry of every window and the active mode are exactly as before the
 * event (cancel restores position and size in one win_set_geometry() call),
 * and the failed event is not applied: the caller may send the same key
 * again. A helper that decodes several events (win_input_byte() with an ESC
 * before an ordinary byte) discards the events after the failed one. A mode
 * ends cleanly, keeping the geometry, when its window is destroyed or its
 * slot is reused by a new window (compared by creation generation).
 */
#ifndef _WIN_INPUT_H_
#define _WIN_INPUT_H_

#include "win.h"

#define WIN_EV_PRESS	1
#define WIN_EV_REPEAT	2
#define WIN_EV_RELEASE	3
#define WIN_EV_CLOSE	4

#define WIN_MOD_CTRL	1

#define WIN_KEY_BACKSPACE	0x101
#define WIN_KEY_TAB		0x102
#define WIN_KEY_ENTER		0x103
#define WIN_KEY_ESC		0x104
#define WIN_KEY_LEFT		0x105
#define WIN_KEY_UP		0x106
#define WIN_KEY_DOWN		0x107
#define WIN_KEY_RIGHT		0x108
#define WIN_KEY_HOME		0x109
#define WIN_KEY_END		0x10a
#define WIN_KEY_INSERT		0x10b
#define WIN_KEY_DELETE		0x10c
#define WIN_KEY_PAGE_UP		0x10d
#define WIN_KEY_PAGE_DOWN	0x10e
#define WIN_KEY_F1		0x111	/* F1-F10 are WIN_KEY_F1 + 0-9. */
#define WIN_KEY_F10		0x11a

#define WIN_STEP	8		/* Pixels per move or resize key. */

#define WIN_MODE_NONE	0
#define WIN_MODE_MOVE	1
#define WIN_MODE_RESIZE	2

/* win_input_event() results. */
#define WIN_DISP_DROPPED	0	/* No consumer. */
#define WIN_DISP_MANAGER	1	/* The binding table or a mode took it. */
#define WIN_DISP_WINDOW		2	/* The focused window's handler took it. */

struct win_event {
	int key;
	unsigned char mods;
	unsigned char kind;
};

#define WIN_ACT_NEXT	1
#define WIN_ACT_PREV	2
#define WIN_ACT_MOVE	3
#define WIN_ACT_RESIZE	4
#define WIN_ACT_CLOSE	5

struct win_binding {
	short key;
	unsigned char mods;		/* Exact WIN_MOD_CTRL state required. */
	unsigned char action;
};

extern const struct win_binding win_bindings[];
extern const int win_nbindings;

struct win_input {
	unsigned char mods;		/* Modifier table (raw source). */
	unsigned char esc;		/* Escape decoder state (tty source). */
	unsigned char mode;		/* WIN_MODE_*. */
	unsigned char csi_len;		/* Parameter bytes seen in a CSI. */
	short csi_num;			/* Their value, or -1 when malformed. */
	short held;			/* Key whose press the manager took, or 0. */
	struct win *mode_win;
	unsigned mode_gen;		/* Generation of mode_win at mode entry. */
	int sx, sy, sw, sh;		/* Geometry when the mode began. */
	struct win *modal;		/* Window that holds the focus, or NULL. */
	unsigned modal_gen;		/* Generation of modal at win_input_set_modal(). */
};

void win_input_init(struct win_input *in);

/*
 * Make win modal (NULL ends it): the window-cycle keys F6, F7 and Ctrl-Tab
 * are consumed without moving the focus while win is live, so no key leaves
 * the window the toolkit's dialog (win_widget.h) opened. The mode ends by
 * itself when the window is destroyed or its slot is reused.
 */
void win_input_set_modal(struct win_input *in, struct win *win);

/* Decoders. They return the number of events written. */
int win_decode_raw(struct win_input *in, int state, int key,
    struct win_event *ev);
int win_decode_byte(struct win_input *in, int byte, struct win_event ev[2]);
int win_decode_idle(struct win_input *in, struct win_event *ev);
void win_input_lost(struct win_input *in);

/* Manager and dispatch for one event; -1 when a scene call failed. */
int win_input_event(struct win_input *in, struct win_scene *s,
    const struct win_event *ev);

/* Decode and dispatch; they return 0, or -1 when an event failed. */
int win_input_raw(struct win_input *in, struct win_scene *s, int state,
    int key);
int win_input_byte(struct win_input *in, struct win_scene *s, int byte);
int win_input_idle(struct win_input *in, struct win_scene *s);

/*
 * The descriptor side (win_input_dev.c). The input descriptor is the
 * console tty, open for reading in the foreground process group; raw
 * events additionally need the tty in RAW or CBREAK mode without echo and
 * an active LCDIOC_KEYEVS session.
 */
struct win_input_src {
	int fd;
	int raw;			/* Nonzero while a raw session is active. */
};

/* Start a raw session on fd. Returns 0, or -1 with the tty source selected. */
int win_input_open(struct win_input_src *src, int fd);
void win_input_close(struct win_input_src *src);

/*
 * Wait up to timeout_ms (or without limit when negative) for input and
 * feed everything that arrived to the manager. Raw sessions poll the
 * event queue every WIN_POLL_MS because select(2) wakes on tty bytes only.
 * Returns the number of events and bytes fed, 0 on a timeout, -1 on error;
 * the call ends within timeout_ms plus WIN_POLL_MS.
 * A dispatch that fails at the backend returns -1 at once with the scene as
 * before it (see the failure contract above); the failed event and the rest
 * of its read batch are discarded, and the modifier table and the swallowed
 * key are cleared (win_input_lost()) because releases may be among the
 * discarded events. The caller resends the key or continues.
 */
#define WIN_POLL_MS	20
int win_input_wait(struct win_input_src *src, struct win_input *in,
    struct win_scene *s, int timeout_ms);

#endif /* !_WIN_INPUT_H_ */
