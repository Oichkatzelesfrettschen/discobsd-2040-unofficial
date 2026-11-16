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
 * LD_PRELOAD shim for the host-built sbin/fsck (tests/fsck_inode_range):
 * counts the read and write calls fsck makes on the image and on its scratch
 * device, and sends the scratch device /dev/temp0 to the file FSCK_TEMP names,
 * so a filesystem whose maps exceed fsck's 16 in-core buffers runs on the host
 * the way it runs on the board, with its maps on a scratch file. The counts go
 * to the file FSCK_IO_REPORT names when fsck exits, as one line of
 * "image_reads image_read_bytes image_writes temp_reads temp_appends
 * temp_rewrites temp_opens".
 *
 * A scratch write is an append while every write since the open has gone to
 * the next block in order from block 0, and a rewrite from the first write
 * out of that order on: the rule swtemp_write_flags() in sys/dev/swap.c
 * applies to a temporary unit, where an append programs erased flash and a
 * rewrite copies the whole 4 KiB sector through a spare (flash_swap_rewrite()
 * in sys/arch/rp2040/dev/flash_swap.c).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define MAXFD 256

enum fdclass { FD_OTHER, FD_IMAGE, FD_TEMP };

static enum fdclass fdclass[MAXFD];
static off_t position[MAXFD];
static unsigned long image_reads, image_read_bytes, image_writes;
static unsigned long temp_reads, temp_appends, temp_rewrites, temp_opens;
static off_t temp_next;
static int temp_append_open = 1;

static void
classify(int fd, const char *path)
{
	const char *image = getenv("FSCK_IMAGE");

	if (fd < 0 || fd >= MAXFD)
		return;
	if (strcmp(path, "/dev/temp0") == 0)
		fdclass[fd] = FD_TEMP;
	else if (image != NULL && strcmp(path, image) == 0)
		fdclass[fd] = FD_IMAGE;
	else
		fdclass[fd] = FD_OTHER;
}

static int
open_common(const char *path, int flags, mode_t mode, const char *symbol)
{
	int (*real)(const char *, int, ...) =
	    (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, symbol);
	const char *target = path;
	int fd;

	if (strcmp(path, "/dev/temp0") == 0) {
		target = getenv("FSCK_TEMP");
		if (target == NULL)
			target = path;
		temp_opens++;
	}
	fd = real(target, flags, mode);
	classify(fd, path);
	if (fd >= 0 && fd < MAXFD)
		position[fd] = 0;
	return fd;
}

int
open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	va_list ap;

	if (flags & O_CREAT) {
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
	}
	return open_common(path, flags, mode, "open");
}

int
open64(const char *path, int flags, ...)
{
	mode_t mode = 0;
	va_list ap;

	if (flags & O_CREAT) {
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
	}
	return open_common(path, flags, mode, "open64");
}

off_t
lseek(int fd, off_t offset, int whence)
{
	off_t (*real)(int, off_t, int) =
	    (off_t (*)(int, off_t, int))dlsym(RTLD_NEXT, "lseek");
	off_t result = real(fd, offset, whence);

	if (fd >= 0 && fd < MAXFD && result >= 0)
		position[fd] = result;
	return result;
}

ssize_t
read(int fd, void *buf, size_t count)
{
	ssize_t (*real)(int, void *, size_t) =
	    (ssize_t (*)(int, void *, size_t))dlsym(RTLD_NEXT, "read");
	ssize_t done = real(fd, buf, count);

	if (fd >= 0 && fd < MAXFD) {
		if (fdclass[fd] == FD_IMAGE) {
			image_reads++;
			if (done > 0)
				image_read_bytes += (unsigned long)done;
		} else if (fdclass[fd] == FD_TEMP)
			temp_reads++;
		if (done > 0)
			position[fd] += done;
	}
	return done;
}

ssize_t
write(int fd, const void *buf, size_t count)
{
	ssize_t (*real)(int, const void *, size_t) =
	    (ssize_t (*)(int, const void *, size_t))dlsym(RTLD_NEXT, "write");

	ssize_t done;

	if (fd >= 0 && fd < MAXFD) {
		if (fdclass[fd] == FD_IMAGE)
			image_writes++;
		else if (fdclass[fd] == FD_TEMP) {
			if (temp_append_open && position[fd] == temp_next) {
				temp_appends++;
				temp_next += (off_t)count;
			} else {
				temp_append_open = 0;
				temp_rewrites++;
			}
		}
	}
	done = real(fd, buf, count);
	if (fd >= 0 && fd < MAXFD && done > 0)
		position[fd] += done;
	return done;
}

__attribute__((destructor)) static void
report(void)
{
	const char *path = getenv("FSCK_IO_REPORT");
	FILE *out;

	if (path == NULL || (out = fopen(path, "w")) == NULL)
		return;
	fprintf(out, "%lu %lu %lu %lu %lu %lu %lu\n", image_reads,
	    image_read_bytes, image_writes, temp_reads, temp_appends,
	    temp_rewrites, temp_opens);
	fclose(out);
}
