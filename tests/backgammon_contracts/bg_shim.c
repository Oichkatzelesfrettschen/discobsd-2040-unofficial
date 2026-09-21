/*
 * The host side of the gate: stdio, pipes and wait(2).  bg_shim.h states why
 * this translation unit holds no tree header.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bg_shim.h"

unsigned bg_checks;
unsigned bg_failures;

void
bg_fail(const char *file, int line, const char *what)
{
	bg_failures++;
	fprintf(stderr, "FAIL %s:%d: %s\n", file, line, what);
}

void
bg_note(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
}

int
bg_verdict(const char *suite)
{
	fprintf(stderr, "%s: %u checks, %u failures\n", suite, bg_checks,
	    bg_failures);
	return (bg_failures == 0 ? 0 : 1);
}

void
bg_silence_stdout(void)
{
	int fd = open("/dev/null", O_WRONLY);

	if (fd < 0) {
		perror("open /dev/null");
		exit(2);
	}
	if (dup2(fd, 1) < 0) {
		perror("dup2");
		exit(2);
	}
	if (fd != 1)
		close(fd);
}

void
bg_feed(int byte)
{
	unsigned char b = (unsigned char)byte;
	int fds[2];

	if (pipe(fds) < 0) {
		perror("pipe");
		exit(2);
	}
	if (write(fds[1], &b, 1) != 1) {
		perror("write");
		exit(2);
	}
	close(fds[1]);
	if (dup2(fds[0], 0) < 0) {
		perror("dup2");
		exit(2);
	}
	close(fds[0]);
}

int
bg_child(int (*fn)(void))
{
	pid_t pid;
	int status;

	fflush(NULL);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0)
		_exit(fn() & 0xff);
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		exit(2);
	}
	if (!WIFEXITED(status))
		return (-2);
	return (WEXITSTATUS(status));
}
