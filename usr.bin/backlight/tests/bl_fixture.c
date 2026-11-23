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
 * The host side of backlight.h: a model of the keyboard controller's two
 * light registers for tests/check.py.
 *
 *   BL_STATE  a file holding "LCD KBD", the levels the controller keeps;
 *             a set rewrites it
 *   BL_LOG    a file that receives "set NAME LEVEL" for each request
 *   BL_UID    the real user id bl_root() reports (default 1000)
 *   BL_ERR    EOPNOTSUPP, EPERM or EIO: every request fails with it
 *   BL_ERR_SET  the same for a set alone, so a read still answers
 *
 * A set conditions its level as the vendor firmware's backlight.ino does:
 * the screen rounds down to a multiple of 16 within 16..240, the keys
 * round down to a multiple of 32 and go dark below 32.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backlight.h"

static int
injected(const char *name)
{
	const char *e = getenv(name);

	if (e == NULL)
		return 0;
	if (strcmp(e, "EOPNOTSUPP") == 0)
		return EOPNOTSUPP;
	if (strcmp(e, "EPERM") == 0)
		return EPERM;
	return EIO;
}

static int
load(int *lcd, int *kbd)
{
	const char *path = getenv("BL_STATE");
	FILE *f;
	int n;

	if (path == NULL || (f = fopen(path, "r")) == NULL)
		return EIO;
	n = fscanf(f, "%d %d", lcd, kbd);
	fclose(f);
	return n == 2 ? 0 : EIO;
}

int
bl_get(int light, int *level)
{
	int lcd, kbd, error;

	if ((error = injected("BL_ERR")) != 0 || (error = load(&lcd, &kbd)) != 0)
		return error;
	*level = light == BL_KBD ? kbd : lcd;
	return 0;
}

int
bl_set(int light, int level)
{
	const char *path = getenv("BL_STATE"), *log = getenv("BL_LOG");
	int lcd, kbd, error;
	FILE *f;

	if ((error = injected("BL_ERR")) != 0 ||
	    (error = injected("BL_ERR_SET")) != 0 ||
	    (error = load(&lcd, &kbd)) != 0)
		return error;
	if (log != NULL && (f = fopen(log, "a")) != NULL) {
		fprintf(f, "set %s %d\n", light == BL_KBD ? "kbd" : "lcd", level);
		fclose(f);
	}
	if (light == BL_KBD) {
		kbd = level / 32 * 32;
		if (kbd < 32 || kbd > BL_LEVEL_MAX)
			kbd = 0;
	} else {
		lcd = level / 16 * 16;
		lcd = lcd < 16 ? 16 : lcd > BL_LEVEL_MAX ? BL_LEVEL_MAX : lcd;
	}
	if ((f = fopen(path, "w")) == NULL)
		return EIO;
	fprintf(f, "%d %d\n", lcd, kbd);
	return fclose(f) == 0 ? 0 : EIO;
}

int
bl_root(void)
{
	const char *uid = getenv("BL_UID");

	return uid != NULL && strcmp(uid, "0") == 0;
}
