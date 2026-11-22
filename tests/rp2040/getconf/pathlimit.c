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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* confcheck supplies a private native directory on the disposable card. */
int
main(void)
{
	char name[65];
	int fd;
	long limit;

	fd = open(".", O_RDONLY);
	if (fd < 0)
		return 1;
	limit = fpathconf(fd, _PC_NAME_MAX);
	if (close(fd) < 0 || limit != 63 || pathconf(".", _PC_NAME_MAX) != limit)
		return 1;
	if (fpathconf(fd, _PC_NAME_MAX) != -1 || errno != EBADF)
		return 1;
	memset(name, 'a', sizeof(name));
	name[limit] = '\0';
	fd = open(name, O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd < 0)
		return 1;
	if (close(fd) < 0 || unlink(name) < 0)
		return 1;
	name[limit] = 'a';
	name[limit + 1] = '\0';
	fd = open(name, O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd >= 0) {
		close(fd);
		unlink(name);
		return 1;
	}
	if (errno != ENAMETOOLONG)
		return 1;
	if (open("", O_RDONLY) != -1 || errno != ENOENT ||
	    unlink("") != -1 || errno != ENOENT ||
	    mkdir("", 0700) != -1 || errno != ENOENT ||
	    rmdir("") != -1 || errno != ENOENT)
		return 1;
	fd = open("\200\257\377", O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd < 0 || close(fd) < 0)
		return 1;
	if (rename("", "new") != -1 || errno != ENOENT ||
	    rename("\200\257\377", "") != -1 || errno != ENOENT)
		return 1;
	return unlink("\200\257\377") < 0;
}
