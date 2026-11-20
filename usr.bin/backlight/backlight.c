/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
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
 * backlight: show and set the PicoCalc's screen and keyboard backlights
 * (backlight.1).
 *
 * The kernel accepts a sysctl write from root only (__sysctl() in
 * sys/kern/kern_sysctl.c), so the manifest installs this program setuid
 * root and any user can change the two lights. The privilege reaches no
 * further than that. An argument is one of the two light names or a
 * decimal level of at most 240, and anything else ends the program before
 * a request. The program reads no environment variable, and the one file it
 * writes, BACKLIGHT_FILE, at a fixed path, it opens only when the real user
 * is root (-s).
 *
 * The keyboard controller conditions what it is given: the vendor firmware
 * (backlight.ino) rounds a screen level down to a multiple of 16 within
 * 16..240 and a keyboard level down to a multiple of 32, below 32 off. The
 * program therefore reads the level back after a write and prints, and
 * saves, the level the controller kept.
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "backlight.h"

static const char light_name[2][4] = { "lcd", "kbd" };

static int
say(int fd, const char *s)
{
	size_t n = strlen(s);

	return write(fd, s, n) == (ssize_t)n ? 0 : 1;
}

static int
usage(void)
{
	(void)say(2, "usage: backlight [-s] [lcd | kbd] [level]\n");
	return 2;
}

static int
fail(const char *what)
{
	(void)say(2, "backlight: ");
	(void)say(2, what);
	(void)say(2, "\n");
	return 1;
}

static int
refused(int error)
{
	switch (error) {
	case EOPNOTSUPP:
	case ENOTDIR:
	case ENOENT:
		return fail("this kernel has no PicoCalc keyboard controller");
	case EPERM:
		return fail("permission denied: install backlight setuid root");
	default:
		return fail("the keyboard controller did not answer");
	}
}

/* A level is decimal digits worth at most BL_LEVEL_MAX; -1 otherwise. */
static int
parse_level(const char *s)
{
	const char *p;
	int v = 0;

	for (p = s; *p >= '0' && *p <= '9'; p++) {
		v = v * 10 + (*p - '0');
		if (v > BL_LEVEL_MAX)
			return -1;
	}
	return p == s || *p != '\0' ? -1 : v;
}

/*
 * Write v and a newline into the bytes before end; the start of the digits.
 * Ten digits, the newline and the light's name with its space fill the
 * 15 bytes in front of a caller's terminating NUL for any int.
 */
static char *
decimal(char *end, int v)
{
	unsigned u = (unsigned)v;

	*--end = '\n';
	do {
		*--end = (char)('0' + u % 10);
		u /= 10;
	} while (u != 0);
	return end;
}

static int
show(int light, int level)
{
	char buf[16], *p;

	buf[sizeof buf - 1] = '\0';
	p = decimal(buf + sizeof buf - 1, level);
	*--p = ' ';
	p -= 3;
	memcpy(p, light_name[light], 3);
	return say(1, p);
}

static int
save(int level)
{
	char buf[16], *p;
	int fd, error;

	buf[sizeof buf - 1] = '\0';
	p = decimal(buf + sizeof buf - 1, level);
	fd = open(BACKLIGHT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return 1;
	error = say(fd, p);
	return close(fd) != 0 || error;
}

int
main(int argc, char **argv)
{
	int saving = 0, named = 0, light = BL_LCD, level = -1, kept, error;

	if (argc > 1 && strcmp(argv[1], "-s") == 0) {
		saving = 1;
		argc--;
		argv++;
	}
	if (argc > 1 && (strcmp(argv[1], light_name[BL_LCD]) == 0 ||
	    strcmp(argv[1], light_name[BL_KBD]) == 0)) {
		light = argv[1][0] == 'k' ? BL_KBD : BL_LCD;
		named = 1;
		argc--;
		argv++;
	}
	if (argc > 1) {
		level = parse_level(argv[1]);
		if (level < 0)
			return usage();
		argc--;
		argv++;
	}
	if (argc > 1 || (saving && light != BL_LCD))
		return usage();
	if (saving && !bl_root())
		return fail("-s: only root sets the boot level");

	if (!saving && !named && level < 0) {
		for (light = BL_LCD; light <= BL_KBD; light++) {
			error = bl_get(light, &kept);
			if (error != 0)
				return refused(error);
			if (show(light, kept) != 0)
				return 1;
		}
		return 0;
	}
	if (level >= 0) {
		error = bl_set(light, level);
		if (error != 0)
			return refused(error);
	}
	error = bl_get(light, &kept);
	if (error != 0)
		return refused(error);
	if (show(light, kept) != 0)
		return 1;
	if (saving && save(kept) != 0)
		return fail("cannot write " BACKLIGHT_FILE);
	return 0;
}
