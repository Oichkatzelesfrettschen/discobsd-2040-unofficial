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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int destination = -1;
static int interrupted_read, interrupted_write;

static int
mode(const char *name)
{
    const char *value = getenv("CP_TEST");
    return value && strcmp(value, name) == 0;
}

ssize_t
cp_read(int fd, void *buf, size_t size)
{
    if (mode("interrupt") && !interrupted_read++) {
        errno = EINTR;
        return -1;
    }
    return read(fd, buf, size);
}

ssize_t
cp_write(int fd, const void *buf, size_t size)
{
    destination = fd;
    if (mode("interrupt") && !interrupted_write++) {
        errno = EINTR;
        return -1;
    }
    if (mode("zero"))
        return 0;
    if (mode("full")) {
        errno = ENOSPC;
        return -1;
    }
    if (mode("short") && size > 7)
        size = 7;
    return write(fd, buf, size);
}

int
cp_close(int fd)
{
    int result = close(fd);
    if (mode("close") && fd == destination) {
        errno = EIO;
        return -1;
    }
    return result;
}
