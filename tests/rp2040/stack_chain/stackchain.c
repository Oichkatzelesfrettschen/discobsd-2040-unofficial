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
 * Drive the kernel call chains the static stack bound of
 * docs/research/kernel-size-flags names, one stage per invocation, so that
 * kern.capacity read between invocations gives each stage's watermark.
 *
 *	stackchain churn N	write and remove a 64 KiB file N times, which
 *				wraps the Dhara journal and starts its
 *				garbage collection
 *	stackchain file		exit holding an unlinked file with indirect
 *				blocks, beside a dirty buffer
 *	stackchain exec		a traced child stops terminal output on
 *				/dev/tty0, queues bytes there on a
 *				close-on-exec descriptor, holds the same kind
 *				of file and executes a script; the close in
 *				execve then waits for the drain, the trace
 *				signal execve posts stops the child inside
 *				that wait and the parent's PT_KILL ends it
 *				there; a helper restarts the output later so
 *				that the dying child's own close returns
 *
 *	stackchain execplain	the same without the file, as the control
 *
 * Every step prints one key=value line.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	BLOCK		1024L
#define	NDIRECT		4L
#define	NPOINTER	256L	/* block addresses in one indirect block */
#define	DEEP		"/tmp/sc.deep"
#define	DIRTY		"/tmp/sc.dirty"
#define	SCRIPT		"/tmp/sc.sh"
#define	MARK		"/tmp/sc.ran"

static char block[BLOCK];

/*
 * An unlinked file whose last block sits behind as many indirection levels
 * as the file system accepts, three first. The result is the descriptor,
 * and the level reached is printed.
 */
static int
deep_file(void)
{
	static const long first[] = {
		(NDIRECT + NPOINTER + NPOINTER * NPOINTER) * BLOCK,
		(NDIRECT + NPOINTER) * BLOCK,
		NDIRECT * BLOCK,
	};
	int fd, i;

	for (i = 0; i < 3; i++) {
		fd = open(DEEP, O_RDWR | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) {
			printf("deep_open_errno=%d\n", errno);
			return -1;
		}
		(void)unlink(DEEP);
		if (lseek(fd, (off_t)first[i], SEEK_SET) == (off_t)first[i] &&
		    write(fd, block, sizeof block) == (int)sizeof block) {
			printf("deep_levels=%d\n", 3 - i);
			return fd;
		}
		printf("deep_level_%d_errno=%d\n", 3 - i, errno);
		(void)close(fd);
	}
	return -1;
}

/* Leave a delayed-write buffer for the next buffer claim to flush. */
static void
dirty_buffer(void)
{
	int fd = open(DIRTY, O_WRONLY | O_CREAT | O_TRUNC, 0600);

	if (fd < 0 || write(fd, block, sizeof block) != (int)sizeof block)
		printf("dirty_errno=%d\n", errno);
	if (fd >= 0)
		(void)close(fd);
}

static int
churn(int rounds)
{
	int fd, i, k;

	for (i = 0; i < rounds; i++) {
		fd = open(DIRTY, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) {
			printf("churn_open_errno=%d\n", errno);
			return 1;
		}
		memset(block, 'a' + i % 26, sizeof block);
		for (k = 0; k < 64; k++)
			if (write(fd, block, sizeof block) != (int)sizeof block) {
				printf("churn_write_errno=%d\n", errno);
				(void)close(fd);
				return 1;
			}
		(void)close(fd);
		(void)unlink(DIRTY);
		sync();
	}
	printf("churn_rounds=%d\n", rounds);
	return 0;
}

static int
stage_file(void)
{
	if (deep_file() < 0)
		return 1;
	dirty_buffer();
	printf("file_exit=1\n");
	fflush(stdout);
	_exit(0);
}

static int
stage_exec(int deep)
{
	static const char script[] = "#!/bin/sh\necho ran >" MARK "\n";
	int fd, tty, status;
	pid_t pid, helper;

	(void)unlink(MARK);
	fd = open(SCRIPT, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd < 0 || write(fd, script, sizeof script - 1) !=
	    (int)(sizeof script - 1)) {
		printf("script_errno=%d\n", errno);
		return 1;
	}
	(void)close(fd);
	fflush(stdout);
	/*
	 * The child dies inside its own exit while that exit closes the
	 * stopped terminal, which would wait for ever: a helper restarts the
	 * output once the child has had time to be stopped and killed.
	 */
	helper = fork();
	if (helper == 0) {
		sleep(8);
		tty = open("/dev/tty0", O_WRONLY | O_NONBLOCK);
		printf("helper_start=%d\n",
		    tty >= 0 && ioctl(tty, TIOCSTART, 0) == 0);
		_exit(0);
	}
	pid = fork();
	if (pid < 0) {
		printf("fork_errno=%d\n", errno);
		return 1;
	}
	if (pid == 0) {
		if (ptrace(PT_TRACE_ME, 0, 0, 0) < 0 && errno != 0)
			printf("trace_errno=%d\n", errno);
		tty = open("/dev/tty0", O_WRONLY);
		if (tty < 0 || fcntl(tty, F_SETFD, 1) < 0 ||
		    ioctl(tty, TIOCSTOP, 0) < 0 ||
		    write(tty, "stackchain\r\n", 12) != 12) {
			printf("tty_errno=%d\n", errno);
			fflush(stdout);
			_exit(8);
		}
		if (deep) {
			if (deep_file() < 0)
				_exit(7);
			dirty_buffer();
		}
		printf("child_exec=1 deep=%d\n", deep);
		fflush(stdout);
		execl(SCRIPT, "sc.sh", (char *)0);
		printf("exec_errno=%d\n", errno);
		fflush(stdout);
		_exit(9);
	}
	if (waitpid(pid, &status, WUNTRACED) != pid) {
		printf("wait_errno=%d\n", errno);
		return 1;
	}
	printf("child_stopped=%d stop_signal=%d\n", WIFSTOPPED(status) != 0,
	    WIFSTOPPED(status) ? WSTOPSIG(status) : 0);
	errno = 0;
	(void)ptrace(PT_KILL, pid, 0, 0);
	printf("ptkill_errno=%d\n", errno);
	if (waitpid(pid, &status, 0) != pid)
		printf("wait2_errno=%d\n", errno);
	else
		printf("child_signaled=%d child_exited=%d\n",
		    WIFSIGNALED(status) != 0, WIFEXITED(status) != 0);
	/* The script's mark is absent when the child died inside execve. */
	printf("script_ran=%d\n", access(MARK, 0) == 0);
	if (helper > 0)
		(void)waitpid(helper, &status, 0);
	(void)unlink(SCRIPT);
	(void)unlink(DIRTY);
	return 0;
}

int
main(int argc, char **argv)
{
	setvbuf(stdout, (char *)0, _IONBF, 0);
	if (argc == 3 && strcmp(argv[1], "churn") == 0)
		return churn(atoi(argv[2]));
	if (argc == 2 && strcmp(argv[1], "file") == 0)
		return stage_file();
	if (argc == 2 && strcmp(argv[1], "exec") == 0)
		return stage_exec(1);
	if (argc == 2 && strcmp(argv[1], "execplain") == 0)
		return stage_exec(0);
	fprintf(stderr, "usage: stackchain churn N | file | exec | execplain\n");
	return 2;
}
