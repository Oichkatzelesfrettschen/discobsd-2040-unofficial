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
 * dev_picocalc.c against a model of /dev/lcdgfx, for the one rule of the
 * pixel display that a transcript cannot show: the first drawing or read
 * command of each open follows an LCDG_COLMOD of 0x55, since an open starts
 * in RGB666 and every color the program draws is an RGB565 value. The
 * system calls of dev_picocalc.c are renamed to the stubs below before the
 * file is included, so the code under test is the shipped source.
 *
 * usage: dev-picocalc-test
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sys/lcdgfx.h>

static int nbad, nopen, nclose, colmod;

#define CHECK(cond, ...) \
	do { \
		if (!(cond)) { \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
			nbad++; \
		} \
	} while (0)

#define MODEL_FD 7

static int
stub_open(const char *path, int flags, ...)
{
	(void)flags;
	if (strcmp(path, "/dev/lcdgfx") != 0) {
		errno = ENOENT;
		return -1;
	}
	nopen++;
	colmod = 0x66;		/* The kernel's format at an open. */
	return MODEL_FD;
}

static int
stub_close(int fd)
{
	CHECK(fd == MODEL_FD, "close of fd %d", fd);
	nclose++;
	return 0;
}

static int
stub_ioctl(int fd, unsigned long cmd, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, cmd);
	arg = va_arg(ap, void *);
	va_end(ap);
	CHECK(fd == MODEL_FD, "ioctl on fd %d", fd);
	if (cmd == LCDG_COLMOD) {
		colmod = ((struct lcdg_colmod *)arg)->fmt;
		return 0;
	}
	CHECK(colmod == LCDG_COLMOD_RGB565,
	    "command %lx reaches the panel at COLMOD %02x, not 55", cmd, colmod);
	return 0;
}

static ssize_t
stub_read(int fd, void *buf, size_t n)
{
	(void)fd;
	CHECK(colmod == LCDG_COLMOD_RGB565, "read at COLMOD %02x, not 55", colmod);
	memset(buf, 0, n);
	return (ssize_t)n;
}

static ssize_t
stub_write(int fd, const void *buf, size_t n)
{
	(void)fd;
	(void)buf;
	return (ssize_t)n;
}

static int
stub_sysctl(int *mib, unsigned n, void *old, size_t *oldlen, void *new, size_t newlen)
{
	(void)mib;
	(void)n;
	(void)old;
	(void)oldlen;
	(void)new;
	(void)newlen;
	errno = ENOENT;
	return -1;
}

#define open	stub_open
#define close	stub_close
#define ioctl	stub_ioctl
#define read	stub_read
#define write	stub_write
#define sysctl	stub_sysctl

#include <dev_picocalc.c>

void
sys_flush(void)
{
}

void
ulisp_fail(const char *message)
{
	printf("FAIL ulisp_fail: %s\n", message);
	exit(1);
}

int
main(void)
{
	/* The first drawing command of an open follows COLMOD 55. */
	dev_fill(0, 0, 9, 9, 0xf800);
	CHECK(nopen == 1 && colmod == LCDG_COLMOD_RGB565, "fill: %d opens, COLMOD %02x", nopen, colmod);
	dev_fill(1, 1, 2, 2, 0x07e0);
	CHECK(nopen == 1, "a second fill reopens: %d opens", nopen);
	/* A close returns the panel to the console; the next open sets the format again. */
	dev_release();
	CHECK(nclose == 1, "release closes once: %d", nclose);
	(void)dev_readpixel(3, 3);
	CHECK(nopen == 2 && colmod == LCDG_COLMOD_RGB565, "read: %d opens, COLMOD %02x", nopen, colmod);
	dev_release();
	printf("%s\n", nbad == 0 ? "dev_picocalc: ok" : "dev_picocalc: failed");
	return nbad != 0;
}
