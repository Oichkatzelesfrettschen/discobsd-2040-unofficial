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
 * gfx.c's owed graphics lines, driven directly: gfx.c is linked against a
 * recording stand-in for /dev/lcdgfx and against stand-ins for the parts
 * of mem.c it calls, so each refresh and each drain is one call here and
 * every window it opens is in the record. The checks: a refresh only marks
 * lines; a drain sends at most GFX_DRAIN lines, a run of at most GFX_DRAIN
 * a window; a full redraw goes out in order, each line once; and lines
 * written again on every poll near the top leave the rest of the frame
 * served within one pass of the drain. With page frames for the copy of
 * the lines last sent, a line equal to its copy costs nothing, a changed
 * byte costs a window of its four pixels, and two changed lines share a
 * window only where that costs fewer bytes.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "mem.h"

#define LINES		200
#define GFX_Y0		60
#define DRAIN		16

int panel_open(void);
int panel_close(void);
int panel_ioctl(unsigned long cmd, void *arg);
long panel_write(const void *p, unsigned long n);

uintptr_t mem_rmap[MEM_NPAGE], mem_wmap[MEM_NPAGE];
unsigned mem_watched;
static uint8_t vmem[8192], lent[24 * 1024];
static unsigned sent[LINES];		/* windows that drew each line */
static unsigned drawn, tallest, fails, checks;
static unsigned lend_max = 8;		/* frames the stand-in pager lends */
static struct lcdg_window wins[8];	/* the first windows since a reset */
static unsigned nwins;

unsigned
mem_rslow(uint32_t a)
{
	(void)a;
	return (0);
}

void
mem_wslow(uint32_t a, unsigned v)
{
	(void)a;
	(void)v;
}

void
mem_watch(uint32_t base, unsigned first, uint8_t *p, unsigned n)
{
	(void)base;
	(void)first;
	(void)p;
	(void)n;
}

unsigned
mem_rearm(void)
{
	unsigned m = mem_watched;

	mem_watched = 0;
	return (m);
}

uint8_t *
mem_lend(unsigned n)
{
	return (n <= lend_max ? lent : NULL);
}

void
mem_return(void)
{
}

int
panel_open(void)
{
	return (3);
}

int
panel_close(void)
{
	return (0);
}

int
panel_ioctl(unsigned long cmd, void *arg)
{
	const struct lcdg_window *w = arg;
	unsigned y, top;

	if (cmd == LCDG_WINDOW && w->fmt == LCDG_WIN_INDEX) {
		top = (unsigned)w->y - GFX_Y0;
		for (y = top; y < top + w->h; y++)
			sent[y]++;
		drawn += w->h;
		if (w->h > tallest)
			tallest = w->h;
		if (nwins < 8)
			wins[nwins] = *w;
		nwins++;
	}
	return (0);
}

long
panel_write(const void *p, unsigned long n)
{
	(void)p;
	return ((long)n);
}

static void
check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

static int
all_sent(unsigned times)
{
	unsigned y;

	for (y = 0; y < LINES; y++)
		if (times ? sent[y] != times : sent[y] == 0)
			return (0);
	return (1);
}

int
main(void)
{
	unsigned i;

	gfx_init(vmem);
	check(gfx_open() == 0, "the stand-in device opens");
	check(gfx_mode(4) == 1, "mode 4 is a graphics mode");

	/* A mode set: the refresh marks all 200 lines and leaves them owed. */
	gfx_refresh(40, GFX_NOCURSOR);
	check(drawn == 0, "a refresh only marks lines");

	/* Thirteen drains send them, 16 at a time, each line once. */
	for (i = 0; i < 13; i++) {
		unsigned before = drawn;

		gfx_drain();
		check(drawn - before == (i < 12 ? DRAIN : LINES - 12 * DRAIN),
		    "a drain sends at most 16 lines");
	}
	check(all_sent(1), "a full redraw sends every line once");
	check(tallest == DRAIN, "a window holds at most 16 lines");
	gfx_drain();
	check(drawn == LINES, "nothing is owed after the redraw");

	/* Every line again, then the first page of the even field written
	 * before every poll: one pass of the drain still reaches line 199. */
	memset(sent, 0, sizeof(sent));
	mem_watched = 0xffff;
	gfx_refresh(40, GFX_NOCURSOR);
	for (i = 0; i < 14; i++) {
		mem_watched = 1;
		gfx_refresh(40, GFX_NOCURSOR);
		gfx_drain();
	}
	check(all_sent(0), "lines written on every poll starve no other line");

	/* The same with the copy: after one full redraw, unchanged lines
	 * cost nothing; then one byte on line 0 and one on line 1 three
	 * bytes apart go as two windows of four pixels (one window of both
	 * would send 64 bytes against 27), and one byte at the same place
	 * on lines 4 and 5 as one window of two lines. */
	(void)gfx_mode(3);
	lend_max = 24;
	check(gfx_mode(4) == 1, "mode 4 with frames for the copy");
	gfx_refresh(40, GFX_NOCURSOR);
	for (i = 0; i < 14; i++)
		gfx_drain();
	drawn = nwins = 0;
	mem_watched = 0xffff;
	gfx_refresh(40, GFX_NOCURSOR);
	for (i = 0; i < 14; i++)
		gfx_drain();
	check(drawn == 0, "lines equal to their copy cost nothing");
	vmem[0] = 1;
	lent[3] = 1;
	vmem[2 * 80 + 10] = 1;
	lent[2 * 80 + 10] = 1;
	mem_watched = 0xffff;
	gfx_refresh(40, GFX_NOCURSOR);
	gfx_drain();
	check(nwins == 3 && drawn == 4, "three windows for four changed lines");
	check(nwins >= 1 && wins[0].x == 0 && wins[0].w == 4 &&
	    wins[0].y == GFX_Y0 && wins[0].h == 1, "line 0: one byte");
	check(nwins >= 2 && wins[1].x == 12 && wins[1].w == 4 &&
	    wins[1].y == GFX_Y0 + 1 && wins[1].h == 1, "line 1: one byte");
	check(nwins >= 3 && wins[2].x == 40 && wins[2].w == 4 &&
	    wins[2].y == GFX_Y0 + 4 && wins[2].h == 2, "lines 4 and 5 joined");

	gfx_close();
	if (fails) {
		printf("drain: %u of %u checks fail\n", fails, checks);
		return (1);
	}
	printf("drain: %u checks pass\n", checks);
	return (0);
}
