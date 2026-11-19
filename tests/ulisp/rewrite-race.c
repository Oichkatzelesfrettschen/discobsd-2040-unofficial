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
 * An LD_PRELOAD shim that changes a saved uLisp image between the two reads
 * the loader makes of it. loadimagefile() checks the file, then seeks to the
 * end of the header to install the cells; the first such seek on a file named
 * race.img overwrites every cell and code byte with 0xFF, keeping the size,
 * and one on truncrace.img cuts the file short, and then the seek is made.
 * A loader that trusts its first check installs the rewritten bytes, and
 * one that checks what it installed refuses them; a truncated file fails the
 * second read, which leaves the workspace to be rebuilt.
 * HEADERBYTES is IMAGEWORDS (6) words and CODEBYTES is CODESIZE in ulisp.c.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEADERBYTES (6 * sizeof(uintptr_t))
#define CODEBYTES 256

/* Fills path with the name of the file fd reads; true when it ends in race.img */
static bool
raceimage(int fd, char *path, size_t size)
{
	char link[64];
	const char suffix[] = "race.img";  /* also truncrace.img */
	ssize_t n;

	(void)snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
	n = readlink(link, path, size - 1);
	if (n < (ssize_t)sizeof suffix - 1)
		return false;
	path[n] = '\0';
	return strcmp(path + n - (sizeof suffix - 1), suffix) == 0;
}

static void
rewrite(const char *path)
{
	static unsigned char fill[4096];
	struct stat st;
	off_t at = (off_t)(HEADERBYTES + CODEBYTES);
	const char trunc[] = "truncrace.img";
	size_t length = strlen(path);
	int fd = open(path, O_WRONLY);

	if (fd < 0)
		return;
	if (length >= sizeof trunc - 1 &&
	    strcmp(path + length - (sizeof trunc - 1), trunc) == 0) {
		if (ftruncate(fd, at + 64) != 0)
			at = 0;
		(void)close(fd);
		return;
	}
	memset(fill, 0xFF, sizeof fill);
	if (fstat(fd, &st) == 0) {
		while (at < st.st_size) {
			size_t n = sizeof fill;

			if ((off_t)n > st.st_size - at)
				n = (size_t)(st.st_size - at);
			if (pwrite(fd, fill, n, at) != (ssize_t)n)
				break;
			at += (off_t)n;
		}
	}
	(void)close(fd);
}

off_t
lseek(int fd, off_t offset, int whence)
{
	static off_t (*real)(int, off_t, int);
	static bool done;
	char path[PATH_MAX];

	if (real == NULL)
		real = (off_t (*)(int, off_t, int))dlsym(RTLD_NEXT, "lseek");
	if (!done && whence == SEEK_SET && offset == (off_t)HEADERBYTES &&
	    raceimage(fd, path, sizeof path)) {
		done = true;
		rewrite(path);
	}
	return real(fd, offset, whence);
}
