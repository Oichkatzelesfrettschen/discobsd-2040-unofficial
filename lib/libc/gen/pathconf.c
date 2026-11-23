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

#include <sys/param.h>
#include <sys/dir.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static long
mount_limit(const struct statfs *mount_info, int name)
{
	if (name == _PC_NAME_MAX)
		return (mount_info->f_flags & MNT_V7) ? V7_NAMELEN : MAXNAMLEN;
	if (name == _PC_LINK_MAX)
		return (mount_info->f_flags & MNT_V7) ? V7_LINK_MAX : NATIVE_LINK_MAX;
	if (name == _PC_ALLOC_SIZE_MIN)
		return mount_info->f_bsize;
	static const long fixed[] = {
		[_PC_PIPE_BUF] = PIPE_BUF,
		[_PC_PATH_MAX] = PATH_MAX,
		[_PC_NO_TRUNC] = _POSIX_NO_TRUNC,
		[_PC_CHOWN_RESTRICTED] = _POSIX_CHOWN_RESTRICTED,
		[_PC_2_SYMLINKS] = 1,
		[_PC_TIMESTAMP_RESOLUTION] = 1000000000L
	};
	if (name >= _PC_PIPE_BUF && name <= _PC_TIMESTAMP_RESOLUTION)
		return fixed[name];
	errno = EINVAL;
	return -1;
}

long
pathconf(const char *path, int name)
{
	struct statfs mount_info;

	if (name < _PC_NAME_MAX || name > _PC_TIMESTAMP_RESOLUTION) {
		errno = EINVAL;
		return -1;
	}
	if (*path == '\0') {
		errno = ENOENT;
		return -1;
	}
	if (statfs(path, &mount_info) == -1)
		return -1;
	return mount_limit(&mount_info, name);
}

long
fpathconf(int fd, int name)
{
	struct statfs mount_info;

	if (name == _PC_PIPE_BUF)
		return fcntl(fd, F_GETFD, 0) < 0 ? -1 : PIPE_BUF;
	if (name < _PC_NAME_MAX || name > _PC_TIMESTAMP_RESOLUTION) {
		errno = EINVAL;
		return -1;
	}
	if (fstatfs(fd, &mount_info) == -1)
		return -1;
	return mount_limit(&mount_info, name);
}
