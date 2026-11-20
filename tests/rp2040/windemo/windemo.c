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
 * restores the console by closing the device. Each window paints a
 * background stripe and a line of text in its client area. A board run is
 * opt-in: it takes over the panel until the program exits. Prints "WINDEMO
 * OK" when every call succeeds.
 *
 * Regression tool, not shipped in the root manifest; stage it with a "file
 * /usr/bin/windemo" line in distrib/rp2040/mi.rp2040 for a test image or
 * copy it to the board over the console.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "win.h"

#define DEVICE		"/dev/lcdgfx"
#define STEP		8
#define NSTEPS		20

static void
paint_label(struct win_ctx *c, void *arg)
{
	const char *label = arg;

	win_fill(c, 0, 0, c->w, 12, 1);
	win_hline(c, 0, 12, c->w, 15);
	win_text(c, 2, 2, label, 15);
	win_vline(c, c->w - 3, 14, c->h - 14, 2);
}

int
main(void)
{
	/*
	 * The scene is 5,824 bytes and a process starts with a 2,048-byte
	 * stack (SSIZE in machine/machparam.h), so it has static storage.
	 */
	static struct win_scene scene;
	struct win_backend backend;
	struct win_lcdgfx lcd;
	struct win *a, *b, *c;
	int fd, i;

	fd = open(DEVICE, O_WRONLY);
	if (fd < 0) {
		printf("windemo: cannot open %s\n", DEVICE);
		return 1;
	}
	win_lcdgfx_init(&backend, &lcd, fd);
	if (win_scene_init(&scene, &backend) != 0)
		return 1;
	a = win_create(&scene, 20, 20, 140, 100, "first", paint_label, "alpha");
	b = win_create(&scene, 100, 60, 150, 110, "second", paint_label, "beta");
	c = win_create(&scene, 60, 90, 100, 60, "third", paint_label, "gamma");
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
	close(fd);
	printf("WINDEMO OK\n");
	return 0;
}
