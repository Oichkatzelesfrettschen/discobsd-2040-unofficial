/*-
 * Copyright (c) 2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2026 DiscoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXIT_TIMEOUT 124
#define EXIT_INTERNAL 125
#define EXIT_CANNOT_INVOKE 126
#define EXIT_NOT_FOUND 127
#define MAX_DURATION_SECONDS 99999999L
#define USECONDS_PER_SECOND 1000000L

struct timeout_duration {
	struct timeval value;
	int nonzero;
};

static volatile sig_atomic_t alarm_pending;
static volatile sig_atomic_t signal_pending;

static const unsigned char signal_numbers[] = {
	SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT,
#ifdef SIGEMT
	SIGEMT,
#endif
	SIGFPE, SIGKILL, SIGBUS, SIGSEGV, SIGSYS, SIGPIPE, SIGALRM, SIGTERM,
	SIGURG, SIGSTOP, SIGTSTP, SIGCONT, SIGCHLD, SIGTTIN, SIGTTOU, SIGIO,
	SIGXCPU, SIGXFSZ, SIGVTALRM, SIGPROF, SIGWINCH, SIGUSR1, SIGUSR2
};

static const char signal_names[] =
	"HUP\0INT\0QUIT\0ILL\0TRAP\0ABRT\0"
#ifdef SIGEMT
	"EMT\0"
#endif
	"FPE\0KILL\0BUS\0SEGV\0SYS\0PIPE\0ALRM\0TERM\0URG\0STOP\0"
	"TSTP\0CONT\0CHLD\0TTIN\0TTOU\0IO\0XCPU\0XFSZ\0VTALRM\0PROF\0"
	"WINCH\0USR1\0USR2";

static const unsigned char terminating_signals[] = {
	SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT,
#ifdef SIGEMT
	SIGEMT,
#endif
	SIGFPE,
	SIGBUS, SIGSEGV, SIGSYS, SIGPIPE, SIGALRM, SIGTERM, SIGXCPU, SIGXFSZ,
	SIGVTALRM, SIGPROF, SIGUSR1, SIGUSR2
};

static int
usage(void)
{
	fputs("usage: timeout [-fp] [-k time] [-s signal] duration utility [args]\n",
	    stderr);
	return EXIT_INTERNAL;
}

static int
internal_error(void)
{
	fputs("timeout: error\n", stderr);
	return EXIT_INTERNAL;
}

static int
duration_suffix(int suffix, long *multiplier)
{
	switch (suffix) {
	case '\0':
	case 's':
		*multiplier = 1;
		return 0;
	case 'm':
		*multiplier = 60;
		return 0;
	case 'h':
		*multiplier = 60 * 60;
		return 0;
	case 'd':
		*multiplier = 60 * 60 * 24;
		return 0;
	default:
		return -1;
	}
}

static int
parse_duration(const char *text, struct timeout_duration *duration)
{
	const char *cursor;
	const char *fraction_start;
	const char *fraction_end;
	const char *reverse;
	unsigned long whole;
	long multiplier;
	long fraction_seconds;
	long fraction_useconds;
	int digit_seen;
	int nonzero;

	cursor = text;
	if (*cursor == '+')
		cursor++;
	if (*cursor == '\0' || *cursor == '-')
		return -1;
	whole = 0;
	digit_seen = 0;
	nonzero = 0;
	while (*cursor >= '0' && *cursor <= '9') {
		unsigned int digit;

		digit = (unsigned int)(*cursor - '0');
		if (whole > (ULONG_MAX - digit) / 10)
			return -1;
		whole = whole * 10 + digit;
		digit_seen = 1;
		if (digit != 0)
			nonzero = 1;
		cursor++;
	}
	if (!digit_seen)
		return -1;

	fraction_start = cursor;
	fraction_end = cursor;
	if (*cursor == '.') {
		fraction_start = ++cursor;
		while (*cursor >= '0' && *cursor <= '9') {
			if (*cursor != '0')
				nonzero = 1;
			cursor++;
		}
		fraction_end = cursor;
		if (fraction_start == fraction_end)
			return -1;
	}
	if (duration_suffix((unsigned char)*cursor, &multiplier) < 0)
		return -1;
	if (*cursor != '\0' && cursor[1] != '\0')
		return -1;
	if (whole > (unsigned long)(MAX_DURATION_SECONDS / multiplier))
		return -1;

	fraction_seconds = 0;
	fraction_useconds = 0;
	reverse = fraction_end;
	while (reverse > fraction_start) {
		long combined_useconds;
		long seconds_before_division;
		unsigned int digit;

		digit = (unsigned int)(*--reverse - '0');
		seconds_before_division = fraction_seconds +
		    (long)digit * multiplier;
		combined_useconds = (seconds_before_division % 10) *
		    USECONDS_PER_SECOND + fraction_useconds;
		fraction_seconds = seconds_before_division / 10;
		fraction_useconds = combined_useconds / 10;
	}
	if (nonzero && whole == 0 && fraction_seconds == 0 &&
	    fraction_useconds == 0)
		fraction_useconds = 1;
	duration->value.tv_sec = (long)whole * multiplier + fraction_seconds;
	duration->value.tv_usec = fraction_useconds;
	if (duration->value.tv_sec > MAX_DURATION_SECONDS)
		return -1;
	duration->nonzero = nonzero;
	return 0;
}

static int
signal_name_equal(const char *left, const char *right)
{
	for (;;) {
		int left_byte;
		int right_byte;

		left_byte = (unsigned char)*left++;
		right_byte = (unsigned char)*right++;
		if (left_byte >= 'a' && left_byte <= 'z')
			left_byte -= 'a' - 'A';
		if (right_byte >= 'a' && right_byte <= 'z')
			right_byte -= 'a' - 'A';
		if (left_byte != right_byte)
			return 0;
		if (left_byte == '\0')
			return 1;
	}
}

static int
parse_signal(const char *name)
{
	const char *candidate;
	size_t index;

	if (*name == '\0')
		return -1;
	candidate = signal_names;
	for (index = 0; index < sizeof(signal_numbers); index++) {
		if (signal_name_equal(name, candidate))
			return signal_numbers[index];
		candidate += strlen(candidate) + 1;
	}
	return -1;
}

static void
signal_handler(int signal_number)
{
	if (signal_number == SIGALRM)
		alarm_pending = 1;
	else if (signal_number != SIGCHLD)
		signal_pending = signal_number;
}

static int
add_blocked_signal(sigset_t *blocked, int signal_number)
{
	if (signal_number == SIGKILL || signal_number == SIGSTOP)
		return 0;
	return sigaddset(blocked, signal_number);
}

static int
prepare_signal_mask(sigset_t *blocked, sigset_t *inherited_mask,
	int timeout_signal)
{
	size_t index;

	if (sigemptyset(blocked) < 0)
		return -1;
	for (index = 0;
	    index < sizeof(terminating_signals) / sizeof(terminating_signals[0]);
	    index++)
		if (add_blocked_signal(blocked, terminating_signals[index]) < 0)
			return -1;
	if (add_blocked_signal(blocked, SIGCHLD) < 0 ||
	    add_blocked_signal(blocked, timeout_signal) < 0 ||
	    add_blocked_signal(blocked, SIGTTIN) < 0 ||
	    add_blocked_signal(blocked, SIGTTOU) < 0)
		return -1;
	return sigprocmask(SIG_BLOCK, blocked, inherited_mask);
}

static int
install_handler(int signal_number, void (*handler)(int),
	struct sigaction *inherited_action)
{
	struct sigaction action;

	if (signal_number == SIGKILL || signal_number == SIGSTOP)
		return 0;
	action.sa_handler = handler;
	if (sigemptyset(&action.sa_mask) < 0)
		return -1;
	action.sa_flags = 0;
	return sigaction(signal_number, &action, inherited_action);
}

static int
install_parent_signals(int timeout_signal)
{
	size_t index;

	for (index = 0;
	    index < sizeof(terminating_signals) / sizeof(terminating_signals[0]);
	    index++)
		if (install_handler(terminating_signals[index], signal_handler,
		    NULL) < 0)
			return -1;
	return install_handler(SIGCHLD, signal_handler, NULL) < 0 ||
	    install_handler(timeout_signal, signal_handler, NULL) < 0 ||
	    install_handler(SIGTTIN, SIG_IGN, NULL) < 0 ||
	    install_handler(SIGTTOU, SIG_IGN, NULL) < 0 ? -1 : 0;
}

static int
prepare_parent_mask(sigset_t *parent_mask, const sigset_t *inherited_mask,
	int timeout_signal)
{
	size_t index;

	*parent_mask = *inherited_mask;
	for (index = 0;
	    index < sizeof(terminating_signals) / sizeof(terminating_signals[0]);
	    index++)
		if (sigdelset(parent_mask, terminating_signals[index]) < 0)
			return -1;
	if (sigdelset(parent_mask, SIGCHLD) < 0 ||
	    (timeout_signal != SIGKILL && timeout_signal != SIGSTOP &&
	    sigdelset(parent_mask, timeout_signal) < 0) ||
	    sigdelset(parent_mask, SIGTTIN) < 0 ||
	    sigdelset(parent_mask, SIGTTOU) < 0)
		return -1;
	return 0;
}

static int
restore_child_signals(const sigset_t *inherited_mask, int timeout_signal)
{
	struct sigaction action;

	if (timeout_signal != SIGKILL && timeout_signal != SIGSTOP) {
		action.sa_handler = SIG_DFL;
		if (sigemptyset(&action.sa_mask) < 0)
			return -1;
		action.sa_flags = 0;
		if (sigaction(timeout_signal, &action, NULL) < 0)
			return -1;
	}
	return sigprocmask(SIG_SETMASK, inherited_mask, NULL);
}

static int
arm_timer(const struct timeout_duration *duration)
{
	struct itimerval timer;

	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;
	timer.it_value = duration->value;
	return setitimer(ITIMER_REAL, &timer, NULL);
}

static int
disarm_timer(void)
{
	struct timeout_duration zero;

	zero.value.tv_sec = 0;
	zero.value.tv_usec = 0;
	zero.nonzero = 0;
	return arm_timer(&zero);
}

static int
send_to_utility(pid_t child_pid, int foreground, int signal_number)
{
	struct sigaction ignored_action;
	struct sigaction saved_action;
	sigset_t signal_mask;
	pid_t destination;
	int saved_errno;
	int send_result;

	destination = foreground ? child_pid : -getpid();
	if (!foreground && signal_number != SIGKILL && signal_number != SIGSTOP) {
		ignored_action.sa_handler = SIG_IGN;
		if (sigemptyset(&ignored_action.sa_mask) < 0)
			return -1;
		ignored_action.sa_flags = 0;
		if (sigaction(signal_number, &ignored_action, &saved_action) < 0)
			return -1;
	}
	send_result = kill(destination, signal_number);
	saved_errno = errno;
	if (!foreground && signal_number != SIGKILL && signal_number != SIGSTOP) {
		if (sigemptyset(&signal_mask) < 0 ||
		    sigaddset(&signal_mask, signal_number) < 0 ||
		    sigprocmask(SIG_UNBLOCK, &signal_mask, NULL) < 0 ||
		    sigprocmask(SIG_BLOCK, &signal_mask, NULL) < 0 ||
		    sigaction(signal_number, &saved_action, NULL) < 0)
			return -1;
	}
	if (send_result == 0 || saved_errno == ESRCH)
		return 0;
	errno = saved_errno;
	return -1;
}

static int
start_escalation(pid_t child_pid, int foreground,
	const struct timeout_duration *kill_after, int have_kill_after)
{
	if (send_to_utility(child_pid, foreground, SIGCONT) < 0)
		return -1;
	if (!have_kill_after || !kill_after->nonzero)
		return 0;
	return arm_timer(kill_after);
}

static int
mimic_signal(int signal_number)
{
	struct rlimit no_core;
	struct sigaction action;
	sigset_t unblocked;

	no_core.rlim_cur = 0;
	no_core.rlim_max = 0;
	if (setrlimit(RLIMIT_CORE, &no_core) < 0) {
		return internal_error();
	}
	if (signal_number != SIGKILL && signal_number != SIGSTOP) {
		action.sa_handler = SIG_DFL;
		action.sa_flags = 0;
		if (sigemptyset(&action.sa_mask) < 0 ||
		    sigaction(signal_number, &action, NULL) < 0 ||
		    sigemptyset(&unblocked) < 0 ||
		    sigaddset(&unblocked, signal_number) < 0 ||
		    sigprocmask(SIG_UNBLOCK, &unblocked, NULL) < 0) {
			return internal_error();
		}
	}
	if (kill(getpid(), signal_number) < 0)
		return internal_error();
	return internal_error();
}

static int
monitor_child(pid_t child_pid, int foreground, int timeout_signal,
	const struct timeout_duration *duration,
	const struct timeout_duration *kill_after, int have_kill_after,
	int preserve_status, const sigset_t *inherited_mask)
{
	int escalation_armed;
	int deadline_expired;
	int first_signal_sent;
	int status;
	int timed_out;

	escalation_armed = 0;
	deadline_expired = 0;
	first_signal_sent = 0;
	timed_out = 0;
	if (duration->nonzero && arm_timer(duration) < 0)
		return internal_error();

	for (;;) {
		pid_t waited;

		waited = waitpid(child_pid, &status, WNOHANG | WUNTRACED);
		if (waited == child_pid) {
			if (!WIFSTOPPED(status))
				break;
			if (first_signal_sent &&
			    send_to_utility(child_pid, foreground, SIGCONT) < 0)
				return internal_error();
		} else if (waited < 0 && errno != EINTR) {
			return internal_error();
		}

		if (alarm_pending) {
			alarm_pending = 0;
			if (escalation_armed) {
				escalation_armed = 0;
				if (send_to_utility(child_pid, foreground, SIGKILL) < 0)
					return internal_error();
			} else if (!deadline_expired) {
				deadline_expired = 1;
				timed_out = 1;
				first_signal_sent = 1;
				if (send_to_utility(child_pid, foreground,
				    timeout_signal) < 0 ||
				    start_escalation(child_pid, foreground, kill_after,
				    have_kill_after) < 0)
					return internal_error();
				escalation_armed = have_kill_after && kill_after->nonzero;
			}
		}
		if (signal_pending) {
			int delivered_signal;

			delivered_signal = signal_pending;
			signal_pending = 0;
			if (send_to_utility(child_pid, foreground,
			    delivered_signal) < 0)
				return internal_error();
			if (!first_signal_sent) {
				first_signal_sent = 1;
				if (start_escalation(child_pid, foreground, kill_after,
				    have_kill_after) < 0)
					return internal_error();
				escalation_armed = have_kill_after && kill_after->nonzero;
			}
		}
		if (sigsuspend(inherited_mask) < 0 && errno != EINTR)
			return internal_error();
	}
	(void)disarm_timer();
	if (timed_out && !preserve_status)
		return EXIT_TIMEOUT;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return mimic_signal(WTERMSIG(status));
	return internal_error();
}

int
main(int argc, char **argv)
{
	struct timeout_duration duration;
	struct timeout_duration kill_after;
	struct sigaction inherited_sigchld;
	sigset_t blocked_signals;
	sigset_t inherited_mask;
	sigset_t parent_mask;
	pid_t child_pid;
	int foreground;
	int have_kill_after;
	int option;
	int preserve_status;
	int timeout_signal;

	foreground = 0;
	have_kill_after = 0;
	preserve_status = 0;
	timeout_signal = SIGTERM;
	kill_after.value.tv_sec = 0;
	kill_after.value.tv_usec = 0;
	kill_after.nonzero = 0;

	while ((option = getopt(argc, argv, "fk:ps:")) != -1) {
		switch (option) {
		case 'f':
			foreground = 1;
			break;
		case 'k':
			if (parse_duration(optarg, &kill_after) < 0) {
				fputs("timeout: invalid operand\n", stderr);
				return EXIT_INTERNAL;
			}
			have_kill_after = 1;
			break;
		case 'p':
			preserve_status = 1;
			break;
		case 's':
			timeout_signal = parse_signal(optarg);
			if (timeout_signal < 0) {
				fputs("timeout: invalid operand\n", stderr);
				return EXIT_INTERNAL;
			}
			break;
		default:
			return usage();
		}
	}
	if (argc - optind < 2)
		return usage();
	if (parse_duration(argv[optind], &duration) < 0) {
		fputs("timeout: invalid operand\n", stderr);
		return EXIT_INTERNAL;
	}
	optind++;

	if (prepare_signal_mask(&blocked_signals, &inherited_mask,
	    timeout_signal) < 0) {
		return internal_error();
	}
	if (install_handler(SIGCHLD, signal_handler, &inherited_sigchld) < 0)
		return internal_error();
	if (!foreground && setpgid(0, 0) < 0)
		return internal_error();
	child_pid = fork();
	if (child_pid < 0) {
		return internal_error();
	}
	if (child_pid == 0) {
		int exec_error;

		if (sigaction(SIGCHLD, &inherited_sigchld, NULL) < 0 ||
		    restore_child_signals(&inherited_mask, timeout_signal) < 0)
			_exit(EXIT_INTERNAL);
		execvp(argv[optind], &argv[optind]);
		exec_error = errno;
		perror(argv[optind]);
		_exit(exec_error == ENOENT ? EXIT_NOT_FOUND : EXIT_CANNOT_INVOKE);
	}
	if (install_parent_signals(timeout_signal) < 0) {
		(void)kill(child_pid, SIGKILL);
		(void)waitpid(child_pid, NULL, 0);
		return internal_error();
	}
	if (prepare_parent_mask(&parent_mask, &inherited_mask, timeout_signal) < 0) {
		(void)kill(child_pid, SIGKILL);
		(void)waitpid(child_pid, NULL, 0);
		return internal_error();
	}
	return monitor_child(child_pid, foreground, timeout_signal, &duration,
	    &kill_after, have_kill_after, preserve_status, &parent_mask);
}
