#define _POSIX_C_SOURCE 200809L

#include <sys/time.h>
#include <sys/types.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int marker_fd = -1;
static char marker_byte;

static void
mark_and_exit(int signal_number)
{
	ssize_t written;

	(void)signal_number;
	written = write(marker_fd, &marker_byte, 1);
	(void)written;
	_exit(marker_byte == 'I' ? 42 : 0);
}

static void
mark_only(int signal_number)
{
	char delivered_byte;
	ssize_t written;

	delivered_byte = signal_number == SIGTERM ? 'T' : 'C';
	written = write(marker_fd, &delivered_byte, 1);
	(void)written;
	if (signal_number == SIGCONT)
		(void)kill(getppid(), SIGCHLD);
}

static void
exit_success(int signal_number)
{
	(void)signal_number;
	_exit(0);
}

static int
open_marker(const char *path)
{
	int descriptor;

	descriptor = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (descriptor < 0)
		perror(path);
	return descriptor;
}

static void
wait_forever(void)
{
	for (;;)
		pause();
}

static int
write_child_pid(const char *path, pid_t child_pid)
{
	char pid_path[512];
	char pid_text[64];
	int descriptor;
	int length;
	ssize_t written;

	length = snprintf(pid_path, sizeof(pid_path), "%s.pid", path);
	if (length < 0 || (size_t)length >= sizeof(pid_path))
		return -1;
	descriptor = open(pid_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor < 0)
		return -1;
	length = snprintf(pid_text, sizeof(pid_text), "%ld\n", (long)child_pid);
	if (length < 0 || (size_t)length >= sizeof(pid_text)) {
		close(descriptor);
		return -1;
	}
	written = write(descriptor, pid_text, (size_t)length);
	close(descriptor);
	return written == length ? 0 : -1;
}

static int
record_process_id(const char *path)
{
	return write_child_pid(path, getpid());
}

static int
record_group_ids(const char *path)
{
	char identifier_text[96];
	int descriptor;
	int length;
	ssize_t written;

	descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (descriptor < 0)
		return -1;
	length = snprintf(identifier_text, sizeof(identifier_text), "%ld %ld %ld\n",
	    (long)getpid(), (long)getppid(), (long)getpgrp());
	if (length < 0 || (size_t)length >= sizeof(identifier_text)) {
		close(descriptor);
		return -1;
	}
	written = write(descriptor, identifier_text, (size_t)length);
	close(descriptor);
	return written == length ? 0 : -1;
}

int
main(int argc, char **argv)
{
	pid_t child_pid;
	int signal_number;

	if (argc < 2)
		return 2;
	if (strcmp(argv[1], "exit") == 0 && argc == 3)
		return atoi(argv[2]);
	if (strcmp(argv[1], "signal") == 0 && argc == 3) {
		signal_number = atoi(argv[2]);
		return raise(signal_number) == 0 ? 3 : 4;
	}
	if (strcmp(argv[1], "ignore-chld-exec") == 0 && argc >= 3) {
		struct sigaction action;

		action.sa_handler = SIG_IGN;
		if (sigemptyset(&action.sa_mask) < 0)
			return 38;
		action.sa_flags = 0;
		if (sigaction(SIGCHLD, &action, NULL) < 0)
			return 39;
		execv(argv[2], &argv[2]);
		return 40;
	}
	if (strcmp(argv[1], "pause") == 0 && argc == 2) {
		wait_forever();
		return 5;
	}
	if (strcmp(argv[1], "ignore-term") == 0 && argc == 2) {
		if (signal(SIGTERM, SIG_IGN) == SIG_ERR)
			return 6;
		wait_forever();
		return 7;
	}
	if (strcmp(argv[1], "stop") == 0 && argc == 2) {
		if (raise(SIGSTOP) != 0)
			return 8;
		wait_forever();
		return 9;
	}
	if (strcmp(argv[1], "pause-pid") == 0 && argc == 3) {
		if (record_process_id(argv[2]) != 0)
			return 22;
		wait_forever();
		return 23;
	}
	if (strcmp(argv[1], "stop-pid") == 0 && argc == 3) {
		if (record_process_id(argv[2]) != 0)
			return 24;
		if (raise(SIGSTOP) != 0)
			return 25;
		wait_forever();
		return 26;
	}
	if (strcmp(argv[1], "group-ids") == 0 && argc == 3) {
		if (record_group_ids(argv[2]) != 0)
			return 27;
		wait_forever();
		return 28;
	}
	if (strcmp(argv[1], "raise-usr1") == 0 && argc == 2) {
		if (raise(SIGUSR1) != 0)
			return 10;
		return 33;
	}
	if (strcmp(argv[1], "record-int") == 0 && argc == 3) {
		char ready_byte;
		sigset_t blocked_mask;
		sigset_t inherited_mask;

		marker_fd = open_marker(argv[2]);
		if (marker_fd < 0)
			return 11;
		if (sigemptyset(&blocked_mask) < 0 ||
		    sigaddset(&blocked_mask, SIGINT) < 0 ||
		    sigprocmask(SIG_BLOCK, &blocked_mask, &inherited_mask) < 0)
			return 12;
		marker_byte = 'I';
		if (signal(SIGINT, mark_and_exit) == SIG_ERR)
			return 13;
		ready_byte = 'R';
		if (write(marker_fd, &ready_byte, 1) != 1)
			return 14;
		if (sigprocmask(SIG_SETMASK, &inherited_mask, NULL) < 0)
			return 15;
		wait_forever();
		return 16;
	}
	if (strcmp(argv[1], "count-term") == 0 && argc == 3) {
		char ready_byte;
		struct sigaction action;
		struct itimerval timer;
		sigset_t blocked_mask;
		sigset_t inherited_mask;

		marker_fd = open_marker(argv[2]);
		if (marker_fd < 0)
			return 29;
		if (sigemptyset(&blocked_mask) < 0 ||
		    sigaddset(&blocked_mask, SIGTERM) < 0 ||
		    sigaddset(&blocked_mask, SIGCONT) < 0 ||
		    sigprocmask(SIG_BLOCK, &blocked_mask, &inherited_mask) < 0)
			return 30;
		action.sa_handler = mark_only;
		if (sigemptyset(&action.sa_mask) < 0)
			return 31;
		action.sa_flags = 0;
		if (sigaction(SIGTERM, &action, NULL) < 0 ||
		    sigaction(SIGCONT, &action, NULL) < 0)
			return 32;
		action.sa_handler = exit_success;
		if (sigaction(SIGALRM, &action, NULL) < 0)
			return 33;
		timer.it_interval.tv_sec = 0;
		timer.it_interval.tv_usec = 0;
		timer.it_value.tv_sec = 0;
		timer.it_value.tv_usec = 500000;
		if (setitimer(ITIMER_REAL, &timer, NULL) < 0)
			return 34;
		ready_byte = 'R';
		if (write(marker_fd, &ready_byte, 1) != 1)
			return 35;
		if (sigprocmask(SIG_SETMASK, &inherited_mask, NULL) < 0)
			return 36;
		wait_forever();
		return 37;
	}
	if (strcmp(argv[1], "tree") == 0 && argc == 3) {
		sigset_t blocked_mask;
		sigset_t inherited_mask;

		marker_fd = open_marker(argv[2]);
		if (marker_fd < 0)
			return 15;
		if (sigemptyset(&blocked_mask) < 0 ||
		    sigaddset(&blocked_mask, SIGTERM) < 0 ||
		    sigprocmask(SIG_BLOCK, &blocked_mask, &inherited_mask) < 0)
			return 16;
		marker_byte = 'P';
		if (signal(SIGTERM, mark_and_exit) == SIG_ERR)
			return 17;
		child_pid = fork();
		if (child_pid < 0)
			return 18;
		if (child_pid == 0) {
			marker_byte = 'C';
			if (sigprocmask(SIG_SETMASK, &inherited_mask, NULL) < 0)
				_exit(19);
			wait_forever();
			_exit(20);
		}
		if (write_child_pid(argv[2], child_pid) != 0)
			return 21;
		if (sigprocmask(SIG_SETMASK, &inherited_mask, NULL) < 0)
			return 22;
		wait_forever();
		return 23;
	}
	fprintf(stderr, "unknown probe mode\n");
	return 2;
}
