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
 * libwin backend for /dev/lcdgfx (sys/sys/lcdgfx.h): each rectangle is one
 * LCDG_WINDOW in LCDG_WIN_INDEX format followed by write(2) calls of one
 * byte per pixel. The palette is the device's, set with LCDG_PALETTE; the
 * device starts each open with the console's sixteen colors.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <unistd.h>

#include "win.h"

static int
lcdgfx_rect(void *self, int x, int y, int w, int h, const unsigned char *px)
{
	const struct win_lcdgfx *st = self;
	struct lcdg_window win;
	size_t left;
	ssize_t n;

	if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > LCDG_WIDTH ||
	    y + h > LCDG_HEIGHT)
		return -1;
	win.x = (u_short)x;
	win.y = (u_short)y;
	win.w = (u_short)w;
	win.h = (u_short)h;
	win.fmt = LCDG_WIN_INDEX;
	win.fg = 0;
	win.bg = 0;
	if (ioctl(st->fd, LCDG_WINDOW, &win) < 0)
		return -1;
	left = (size_t)w * (size_t)h;
	while (left > 0) {
		n = write(st->fd, px, left);
		if (n <= 0)
			return -1;
		px += n;
		left -= (size_t)n;
	}
	return 0;
}

/* A two-color rectangle: LCDG_WIN_BITS rows with the two palette entries. */
int
win_lcdgfx_bits(void *self, int x, int y, int w, int h, int fg, int bg,
    const unsigned char *rows)
{
	const struct win_lcdgfx *st = self;
	struct lcdg_window win;
	size_t left;
	ssize_t n;

	if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > LCDG_WIDTH ||
	    y + h > LCDG_HEIGHT || fg < 0 || fg >= LCDG_NPAL || bg < 0 ||
	    bg >= LCDG_NPAL)
		return -1;
	win.x = (u_short)x;
	win.y = (u_short)y;
	win.w = (u_short)w;
	win.h = (u_short)h;
	win.fmt = LCDG_WIN_BITS;
	win.fg = (u_char)fg;
	win.bg = (u_char)bg;
	if (ioctl(st->fd, LCDG_WINDOW, &win) < 0)
		return -1;
	left = (size_t)((w + 7) / 8) * (size_t)h;
	while (left > 0) {
		n = write(st->fd, rows, left);
		if (n <= 0)
			return -1;
		rows += n;
		left -= (size_t)n;
	}
	return 0;
}

static int
lcdgfx_palette(void *self, int index, int r, int g, int b)
{
	const struct win_lcdgfx *st = self;
	struct lcdg_palette p;

	p.index = (u_char)index;
	p.r = (u_char)r;
	p.g = (u_char)g;
	p.b = (u_char)b;
	return ioctl(st->fd, LCDG_PALETTE, &p) < 0 ? -1 : 0;
}

void
win_lcdgfx_init(struct win_backend *be, struct win_lcdgfx *st, int fd)
{
	st->fd = fd;
	be->rect = lcdgfx_rect;
	be->palette = lcdgfx_palette;
	be->self = st;
}
