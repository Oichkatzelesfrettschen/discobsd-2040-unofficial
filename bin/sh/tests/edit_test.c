/*
 * Host smoke test for bin/sh/edit.c: feeds scripted raw byte
 * sequences (arrow/Home/End/Tab escapes included) through the exact
 * shipped editline() and asserts the resulting line.  No pty: a
 * plain pipe supplies fdin, since editline() reads one byte at a
 * time regardless of what fdin is, and /dev/null discards output --
 * the point is the edit-buffer state machine, not the terminal.
 * isatty() on a pipe is false, so tty_raw() is a no-op here exactly
 * as it is on the device whenever input is redirected (see main.c's
 * isatty gate) -- this test exercises the same fallback-safe code
 * path, not a mocked copy of it.
 *
 * Build and run: `make host && ./edit_test-host`, or `make test`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "../edit.h"

static int failures = 0;
static int devnull;

static int
feed(const char *path, const unsigned char *script, int scriptlen,
    char *buf, int bufsz)
{
	int p[2];
	int n;

	if (pipe(p) != 0) {
		perror("pipe");
		exit(2);
	}
	if (write(p[1], script, scriptlen) != scriptlen) {
		perror("write");
		exit(2);
	}
	close(p[1]);

	n = editline(p[0], devnull, "$ ", path, buf, bufsz);

	close(p[0]);
	return n;
}

static void
check(const char *name, const char *path, const unsigned char *script,
    int scriptlen, const char *expect)
{
	char buf[256];
	int n, elen;

	memset(buf, 0, sizeof(buf));
	n = feed(path, script, scriptlen, buf, sizeof(buf));
	elen = expect ? (int)strlen(expect) : -1;

	if (n != elen || (expect && memcmp(buf, expect, elen) != 0)) {
		printf("FAIL %s: got %d:%.*s want %d:%s\n",
		    name, n, n > 0 ? n : 0, buf, elen, expect ? expect : "(EOF)");
		failures++;
	} else {
		printf("ok   %s\n", name);
	}
}

#define ESC "\033"

/*
 * A bare ESC (Esc key alone, no following bytes) must not eat the
 * next real keystroke.  This needs pacing that the all-at-once
 * feed() above cannot give: if every scripted byte is written to
 * the pipe before editline() starts, the bytes after ESC are always
 * already queued and the FIONREAD check in edit.c can never tell a
 * standalone ESC from the start of a real escape sequence.  A child
 * process writes "ab" + ESC, sleeps, then writes "c\n" -- so when
 * editline() polls FIONREAD right after reading the ESC, nothing is
 * pending yet, exactly like a person pressing Esc and pausing.
 */
static void
check_bare_esc(void)
{
	int p[2];
	pid_t pid;
	char buf[64];
	int n, status;

	if (pipe(p) != 0) {
		perror("pipe");
		exit(2);
	}

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0) {
		struct timespec ts;

		close(p[0]);
		write(p[1], "ab\033", 3);
		ts.tv_sec = 0;
		ts.tv_nsec = 50000000L; /* 50 ms */
		nanosleep(&ts, (struct timespec *)0);
		write(p[1], "c\n", 2);
		close(p[1]);
		_exit(0);
	}
	close(p[1]);

	memset(buf, 0, sizeof(buf));
	n = editline(p[0], devnull, "$ ", ".", buf, sizeof(buf));
	close(p[0]);
	waitpid(pid, &status, 0);

	if (n == 3 && memcmp(buf, "abc", 3) == 0) {
		printf("ok   bare-esc\n");
	} else {
		printf("FAIL bare-esc: got %d:%.*s want 3:abc\n",
		    n, n > 0 ? n : 0, buf);
		failures++;
	}
}

int
main(void)
{
	unsigned char script[256];
	int n;
	char tmpl[] = "/tmp/sh_edit_test.XXXXXX";
	char *dir;
	char path[512];
	int fd;

	devnull = open("/dev/null", O_WRONLY);
	if (devnull < 0) {
		perror("/dev/null");
		return 2;
	}

	/* 1. plain insert + Enter */
	check("insert", ".", (unsigned char *)"echo hi\n", 8, "echo hi");

	/* 2. Backspace (DEL 0177) removes the last typed char */
	check("backspace", ".", (unsigned char *)"echo hii\177\n", 10, "echo hi");

	/* 3. Left, Left, insert -- cursor motion inserts mid-line */
	n = 0;
	memcpy(script + n, "echo h", 6); n += 6;
	memcpy(script + n, ESC "[D" ESC "[D", 6); n += 6;
	script[n++] = 'X';
	script[n++] = '\n';
	check("cursor-left-insert", ".", script, n, "echoX h");

	/* 4. Ctrl-A (home) then insert, Ctrl-E is exercised implicitly
	 * by cursor already sitting at end afterwards */
	n = 0;
	memcpy(script + n, "bcd", 3); n += 3;
	script[n++] = 001;            /* Ctrl-A */
	script[n++] = 'a';
	script[n++] = '\n';
	check("ctrl-a-home", ".", script, n, "abcd");

	/* 5. Right arrow after Home, then Delete (forward, ESC[3~) */
	n = 0;
	memcpy(script + n, "abd", 3); n += 3;
	script[n++] = 001;             /* Home */
	memcpy(script + n, ESC "[C", 3); n += 3;   /* Right: cursor after 'a' */
	memcpy(script + n, ESC "[3~", 4); n += 4;  /* forward-delete 'b' */
	script[n++] = 'b';
	script[n++] = '\n';
	check("delete-fwd", ".", script, n, "abd");

	/* 6. History: Up recalls the previous line into a fresh call --
	 * the ring is process-static, so back-to-back feed() calls see it. */
	{
		char buf1[64];
		char buf2[64];
		int n1, n2;

		n1 = feed(".", (unsigned char *)"ls -la\n", 7, buf1, sizeof(buf1));
		memcpy(script, ESC "[A" "\n", 4);
		n2 = feed(".", script, 4, buf2, sizeof(buf2));
		if (n1 == 6 && n2 == 6 && memcmp(buf1, buf2, 6) == 0)
			printf("ok   history-up\n");
		else {
			printf("FAIL history-up: first=%.*s second=%.*s\n",
			    n1, buf1, n2, buf2);
			failures++;
		}
	}

	/* 6b. History does not truncate a long recalled line: this must
	 * hold up to ED_MAX (126); a shorter regression bound (110)
	 * is enough to catch HIST_COLS < ED_MAX+1. */
	{
		char long_line[112];
		char buf1[160];
		char buf2[160];
		int n1, n2, i;

		for (i = 0; i < 110; i++)
			long_line[i] = 'a' + (i % 26);
		long_line[110] = '\n';

		n1 = feed(".", (unsigned char *)long_line, 111, buf1, sizeof(buf1));
		memcpy(script, ESC "[A" "\n", 4);
		n2 = feed(".", script, 4, buf2, sizeof(buf2));
		if (n1 == 110 && n2 == 110 && memcmp(buf1, buf2, 110) == 0)
			printf("ok   history-no-truncate\n");
		else {
			printf("FAIL history-no-truncate: first=%d second=%d\n", n1, n2);
			failures++;
		}
	}

	/* 7. Ctrl-D on an empty line is EOF */
	check("eof-empty", ".", (unsigned char *)"\004", 1, (char *)0);

	/* 7b. A standalone ESC does not eat the next keystroke */
	check_bare_esc();

	/* 8. Filename completion: build a small tmpdir with two names
	 * sharing a prefix and one that does not. */
	dir = mkdtemp(tmpl);
	if (!dir) {
		perror("mkdtemp");
		return 2;
	}
	snprintf(path, sizeof(path), "%s/alpha.txt", dir);
	fd = open(path, O_CREAT | O_WRONLY, 0644); close(fd);
	snprintf(path, sizeof(path), "%s/alpha2.txt", dir);
	fd = open(path, O_CREAT | O_WRONLY, 0644); close(fd);
	snprintf(path, sizeof(path), "%s/beta.txt", dir);
	fd = open(path, O_CREAT | O_WRONLY, 0644); close(fd);

	if (chdir(dir) != 0) {
		perror("chdir");
		return 2;
	}

	/* ambiguous: "al" -> common prefix "alpha" (unlisted third char differs) */
	check("complete-common-prefix", ".",
	    (unsigned char *)"ls al\t\n", 7, "ls alpha");

	/* unique: "beta" -> "beta.txt" */
	check("complete-unique", ".",
	    (unsigned char *)"ls beta\t\n", 9, "ls beta.txt");

	/* 9. Command-name completion against PATH */
	snprintf(path, sizeof(path), "%s/pingbin", dir);
	fd = open(path, O_CREAT | O_WRONLY, 0755); close(fd);
	check("complete-path-command", dir,
	    (unsigned char *)"pin\t\n", 5, "pingbin");

	printf(failures ? "%d test(s) FAILED\n" : "all tests passed\n", failures);
	return failures ? 1 : 0;
}
