#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

int discobsd_raise(int);
char *discobsd_ctermid(char *);

static pid_t process_id = 4242;
static pid_t observed_process_id;
static int observed_signal;
static int kill_result;

pid_t
test_getpid(void)
{
	return process_id;
}

int
test_kill(pid_t target_process_id, int signal_number)
{
	observed_process_id = target_process_id;
	observed_signal = signal_number;
	if (kill_result < 0)
		errno = EPERM;
	return kill_result;
}

static void
fail(const char *message)
{
	fprintf(stderr, "libc contracts test: %s\n", message);
	exit(1);
}

static void
require(int condition, const char *message)
{
	if (!condition)
		fail(message);
}

int
main(void)
{
	char terminal_buffer[L_ctermid];
	char *static_terminal;

	kill_result = 0;
	require(discobsd_raise(SIGUSR1) == 0, "raise success result");
	require(observed_process_id == process_id && observed_signal == SIGUSR1,
	    "raise target");
	kill_result = -1;
	errno = 0;
	require(discobsd_raise(SIGTERM) == -1 && errno == EPERM,
	    "raise failure propagation");
	require(observed_process_id == process_id && observed_signal == SIGTERM,
	    "raise failure target");

	memset(terminal_buffer, 'x', sizeof(terminal_buffer));
	require(discobsd_ctermid(terminal_buffer) == terminal_buffer,
	    "ctermid caller buffer result");
	require(strcmp(terminal_buffer, "/dev/tty") == 0,
	    "ctermid caller buffer value");
	static_terminal = discobsd_ctermid(NULL);
	require(static_terminal != NULL && strcmp(static_terminal, "/dev/tty") == 0,
	    "ctermid static result");
	static_terminal[0] = 'x';
	require(strcmp(discobsd_ctermid(NULL), "/dev/tty") == 0,
	    "ctermid static restoration");

	puts("libc contracts tests passed");
	return 0;
}
