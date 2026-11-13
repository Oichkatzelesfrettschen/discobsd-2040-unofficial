/*
 * Contract gate for init's handling of a refused fork(): spawn(), runcom(),
 * single(), dfork() and wstart() in sbin/init/init.c, and the -1 restart
 * marker that term() and wterm() must never turn into kill(-1, ...).
 * init.c compiles from the tree against the tree's headers; the system
 * calls it makes are answered here from scripted results and recorded, so
 * no process is created and no signal is sent. The daemon supervisor's
 * fork, reap and alarm path (dstart(), dreap(), dschedule()) runs here on
 * the same doubles.
 */

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "daemons.h"

struct tab {
	char	line[8];
	char	comn[200];
	char	xflag;
	int	pid;
	int	wpid;
	char	wcmd[200];
	long	gettytime;
	int	gettycnt;
	long	windtime;
	int	windcnt;
	struct	tab *next;
};

extern int spawn(const char *);
extern int runcom(int);
extern void single(void);
extern void dfork(struct tab *);
extern void wstart(struct tab *);
extern void term(struct tab *);
extern void wterm(struct tab *);
extern struct dtab *dtab;
extern int didle;
extern void dstart(struct dtab *);
extern void dschedule(void);
extern int dreap(int, int);
extern volatile int dtimer;
extern void dalarm(int);
extern void dtimer_run(void);
extern void dconsole(const char *);

int test_errno;

#define SCRIPT 16
static int fork_script[SCRIPT], fork_count, fork_calls;
static int wait_script[SCRIPT], wait_count, wait_calls;
static int sleep_calls, sleep_seconds, syslog_calls, kill_calls;
static int alarm_calls;
static unsigned alarm_last;
/* The order of process-group changes and opens, one letter each. */
static char trace[64];
static int kill_pid[SCRIPT], kill_sig[SCRIPT];
static int failures, checks;

#define CHECK(e) do { checks++; if (!(e)) { failures++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); } } while (0)

static void
reset(void)
{
	fork_count = fork_calls = wait_count = wait_calls = 0;
	sleep_calls = sleep_seconds = syslog_calls = kill_calls = 0;
	alarm_calls = 0;
	alarm_last = 0;
	trace[0] = '\0';
	test_errno = 0;
}

/* A script entry of -1 is a refusal with ENOMEM; past the end, refuse. */
int
test_fork(void)
{
	int pid = fork_calls < fork_count ? fork_script[fork_calls] : -1;

	fork_calls++;
	if (pid < 0)
		test_errno = ENOMEM;
	return pid;
}

int
test_wait(int *status)
{
	int pid;

	if (wait_calls >= wait_count) {
		wait_calls++;
		test_errno = ECHILD;
		return -1;
	}
	pid = wait_script[wait_calls++];
	if (pid < 0)
		test_errno = ECHILD;
	else if (status)
		*status = 0;
	return pid;
}

unsigned int
test_sleep(unsigned int seconds)
{
	sleep_calls++;
	sleep_seconds += (int)seconds;
	return 0;
}

int
test_kill(int pid, int sig)
{
	if (kill_calls < SCRIPT) {
		kill_pid[kill_calls] = pid;
		kill_sig[kill_calls] = sig;
	}
	kill_calls++;
	return 0;
}

void
test_syslog(int priority, const char *format, ...)
{
	(void)priority;
	(void)format;
	syslog_calls++;
}

void test_openlog(const char *i, int o, int f) { (void)i; (void)o; (void)f; }
void test_closelog(void) { }
static void
trace_add(const char *s)
{
	if (strlen(trace) + strlen(s) < sizeof(trace))
		strcat(trace, s);
}
int test_open(const char *p, int f, ...) { (void)p; (void)f; trace_add("o"); return -1; }
int test_setpgrp(pid_t p, pid_t g)
{
	trace_add(p == 0 && g == 0 ? "0" : p == 0 && g == 1 ? "1" : "?");
	return 0;
}
int test_creat(const char *p, int m) { (void)p; (void)m; return -1; }
int test_close(int d) { (void)d; return 0; }
int test_dup2(int a, int b) { (void)a; return b; }
long test_sigsetmask(long m) { return m; }
long test_sigblock(long m) { return m; }
int test_sigpause(long m) { (void)m; return -1; }
unsigned int test_alarm(unsigned int s) { alarm_calls++; alarm_last = s; return 0; }
int test_sysctl(int *n, unsigned l, void *o, size_t *ol, void *nw, size_t nl)
{
	(void)n; (void)l; (void)o; (void)ol; (void)nw; (void)nl;
	test_errno = EOPNOTSUPP;
	return -1;
}

void (*test_signal(int s, void (*h)(int)))(int)
{
	(void)s;
	(void)h;
	return SIG_DFL;
}

int test_sigvec(int s, void *v, void *o) { (void)s; (void)v; (void)o; return 0; }

/* Child branches never run: every scripted fork result is the parent's. */
int
test_execl(const char *p, const char *a, ...)
{
	printf("FAIL child branch reached execl(%s, %s)\n", p, a);
	exit(2);
}

int
test_execve(const char *p, char *const a[], char *const e[])
{
	(void)a;
	(void)e;
	printf("FAIL child branch reached execve(%s)\n", p);
	exit(2);
}

static void
script_fork(int n, const int *pids)
{
	memcpy(fork_script, pids, (size_t)n * sizeof(int));
	fork_count = n;
}

static void
script_wait(int n, const int *pids)
{
	memcpy(wait_script, pids, (size_t)n * sizeof(int));
	wait_count = n;
}

static void
spawn_gives_up_after_bounded_delayed_retries(void)
{
	reset();
	CHECK(spawn("test") == -1);
	CHECK(fork_calls == 6);
	CHECK(sleep_calls == 5);
	CHECK(sleep_seconds == 25);
	CHECK(syslog_calls == 1);
}

static void
spawn_returns_the_first_success(void)
{
	static const int pids[] = { -1, -1, 42 };

	reset();
	script_fork(3, pids);
	CHECK(spawn("test") == 42);
	CHECK(fork_calls == 3);
	CHECK(sleep_calls == 2);
	CHECK(syslog_calls == 1);
}

static void
runcom_refusal_reads_no_status(void)
{
	reset();
	CHECK(runcom(0) == 0);
	CHECK(wait_calls == 0);
}

static void
runcom_lost_child_is_a_failure(void)
{
	static const int pids[] = { 7 };

	reset();
	script_fork(1, pids);
	CHECK(runcom(0) == 0);
	CHECK(wait_calls == 1);
}

static void
runcom_success(void)
{
	static const int pids[] = { 7 };
	static const int reaped[] = { 3, 7 };

	reset();
	script_fork(1, pids);
	script_wait(2, reaped);
	CHECK(runcom(0) == 1);
	CHECK(wait_calls == 2);
}

static void
single_retries_until_a_shell_starts(void)
{
	static const int pids[] = { -1, -1, -1, -1, -1, -1, 9 };
	static const int reaped[] = { 9 };

	reset();
	script_fork(7, pids);
	script_wait(1, reaped);
	single();
	CHECK(fork_calls == 7);
	CHECK(wait_calls == 1);
	CHECK(sleep_calls == 5);
}

static void
refused_getty_leaves_the_restart_marker(void)
{
	struct tab t;

	memset(&t, 0, sizeof(t));
	strcpy(t.line, "console");
	strcpy(t.comn, "/usr/libexec/getty std.115200");
	reset();
	dfork(&t);
	CHECK(t.pid == -1);
	term(&t);
	CHECK(kill_calls == 0);
	CHECK(t.pid == 0);

	t.pid = 9;
	reset();
	term(&t);
	CHECK(kill_calls == 1);
	CHECK(kill_pid[0] == 9 && kill_sig[0] == SIGKILL);
}

static void
refused_window_system_leaves_the_restart_marker(void)
{
	struct tab t;

	memset(&t, 0, sizeof(t));
	strcpy(t.line, "console");
	strcpy(t.wcmd, "/usr/bin/window");
	reset();
	wstart(&t);
	CHECK(t.wpid == -1);
	term(&t);
	wterm(&t);
	CHECK(kill_calls == 0);
	CHECK(t.wpid == 0);
}

static void
refused_daemon_fork_backs_off_and_arms_the_alarm(void)
{
	struct dtab d;

	CHECK(dtab_parse("/update -f 1:1 /var/update backoff", &d) == 1);
	d.pid = -1;
	dtab = &d;
	reset();
	dschedule();
	CHECK(fork_calls == 1);
	CHECK(d.pid == -1 && d.fails == 1);
	CHECK(d.next_try > 0);
	CHECK(alarm_calls == 1 && alarm_last == 1);
	CHECK(sleep_calls == 0);
	dtab = 0;
}

static void
reaped_daemon_waits_for_its_backoff(void)
{
	static const int pids[] = { 7 };
	struct dtab d;

	CHECK(dtab_parse("/update -f 1:1 /var/update backoff", &d) == 1);
	d.pid = -1;
	dtab = &d;
	reset();
	script_fork(1, pids);
	dschedule();
	CHECK(d.pid == 7 && fork_calls == 1);
	CHECK(alarm_last == 0);

	CHECK(dreap(8, 0) == 0);
	CHECK(dreap(7, 9) == 1);
	CHECK(d.pid == -1 && d.fails == 1);
	/* The restart lies 1 s ahead: no fork now, an alarm for it. */
	CHECK(fork_calls == 1);
	CHECK(alarm_last == 1);
	dtab = 0;
}

/* The SIGALRM handler forks nothing; the main loop runs the restart. */
static void
alarm_handler_only_records_the_deadline(void)
{
	static const int pids[] = { 11 };
	struct dtab d;

	CHECK(dtab_parse("/update -f 1:1 /var/update backoff", &d) == 1);
	d.pid = -1;
	dtab = &d;
	reset();
	script_fork(1, pids);
	dalarm(SIGALRM);
	CHECK(fork_calls == 0);
	CHECK(dtimer == 1);
	/* The repeat alarm covers a deadline that lands just before wait(2). */
	CHECK(alarm_calls == 1 && alarm_last == 1);
	dtimer_run();
	CHECK(fork_calls == 1 && d.pid == 11);
	CHECK(dtimer == 0);
	/* With nothing pending, dschedule() cancels the repeat. */
	CHECK(alarm_last == 0);
	dtab = 0;
}

/*
 * init lives in process group 0, where ttyopen() would attach the console
 * and move init into the login shell's group; the console opens from group
 * 1 and init returns to group 0 before anything else runs.
 */
static void
console_message_leaves_init_detached(void)
{
	reset();
	dconsole("init: test\n");
	CHECK(strcmp(trace, "1o0") == 0);
}

static void
idle_starts_no_daemon(void)
{
	struct dtab d;

	CHECK(dtab_parse("/update -f 1:1 /var/update backoff", &d) == 1);
	d.pid = -1;
	dtab = &d;
	didle = 1;
	reset();
	dschedule();
	CHECK(fork_calls == 0);
	didle = 0;
	dtab = 0;
}

int
main(void)
{
	spawn_gives_up_after_bounded_delayed_retries();
	spawn_returns_the_first_success();
	runcom_refusal_reads_no_status();
	runcom_lost_child_is_a_failure();
	runcom_success();
	single_retries_until_a_shell_starts();
	refused_getty_leaves_the_restart_marker();
	refused_window_system_leaves_the_restart_marker();
	refused_daemon_fork_backs_off_and_arms_the_alarm();
	reaped_daemon_waits_for_its_backoff();
	alarm_handler_only_records_the_deadline();
	idle_starts_no_daemon();
	console_message_leaves_init_detached();
	if (failures) {
		printf("init fork refusal: %d of %d checks failed\n", failures,
		    checks);
		return 1;
	}
	printf("init fork refusal: %d checks passed\n", checks);
	return 0;
}
