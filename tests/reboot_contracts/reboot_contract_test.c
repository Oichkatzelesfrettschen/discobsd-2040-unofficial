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

/*
 * Host contract of sbin/reboot/reboot.c. The source is compiled unchanged
 * with main renamed and the system calls it makes (reboot, kill, sync,
 * sleep, geteuid, open, syslog) replaced by recorders. Each case runs in a
 * forked child so reboot.c's exit() ends the case, and the parent compares
 * the exit status and the ordered record of calls.
 *
 * The oracle is the arming contract: the boot ROM loader (RB_BOOTLOADER)
 * is reached only by root with -y, a refusal happens before any syslog,
 * sync or signal, and every other mode keeps its flags.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int discobsd_reboot_main(int argc, char **argv);

static int logfd;
static int test_euid;

static void
record(const char *s)
{
	if (write(logfd, s, strlen(s)) < 0)
		_exit(99);
}

int
test_reboot(int howto)
{
	char b[32];

	(void)snprintf(b, sizeof b, "R%x;", (unsigned)howto);
	record(b);
	_exit(0);
}

int
test_kill(pid_t pid, int sig)
{
	record("K;");
	if (pid == -1 && sig == SIGKILL) {
		errno = ESRCH;
		return -1;
	}
	return 0;
}

int
test_sync(void)
{
	record("S;");
	return 0;
}

unsigned
test_sleep(unsigned n)
{
	(void)n;
	return 0;
}

uid_t
test_geteuid(void)
{
	return (uid_t)test_euid;
}

int
test_open(const char *path, int flags, ...)
{
	(void)path;
	(void)flags;
	return -1;
}

void
test_openlog(const char *ident, int opt, int fac)
{
	(void)ident;
	(void)opt;
	(void)fac;
	record("L;");
}

void
test_syslog(int pri, const char *fmt, ...)
{
	(void)pri;
	(void)fmt;
	record("L;");
}

char *
test_getlogin(void)
{
	return "root";
}

struct tcase {
	const char *label;
	const char *argv0;
	const char *opt1;
	const char *opt2;
	int euid;
	int status;		/* expected exit status */
	const char *calls;	/* expected record, or NULL to test the tail */
	const char *tail;	/* when calls is NULL: the record ends here */
};

static const struct tcase cases[] = {
	/* The arming contract. */
	{ "bootloader unarmed", "bootloader", NULL, NULL, 0, 64, "", NULL },
	{ "reboot -B unarmed", "reboot", "-B", NULL, 0, 64, "", NULL },
	{ "bootloader -y as user", "bootloader", "-y", NULL, 1000, 77, "",
	    NULL },
	{ "reboot -B -y as user", "reboot", "-B", "-y", 1000, 77, "", NULL },
	{ "bootloader as user, unarmed", "bootloader", NULL, NULL, 1000, 77,
	    "", NULL },
	{ "bootloader -y as root", "bootloader", "-y", NULL, 0, 0, NULL,
	    "R1008;" },
	{ "reboot -B -y as root", "reboot", "-B", "-y", 0, 0, NULL, "R1008;" },
	{ "reboot -y -B as root", "reboot", "-y", "-B", 0, 0, NULL, "R1008;" },
	/* Other modes keep their flags and do not need confirmation. */
	{ "reboot", "reboot", NULL, NULL, 0, 0, NULL, "R0;" },
	{ "reboot -y arms nothing", "reboot", "-y", NULL, 0, 0, NULL, "R0;" },
	{ "halt", "halt", NULL, NULL, 0, 0, NULL, "R8;" },
	{ "halt -y arms nothing", "halt", "-y", NULL, 0, 0, NULL, "R8;" },
	{ "reboot -q", "reboot", "-q", NULL, 0, 0, NULL, "R0;" },
};

static int
run(const struct tcase *c)
{
	int fds[2], status, argc = 1;
	char out[256];
	char *argv[4];
	ssize_t n;
	size_t len = 0;
	pid_t pid;

	(void)fflush(stdout);
	if (pipe(fds) != 0)
		return 1;
	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0) {
		(void)close(fds[0]);
		logfd = fds[1];
		test_euid = c->euid;
		argv[0] = (char *)c->argv0;
		if (c->opt1 != NULL)
			argv[argc++] = (char *)c->opt1;
		if (c->opt2 != NULL)
			argv[argc++] = (char *)c->opt2;
		argv[argc] = NULL;
		optind = 1;
		if (freopen("/dev/null", "w", stderr) == NULL ||
		    freopen("/dev/null", "w", stdout) == NULL)
			_exit(97);
		(void)discobsd_reboot_main(argc, argv);
		_exit(98);
	}
	(void)close(fds[1]);
	while (len < sizeof out - 1 &&
	    (n = read(fds[0], out + len, sizeof out - 1 - len)) > 0)
		len += (size_t)n;
	out[len] = '\0';
	(void)close(fds[0]);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
		(void)printf("FAIL %s: child ended abnormally (status %d, calls %s)\n",
		    c->label, status, out);
		return 1;
	}
	if (WEXITSTATUS(status) != c->status) {
		(void)printf("FAIL %s: exit %d, want %d (calls %s)\n", c->label,
		    WEXITSTATUS(status), c->status, out);
		return 1;
	}
	if (c->calls != NULL && strcmp(out, c->calls) != 0) {
		(void)printf("FAIL %s: calls \"%s\", want \"%s\"\n", c->label,
		    out, c->calls);
		return 1;
	}
	if (c->calls == NULL) {
		size_t lo = strlen(out), lt = strlen(c->tail);

		if (lo < lt || strcmp(out + lo - lt, c->tail) != 0) {
			(void)printf("FAIL %s: calls \"%s\" miss tail %s\n",
			    c->label, out, c->tail);
			return 1;
		}
	}
	(void)printf("ok   %s\n", c->label);
	return 0;
}

int
main(void)
{
	unsigned i;
	int failed = 0;

	for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
		failed += run(&cases[i]);
	if (failed != 0) {
		(void)printf("reboot contract: %d failed\n", failed);
		return 1;
	}
	(void)printf("reboot contract: all %u cases passed\n",
	    (unsigned)(sizeof cases / sizeof cases[0]));
	return 0;
}
