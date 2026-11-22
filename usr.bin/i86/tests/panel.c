/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * A model of /dev/lcdgfx (sys/lcdgfx.h) for the host test: the part of the
 * interface gfx.c uses, as strict as the kernel about it and stricter
 * where a correct program never tests the kernel's patience. It counts as
 * an error a command the device would reject, any command the model does
 * not know, a window left with pixels missing, a write with no window or
 * beyond its last pixel, and a palette index above 15 in a pixel byte.
 * A write takes at most 256 bytes, so the caller's loop over short writes
 * runs.
 *
 * The palette starts as one color no display mode uses, so a pixel drawn
 * before the palette was set shows, and the frame starts as another: the
 * superuser's open leaves the console's screen in the panel's memory, and
 * this stands for it. Every close writes the 320 by 320
 * frame the glass shows, through the scroll area and start that
 * LCDG_SCRDEF and LCDG_SCROLL set in the controller's 480 lines, as a binary PPM and the counters as text beside it, to the paths
 * I86_PANEL names with the number of the close appended. I86_PANEL_FAIL
 * makes opens fail: "open" every one, "reopen" every one after the first;
 * "read" makes LCDG_READ fail as a kernel without LCD_GFX_EXT does.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int panel_open(void);
int panel_close(void);
int panel_ioctl(unsigned long cmd, void *arg);
long panel_write(const void *p, unsigned long n);
long panel_read(void *p, unsigned long n);

static uint16_t fb[LCDG_RAM_LINES][LCDG_WIDTH];	/* the controller's memory */
static unsigned scr_top, scr_bottom, scr_start;	/* VSCRDEF and VSCSAD */
static struct lcdg_read rd;			/* LCDG_READ's rectangle */
static unsigned long rd_at, rd_total;		/* pixels */
static int hasrd;
static uint16_t pal[LCDG_NPAL];
static struct lcdg_window win;
static unsigned long win_at, win_total;		/* pixels */
static int isopen, haswin;
static unsigned opens, closes;
static unsigned long errors, pixels, windows, fills, filled, tallest, scrolls;
static unsigned colmod;

static int
bad(int e)
{
	errors++;
	errno = e;
	return (-1);
}

int
panel_open(void)
{
	const char *fail = getenv("I86_PANEL_FAIL");
	unsigned i;

	if (fail != NULL && (strcmp(fail, "open") == 0 ||
	    (strcmp(fail, "reopen") == 0 && opens > 0))) {
		errno = opens ? EBUSY : ENXIO;
		return (-1);
	}
	if (isopen)
		return (bad(EBUSY));
	isopen = 1;
	opens++;
	haswin = 0;
	colmod = LCDG_COLMOD_RGB666;
	for (i = 0; i < LCDG_WIDTH * LCDG_RAM_LINES; i++)
		fb[i / LCDG_WIDTH][i % LCDG_WIDTH] = 0x8410;
	scr_top = 0;
	scr_bottom = LCDG_RAM_LINES - LCDG_HEIGHT;
	scr_start = 0;
	for (i = 0; i < LCDG_NPAL; i++)
		pal[i] = 0xf81f;
	return (3);
}

/* The memory line the glass shows on its line y: the controller's
 * VSCRDEF and VSCSAD as tools/renode/picocalc/st7365p.cs's SourceLine
 * decodes them from the datasheet. */
static unsigned
glass(unsigned y)
{
	unsigned area = LCDG_RAM_LINES - scr_top - scr_bottom, off;

	if (y < scr_top || y >= scr_top + area)
		return (y);
	off = scr_start - scr_top;
	return (scr_top + (y - scr_top + off) % area);
}

int
panel_close(void)
{
	const char *base = getenv("I86_PANEL");
	char path[512];
	unsigned x, y;
	FILE *f;

	if (!isopen)
		return (bad(EBADF));
	if (haswin && win_at < win_total)
		errors++;
	isopen = 0;
	closes++;
	if (base == NULL)
		return (0);
	snprintf(path, sizeof(path), "%s.%u.ppm", base, closes);
	if ((f = fopen(path, "wb")) == NULL)
		return (0);
	fprintf(f, "P6\n%d %d\n255\n", LCDG_WIDTH, LCDG_HEIGHT);
	for (y = 0; y < LCDG_HEIGHT; y++)
		for (x = 0; x < LCDG_WIDTH; x++) {
			unsigned v = fb[glass(y)][x], r = v >> 11, g = v >> 5 & 63;
			unsigned b = v & 31;

			fputc((int)(r << 3 | r >> 2), f);
			fputc((int)(g << 2 | g >> 4), f);
			fputc((int)(b << 3 | b >> 2), f);
		}
	fclose(f);
	snprintf(path, sizeof(path), "%s.%u.txt", base, closes);
	if ((f = fopen(path, "w")) == NULL)
		return (0);
	fprintf(f, "errors %lu\npixels %lu\nwindows %lu\nfills %lu\n"
	    "colmod %x\ntallest %lu\nfilled %lu\nscrolls %lu\n", errors, pixels,
	    windows, fills, colmod, tallest, filled, scrolls);
	fclose(f);
	return (0);
}

int
panel_ioctl(unsigned long cmd, void *arg)
{
	const struct lcdg_rect *r = arg;
	const struct lcdg_window *w = arg;
	const struct lcdg_palette *p = arg;
	const struct lcdg_colmod *cm = arg;
	const struct lcdg_scroll *sc = arg;
	const struct lcdg_scrdef *sd = arg;
	const struct lcdg_read *r2 = arg;
	unsigned x, y;

	if (!isopen)
		return (bad(EBADF));
	/* Any command abandons a window that still wants pixels. */
	if (haswin && win_at < win_total)
		errors++;
	haswin = 0;
	hasrd = 0;
	switch (cmd) {
	case LCDG_READ:
		/* A kernel without LCD_GFX_EXT does not know the command. */
		if (getenv("I86_PANEL_FAIL") != NULL &&
		    strcmp(getenv("I86_PANEL_FAIL"), "read") == 0) {
			errno = ENOTTY;
			return (-1);
		}
		/* The kernel's bounds; the memory of all 480 lines. */
		if (r2->w < 1 || r2->h < 1 || r2->x + r2->w > LCDG_WIDTH ||
		    r2->y + r2->h > LCDG_RAM_LINES || r2->fmt != LCDG_RD_RGB565)
			return (bad(EINVAL));
		rd = *r2;
		rd_at = 0;
		rd_total = (unsigned long)r2->w * r2->h;
		hasrd = 1;
		return (0);
	case LCDG_FILL:
		if (r->x0 > r->x1 || r->y0 > r->y1 || r->x1 >= LCDG_WIDTH ||
		    r->y1 >= LCDG_HEIGHT)
			return (bad(EINVAL));
		for (y = r->y0; y <= r->y1; y++)
			for (x = r->x0; x <= r->x1; x++)
				fb[y][x] = r->color;
		fills++;
		filled += (unsigned long)(r->x1 - r->x0 + 1) * (r->y1 - r->y0 + 1);
		return (0);
	case LCDG_WINDOW:
		if (w->w < 1 || w->h < 1 || w->x + w->w > LCDG_WIDTH ||
		    w->y + w->h > LCDG_HEIGHT || w->fmt > LCDG_WIN_BITS ||
		    (w->fmt == LCDG_WIN_BITS &&
		    (w->fg >= LCDG_NPAL || w->bg >= LCDG_NPAL)))
			return (bad(EINVAL));
		win = *w;
		if (w->fmt == LCDG_WIN_INDEX && w->h > tallest)
			tallest = w->h;
		win_at = 0;
		win_total = (unsigned long)w->w * w->h;
		haswin = 1;
		windows++;
		return (0);
	case LCDG_PALETTE:
		if (p->index >= LCDG_NPAL)
			return (bad(EINVAL));
		pal[p->index] = (uint16_t)((p->r >> 3) << 11 |
		    (p->g >> 2) << 5 | p->b >> 3);
		return (0);
	case LCDG_SCRDEF:
		/* The kernel's bounds (sys/arch/rp2040/dev/lcdgfx.c). */
		if (sd->top + sd->bottom >= LCDG_RAM_LINES)
			return (bad(EINVAL));
		scr_top = sd->top;
		scr_bottom = sd->bottom;
		scr_start = scr_top;
		return (0);
	case LCDG_SCROLL:
		if (sc->line < scr_top ||
		    sc->line >= LCDG_RAM_LINES - scr_bottom)
			return (bad(EINVAL));
		scr_start = sc->line;
		scrolls++;
		return (0);
	case LCDG_COLMOD:
		if (cm->fmt != LCDG_COLMOD_RGB666 &&
		    cm->fmt != LCDG_COLMOD_RGB565)
			return (bad(EINVAL));
		colmod = cm->fmt;
		return (0);
	default:
		return (bad(ENOTTY));
	}
}

static void
pixel(unsigned v)
{
	fb[win.y + win_at / win.w][win.x + win_at % win.w] = (uint16_t)v;
	win_at++;
	pixels++;
}

/* The pixels of the LCDG_READ rectangle, RGB565 low byte first. */
long
panel_read(void *buf, unsigned long n)
{
	uint8_t *p = buf;
	unsigned long i;
	unsigned v;

	if (!isopen || (n & 1))
		return (bad(EINVAL));
	if (!hasrd)
		return (0);
	for (i = 0; i + 1 < n && rd_at < rd_total; i += 2, rd_at++) {
		v = fb[rd.y + rd_at / rd.w][rd.x + rd_at % rd.w];
		p[i] = (uint8_t)v;
		p[i + 1] = (uint8_t)(v >> 8);
	}
	return ((long)i);
}

long
panel_write(const void *buf, unsigned long n)
{
	const uint8_t *p = buf;
	unsigned long i, used = 0;
	unsigned bit;

	if (!isopen)
		return (bad(EBADF));
	if (!haswin)
		return (bad(EINVAL));
	if (n > 256)
		n = 256;
	switch (win.fmt) {
	case LCDG_WIN_INDEX:
		for (i = 0; i < n && win_at < win_total; i++, used++) {
			if (p[i] >= LCDG_NPAL)
				errors++;
			pixel(pal[p[i] & 15]);
		}
		break;
	case LCDG_WIN_BITS:
		/* A row ends on a byte boundary; the caller sends whole
		 * rows' bytes, so a byte never spans two calls' rows. */
		for (i = 0; i < n && win_at < win_total; i++, used++)
			for (bit = 0; bit < 8; bit++) {
				pixel(pal[(p[i] >> (7 - bit) & 1) ? win.fg :
				    win.bg]);
				if (win_at % win.w == 0)
					break;
			}
		break;
	default:
		if (n & 1)
			return (bad(EINVAL));
		for (i = 0; i + 1 < n && win_at < win_total; i += 2, used += 2)
			pixel(p[i] | (unsigned)p[i + 1] << 8);
		break;
	}
	if (used < n)
		errors++;		/* bytes beyond the last pixel */
	return ((long)used);
}
