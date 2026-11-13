/*
 * Contract gate for init's daemon table, sbin/init/daemons.c: the
 * /etc/daemons parser, the backoff and capped restart policies with the
 * healthy-run reset, the argv split and the console line. daemons.c makes
 * no system call, so it compiles from the tree against the tree's headers
 * and runs here unchanged.
 */

#include <stdio.h>
#include <string.h>

#include "daemons.h"

static int failures, checks;

#define CHECK(e) do { checks++; if (!(e)) { failures++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); } } while (0)

/* wait(2) status words in sys/sys/wait.h's union wait layout. */
#define	EXITED(n)	((n) << 8)
#define	SIGNALED(n)	(n)

static void
parses_the_shipped_update_entry(void)
{
	struct dtab d;

	CHECK(dtab_parse("/update -f\t1:1\t/var/update\tbackoff\n", &d) == 1);
	CHECK(strcmp(d.cmd, "/update -f") == 0);
	CHECK(strcmp(d.root, "/var/update") == 0);
	CHECK(d.uid == 1 && d.gid == 1);
	CHECK(d.policy == DPOLICY_BACKOFF);
	CHECK(d.pid == 0 && d.fails == 0 && d.disabled == 0);
}

static void
parses_fields_from_the_right(void)
{
	struct dtab d;

	CHECK(dtab_parse("  /usr/sbin/x  -a   -b  32767:31 - capped", &d) == 1);
	CHECK(strcmp(d.cmd, "/usr/sbin/x -a -b") == 0);
	CHECK(d.root[0] == '\0');
	CHECK(d.uid == 32767 && d.gid == 31);
	CHECK(d.policy == DPOLICY_CAPPED);
	CHECK(dtab_parse("/x 0:0 - backoff\r\n", &d) == 1);
	CHECK(d.uid == 0 && d.gid == 0);
	CHECK(dtab_parse("/x 65535:65535 - backoff", &d) == 1);
}

static void
skips_blank_and_comment_lines(void)
{
	struct dtab d;

	CHECK(dtab_parse("", &d) == 0);
	CHECK(dtab_parse("\n", &d) == 0);
	CHECK(dtab_parse(" \t \n", &d) == 0);
	CHECK(dtab_parse("# /update -f 1:1 - backoff\n", &d) == 0);
	CHECK(dtab_parse("\t# comment\n", &d) == 0);
}

static void
rejects_malformed_lines(void)
{
	static const char *const bad[] = {
		"/x 1:1 - restart",		/* unknown policy */
		"/x 1:1 -",			/* missing field */
		"/x 1:1 backoff",
		"1:1 - backoff",		/* no command */
		"x 1:1 - backoff",		/* relative command */
		"/x 1:1 var/empty backoff",	/* relative chroot */
		"/x 1 - backoff",		/* no gid */
		"/x 1: - backoff",
		"/x :1 - backoff",
		"/x 1:1:1 - backoff",
		"/x a:1 - backoff",
		"/x 1:-1 - backoff",
		"/x 65536:1 - backoff",		/* uid out of range */
		"/x 1:100000 - backoff",
		"/x 1:1 - backoffs",
		"/x 1:1 -- backoff",
	};
	struct dtab d;
	size_t i;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		checks++;
		if (dtab_parse(bad[i], &d) != -1) {
			failures++;
			printf("FAIL %s:%d: accepted \"%s\"\n", __FILE__,
			    __LINE__, bad[i]);
		}
	}
}

/* An overlong command or chroot is refused, never truncated. */
static void
rejects_fields_that_do_not_fit(void)
{
	char line[512];
	struct dtab d;
	size_t n;

	n = DCMDSIZ - 1;
	line[0] = '/';
	memset(line + 1, 'c', n - 1);
	strcpy(line + n, " 1:1 - backoff");
	CHECK(dtab_parse(line, &d) == 1);
	CHECK(strlen(d.cmd) == DCMDSIZ - 1);
	line[0] = '/';
	memset(line + 1, 'c', n);
	strcpy(line + n + 1, " 1:1 - backoff");
	CHECK(dtab_parse(line, &d) == -1);

	n = DROOTSIZ - 1;
	strcpy(line, "/x 1:1 /");
	memset(line + 8, 'r', n - 1);
	strcpy(line + 8 + n - 1, " backoff");
	CHECK(dtab_parse(line, &d) == 1);
	CHECK(strlen(d.root) == DROOTSIZ - 1);
	strcpy(line, "/x 1:1 /");
	memset(line + 8, 'r', n);
	strcpy(line + 8 + n, " backoff");
	CHECK(dtab_parse(line, &d) == -1);
}

static void
compares_command_and_confinement(void)
{
	struct dtab a, b;

	dtab_parse("/x -f 1:1 /r backoff", &a);
	dtab_parse("/x  -f\t1:1 /r backoff", &b);
	CHECK(dtab_same(&a, &b));
	dtab_parse("/x -f 1:2 /r backoff", &b);
	CHECK(!dtab_same(&a, &b));
	dtab_parse("/x -f 1:1 - backoff", &b);
	CHECK(!dtab_same(&a, &b));
	dtab_parse("/x -f 1:1 /r capped", &b);
	CHECK(!dtab_same(&a, &b));
	dtab_parse("/x 1:1 /r backoff", &b);
	CHECK(!dtab_same(&a, &b));
}

static void
splits_argv_in_place(void)
{
	char buf[DCMDSIZ], *argv[DNARGS];
	int i;

	strcpy(buf, "/update -f");
	CHECK(dtab_argv(buf, argv) == 2);
	CHECK(strcmp(argv[0], "/update") == 0);
	CHECK(strcmp(argv[1], "-f") == 0);
	CHECK(argv[2] == 0);

	/* More words than slots: the last slot stays the null terminator. */
	strcpy(buf, "/x");
	for (i = 0; i < DNARGS + 4; i++)
		strcat(buf, " a");
	CHECK(dtab_argv(buf, argv) == DNARGS - 1);
	CHECK(argv[DNARGS - 1] == 0);
}

static void
backoff_doubles_to_the_cap(void)
{
	static const int expect[] = { 1, 2, 4, 8, 16, 32, 60, 60, 60 };
	struct dtab d;
	time_t now = 1000;
	size_t i;

	dtab_parse("/x 1:1 - backoff", &d);
	for (i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
		d.started = now;
		now += 2;
		checks++;
		if (dtab_exited(&d, now) != expect[i]) {
			failures++;
			printf("FAIL %s:%d: exit %d delay %ld, expected %d\n",
			    __FILE__, __LINE__, (int)i + 1,
			    (long)(d.next_try - now), expect[i]);
		}
		CHECK(d.pid == -1 && !d.disabled);
		CHECK(d.next_try == now + expect[i]);
		now = d.next_try;
	}
}

static void
healthy_run_clears_the_failure_count(void)
{
	struct dtab d;

	dtab_parse("/x 1:1 - backoff", &d);
	d.fails = 5;
	d.started = 100;
	CHECK(dtab_exited(&d, 100 + DHEALTHY_RUN - 1) == 32);
	CHECK(d.fails == 6);
	d.started = 1000;
	CHECK(dtab_exited(&d, 1000 + DHEALTHY_RUN) == 1);
	CHECK(d.fails == 1);

	dtab_parse("/x 1:1 - capped", &d);
	d.fails = DCAPPED_TRIES;
	d.started = 0;
	CHECK(dtab_exited(&d, DHEALTHY_RUN) == DCAPPED_DELAY);
	CHECK(d.fails == 1 && !d.disabled);
}

static void
capped_disables_after_its_tries(void)
{
	struct dtab d;
	time_t now = 50;
	int i;

	dtab_parse("/x 1:1 - capped", &d);
	for (i = 1; i <= DCAPPED_TRIES; i++) {
		d.started = now;
		CHECK(dtab_exited(&d, now + 1) == DCAPPED_DELAY);
		CHECK(d.pid == -1 && !d.disabled);
		now += 1 + DCAPPED_DELAY;
	}
	d.started = now;
	CHECK(dtab_exited(&d, now + 1) == -1);
	CHECK(d.disabled && d.pid == 0);
}

static void
describes_each_outcome(void)
{
	char buf[DCMDSIZ + 64];
	struct dtab d;

	dtab_parse("/update -f 1:1 /var/update backoff", &d);
	dtab_describe(buf, sizeof(buf), &d, EXITED(1), 2);
	CHECK(strcmp(buf, "init: /update -f: exited 1; restart in 2 s\n") == 0);
	dtab_describe(buf, sizeof(buf), &d, SIGNALED(9), 60);
	CHECK(strcmp(buf,
	    "init: /update -f: killed by signal 9; restart in 60 s\n") == 0);
	dtab_describe(buf, sizeof(buf), &d, -1, 1);
	CHECK(strcmp(buf, "init: /update -f: cannot fork; restart in 1 s\n") == 0);
	dtab_describe(buf, sizeof(buf), &d, EXITED(127), -1);
	CHECK(strcmp(buf,
	    "init: /update -f: exited 127; disabled until SIGHUP\n") == 0);
}

int
main(void)
{
	parses_the_shipped_update_entry();
	parses_fields_from_the_right();
	skips_blank_and_comment_lines();
	rejects_malformed_lines();
	rejects_fields_that_do_not_fit();
	compares_command_and_confinement();
	splits_argv_in_place();
	backoff_doubles_to_the_cap();
	healthy_run_clears_the_failure_count();
	capped_disables_after_its_tries();
	describes_each_outcome();
	if (failures) {
		printf("init daemon table: %d of %d checks failed\n", failures,
		    checks);
		return 1;
	}
	printf("init daemon table: %d checks passed\n", checks);
	return 0;
}
