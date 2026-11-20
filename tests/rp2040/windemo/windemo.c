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
 * windemo: on-device demonstration of lib/libwin on the PicoCalc panel.
 *
 * Opens /dev/lcdgfx, opens three overlapping windows and moves the top one
 * across the other two in 8-pixel steps, flushing after each step, then
 * hands the keyboard to the window manager of lib/libwin until Ctrl-Q or
 * until every window is closed, and restores the console by closing the
 * device. Keys, with the bindings of win_input.h: F6 and F7 (or Ctrl-Tab and
 * Ctrl-Backspace) switch windows, F8 moves the focused window and F9 resizes
 * it with the arrows (Enter accepts, Esc cancels), F10 or Ctrl-W closes it.
 * Every other key is shown in the focused window. Keys arrive as raw
 * events when the console grants a session (LCDIOC_KEYEVS), otherwise as tty
 * bytes. A board run is opt-in: it takes over the panel and the keyboard
 * until the program exits. Prints "WINDEMO OK" when every call succeeds.
 *
 * Regression tool, not shipped in the root manifest; stage it with a "file
 * /usr/bin/windemo" line in distrib/rp2040/mi.rp2040 for a test image or
 * copy it to the board over the console. Not run on a board.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <fcntl.h>
#include <sgtty.h>
#include <stdio.h>
#include <unistd.h>

#include "win_input.h"

#define DEVICE		"/dev/lcdgfx"
#define STEP		8
#define NSTEPS		20
#define IDLE_MS		60000		/* Leave after a minute without a key. */

struct pane {
	const char *label;
	char last[16];			/* The last key, shown in the client area. */
};

static int quit;

static void
paint_label(struct win_ctx *c, void *arg)
{
	const struct pane *p = arg;

	win_fill(c, 0, 0, c->w, 12, 1);
	win_hline(c, 0, 12, c->w, 15);
	win_text(c, 2, 2, p->label, 15);
	win_vline(c, c->w - 3, 14, c->h - 14, 2);
	win_text(c, 2, 20, p->last, 0);
}

static void
name_key(char *out, const struct win_event *ev)
{
	static const char kinds[] = " +*-";
	int k = ev->key, n = 0;

	out[n++] = kinds[ev->kind <= WIN_EV_RELEASE ? ev->kind : 0];
	if ((ev->mods & WIN_MOD_CTRL) != 0)
		out[n++] = '^';
	if (k >= 0x20 && k <= 0x7e) {
		out[n++] = (char)k;
	} else {
		/* A special key shows as '#' and its code's low digit. */
		out[n++] = '#';
		out[n++] = (char)('0' + (k & 0x0f) % 10);
	}
	out[n] = '\0';
}

static int
on_event(struct win_scene *s, struct win *w, const struct win_event *ev,
    void *arg)
{
	struct pane *p = arg;

	if (ev->kind == WIN_EV_CLOSE)
		return 0;
	if (ev->kind == WIN_EV_PRESS && ev->key == 'q' &&
	    (ev->mods & WIN_MOD_CTRL) != 0) {
		quit = 1;
		return 1;
	}
	name_key(p->last, ev);
	(void)win_invalidate_client(s, w, 0, 20, 100, 10);
	return 1;
}

int
main(void)
{
	/*
	 * The scene is a few KB and a process starts with a 2,048-byte stack
	 * (SSIZE in machine/machparam.h), so it has static storage.
	 */
	static struct win_scene scene;
	static struct win_input in;
	static struct pane panes[3] = {
		{ "alpha", "" }, { "beta", "" }, { "gamma", "" }
	};
	struct win_input_src src;
	struct win_backend backend;
	struct win_lcdgfx lcd;
	struct sgttyb saved, raw;
	struct win *a, *b, *c;
	int fd, i, status = 0, off = 0, restore = 0;

	fd = open(DEVICE, O_WRONLY);
	if (fd < 0) {
		printf("windemo: cannot open %s\n", DEVICE);
		return 1;
	}
	win_lcdgfx_init(&backend, &lcd, fd);
	if (win_scene_init(&scene, &backend) != 0)
		return 1;
	a = win_create(&scene, 20, 20, 140, 100, "first", paint_label, &panes[0]);
	b = win_create(&scene, 100, 60, 150, 110, "second", paint_label, &panes[1]);
	c = win_create(&scene, 60, 90, 100, 60, "third", paint_label, &panes[2]);
	if (a == NULL || b == NULL || c == NULL || win_flush(&scene) != 0) {
		close(fd);
		printf("windemo: setup failed\n");
		return 1;
	}
	for (i = 1; i <= NSTEPS; i++) {
		if (win_move(&scene, c, 60 + i * STEP, 90 + i) != 0 ||
		    win_flush(&scene) != 0) {
			close(fd);
			printf("windemo: step %d failed\n", i);
			return 1;
		}
	}
	if (win_raise(&scene, a) != 0 || win_flush(&scene) != 0) {
		close(fd);
		printf("windemo: raise failed\n");
		return 1;
	}

	/* Keys: standard input is the console, in RAW mode without echo. */
	win_input_init(&in);
	(void)win_set_handler(&scene, a, on_event, &panes[0]);
	(void)win_set_handler(&scene, b, on_event, &panes[1]);
	(void)win_set_handler(&scene, c, on_event, &panes[2]);
	if (ioctl(0, TIOCGETP, &saved) < 0) {
		close(fd);
		printf("windemo: standard input is not a terminal\n");
		return 1;
	}
	raw = saved;
	raw.sg_flags |= RAW;
	raw.sg_flags &= ~ECHO;
	if (ioctl(0, TIOCSETP, &raw) < 0) {
		close(fd);
		return 1;
	}
	restore = 1;
	/* A refused session leaves the tty byte source, which needs no setup. */
	(void)win_input_open(&src, 0);
	while (!quit && scene.nwin > 0) {
		int n = win_input_wait(&src, &in, &scene, IDLE_MS);

		if (n < 0 && src.raw) {
			status = 1;
			break;
		}
		if (n < 0) {
			/* The session ended under us; continue on tty bytes. */
			win_input_init(&in);
			continue;
		}
		if (n == 0)
			break;
		if (win_flush(&scene) != 0) {
			status = 1;
			break;
		}
	}
	win_input_close(&src);
	if (restore) {
		(void)ioctl(0, TIOCSETP, &saved);
		(void)ioctl(0, TIOCFLUSH, &off);
	}
	close(fd);
	if (status != 0) {
		printf("windemo: input or flush failed\n");
		return 1;
	}
	printf("WINDEMO OK\n");
	return 0;
}
