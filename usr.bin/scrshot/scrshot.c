/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * scrshot: save what the PicoCalc panel shows to a file.
 *
 * usage: scrshot [-a] [-f ppm|bmp|raw] [-o file]
 *
 * The program uses the pixel device alone (sys/sys/lcdgfx.h): it opens
 * /dev/lcdgfx, which takes the panel from the text console but leaves the
 * controller's memory as the console drew it, and reads that memory back
 * with LCDG_READ. The console scrolls by moving the controller's scroll
 * start, so the glass shows memory line (T + y) mod 320 at screen row y,
 * where T is the memory line the console showed at the top of the glass
 * when it gave up the panel; LCDG_XINFO reports it as text_top. -a skips
 * that arrangement and saves all LCDG_RAM_LINES lines in memory order.
 *
 * One row of the panel and one row of the image are in memory at a time.
 * The memory read needs a kernel built with LCD_GFX_EXT (sys/sys/lcdgfx.h).
 * Only the superuser's open leaves the console's screen in the memory; any
 * other user's open blanks it, so a capture by another user is a blank panel.
 * The output file is removed when a read or a write fails.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scrfmt.h"

#define RING_LINES	LCDG_HEIGHT	/* The scroll area the console uses. */

static const char *prog = "scrshot";

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-a] [-f ppm|bmp|raw] [-o file]\n", prog);
	exit(2);
}

/* Write all of buf; a short write is an error. */
static int
put(int fd, const unsigned char *buf, unsigned n)
{
	return write(fd, buf, n) == (int)n ? 0 : -1;
}

/*
 * Image rows 0 .. lines - 1 are memory lines (top + row) mod ring. Each
 * stretch that does not wrap is one LCDG_READ rectangle read row by row.
 */
static int
capture(int pfd, int ofd, int fmt, unsigned top, unsigned ring,
    unsigned lines)
{
	unsigned char in[2 * LCDG_WIDTH], out[3 * LCDG_WIDTH + 3];
	unsigned char head[SCR_HDR_MAX];
	struct lcdg_read rd;
	unsigned row, line, run, i, n;

	n = scr_header(fmt, LCDG_WIDTH, lines, head);
	if (n != 0 && put(ofd, head, n) != 0) {
		fprintf(stderr, "%s: header: write: %s\n", prog,
		    strerror(errno));
		return -1;
	}
	for (row = 0; row < lines; row += run) {
		line = (top + row) % ring;
		run = ring - line;
		if (run > lines - row)
			run = lines - row;
		memset(&rd, 0, sizeof(rd));
		rd.w = LCDG_WIDTH;
		rd.y = (u_short)line;
		rd.h = (u_short)run;
		rd.fmt = LCDG_RD_RGB565;
		if (ioctl(pfd, LCDG_READ, &rd) < 0) {
			fprintf(stderr, "%s: row %u: LCDG_READ: %s\n", prog,
			    row, strerror(errno));
			return -1;
		}
		for (i = 0; i < run; i++) {
			errno = 0;
			if (read(pfd, in, sizeof(in)) != (int)sizeof(in)) {
				fprintf(stderr, "%s: row %u: read: %s\n", prog,
				    row + i, errno != 0 ? strerror(errno) :
				    "short read");
				return -1;
			}
			n = scr_row(fmt, LCDG_WIDTH, in, out);
			if (put(ofd, out, n) != 0) {
				fprintf(stderr, "%s: row %u: write: %s\n",
				    prog, row + i, strerror(errno));
				return -1;
			}
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	static const char *const ext[] = { "ppm", "bmp", "raw" };
	const char *path = NULL, *fname = "ppm";
	char dflt[24];
	struct lcdg_info info;
	struct lcdg_xinfo xi;
	int c, all = 0, fmt, pfd, ofd, status;

	if (argc > 0 && argv[0] != NULL)
		prog = argv[0];
	while ((c = getopt(argc, argv, "af:o:")) != -1) {
		switch (c) {
		case 'a':
			all = 1;
			break;
		case 'f':
			fname = optarg;
			break;
		case 'o':
			path = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind != argc)
		usage();
	for (fmt = 0; fmt < 3 && strcmp(fname, ext[fmt]) != 0; fmt++)
		;
	if (fmt == 3)
		usage();
	if (path == NULL) {
		snprintf(dflt, sizeof(dflt), "/tmp/scrshot.%s", ext[fmt]);
		path = dflt;
	}

	pfd = open("/dev/lcdgfx", O_RDWR);
	if (pfd < 0) {
		fprintf(stderr, "%s: /dev/lcdgfx: %s\n", prog, strerror(errno));
		return 1;
	}
	if (ioctl(pfd, LCDG_INFO, &info) < 0 ||
	    info.w != LCDG_WIDTH || info.h != LCDG_HEIGHT) {
		fprintf(stderr, "%s: LCDG_INFO: not a 320x320 panel\n", prog);
		return 1;
	}
	if (ioctl(pfd, LCDG_XINFO, &xi) < 0) {
		fprintf(stderr, "%s: LCDG_XINFO: %s (the kernel needs "
		    "LCD_GFX_EXT)\n", prog, strerror(errno));
		return 1;
	}
	if (xi.ram_lines != LCDG_RAM_LINES || xi.text_top >= RING_LINES) {
		fprintf(stderr, "%s: LCDG_XINFO: ring top %u of %u lines "
		    "is out of range\n", prog, xi.text_top, xi.ram_lines);
		return 1;
	}
	ofd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (ofd < 0) {
		fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
		return 1;
	}
	if (all)
		status = capture(pfd, ofd, fmt, 0, LCDG_RAM_LINES,
		    LCDG_RAM_LINES);
	else
		status = capture(pfd, ofd, fmt, xi.text_top, RING_LINES,
		    LCDG_HEIGHT);
	if (close(ofd) != 0 && status == 0) {
		fprintf(stderr, "%s: %s: close: %s\n", prog, path,
		    strerror(errno));
		status = -1;
	}
	if (status != 0)
		(void)unlink(path);
	close(pfd);
	return status == 0 ? 0 : 1;
}
