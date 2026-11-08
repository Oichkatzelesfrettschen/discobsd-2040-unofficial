/*
 * time -- run a command and report its real, user and system time on
 * standard error. The exit status is the command's own, 128 plus the
 * signal number when a signal ended the command, 127 when the command
 * cannot be executed and 125 when time itself fails.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

static void printt(const char *, const struct timeval *);

int
main(int argc, char **argv)
{
	struct timeval before, after;
	struct rusage ru;
	int status, w;
	pid_t p;

	if (argc <= 1)
		return 0;
	gettimeofday(&before, 0);
	p = fork();
	if (p < 0) {
		perror("time");
		return 125;
	}
	if (p == 0) {
		execvp(argv[1], &argv[1]);
		perror(argv[1]);
		_exit(127);
	}
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	while ((w = wait3(&status, 0, &ru)) != p) {
		if (w < 0 && errno != EINTR) {
			perror("time");
			return 125;
		}
	}
	gettimeofday(&after, 0);
	if (WIFSIGNALED(status))
		fputs("Command terminated abnormally.\n", stderr);
	after.tv_sec -= before.tv_sec;
	after.tv_usec -= before.tv_usec;
	if (after.tv_usec < 0)
		after.tv_sec--, after.tv_usec += 1000000;
	printt("real", &after);
	printt("user", &ru.ru_utime);
	printt("sys ", &ru.ru_stime);
	fputc('\n', stderr);
	return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
}

static void
printt(const char *s, const struct timeval *tv)
{
	fprintf(stderr, "%9ld.%02ld %s ", (long)tv->tv_sec,
	    (long)tv->tv_usec / 10000, s);
}
