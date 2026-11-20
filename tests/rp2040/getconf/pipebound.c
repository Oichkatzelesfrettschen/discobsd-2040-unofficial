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
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

static unsigned char record[PIPE_BUF + 1];

static int
read_record(int fd, unsigned size)
{
	unsigned done = 0;
	while (done < size) {
		ssize_t n = read(fd, record + done, size - done);
		if (n <= 0)
			return -1;
		done += (unsigned)n;
	}
	return 0;
}

int
main(void)
{
	int fd[2], status, seen[2] = { 0, 0 };
	pid_t writers[2];
	unsigned i, j;

	alarm(30);
	if (pipe(fd) < 0 || fpathconf(fd[1], _PC_PIPE_BUF) != PIPE_BUF ||
	    fcntl(fd[1], F_SETFL, O_NONBLOCK) < 0)
		return 1;
	record[0] = 'x';
	if (write(fd[1], record, 1) != 1 ||
	    write(fd[1], record, PIPE_BUF) != -1 || errno != EAGAIN ||
	    read_record(fd[0], 1) < 0 || record[0] != 'x' ||
	    write(fd[1], record, PIPE_BUF) != PIPE_BUF ||
	    read_record(fd[0], PIPE_BUF) < 0 ||
	    write(fd[1], record, sizeof(record)) != PIPE_BUF ||
	    read_record(fd[0], PIPE_BUF) < 0 ||
	    fcntl(fd[1], F_SETFL, 0) < 0)
		return 1;

	/* The initial byte leaves both full-record writers short of room. */
	if (write(fd[1], "x", 1) != 1)
		return 1;
	for (i = 0; i < 2; i++) {
		writers[i] = fork();
		if (writers[i] < 0)
			return 1;
		if (writers[i] == 0) {
			close(fd[0]);
			for (j = 0; j < 4; j++) {
				memset(record, 1 + 4 * i + j, PIPE_BUF);
				if (write(fd[1], record, PIPE_BUF) != PIPE_BUF)
					_exit(1);
			}
			_exit(0);
		}
	}
	close(fd[1]);
	if (read_record(fd[0], 1) < 0 || record[0] != 'x')
		return 1;
	for (i = 0; i < 8; i++) {
		int writer;
		if (read_record(fd[0], PIPE_BUF) < 0 || record[0] < 1 || record[0] > 8)
			return 1;
		writer = (record[0] - 1) / 4;
		if ((record[0] - 1) % 4 != seen[writer]++)
			return 1;
		for (j = 1; j < PIPE_BUF; j++)
			if (record[j] != record[0])
				return 1;
	}
	for (i = 0; i < 2; i++)
		if (waitpid(writers[i], &status, 0) != writers[i] ||
		    !WIFEXITED(status) || WEXITSTATUS(status) != 0)
			return 1;
	return read(fd[0], record, 1) != 0;
}
