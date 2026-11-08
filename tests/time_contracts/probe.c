/*
 * Child program for the time contract: exits with a chosen status or ends
 * itself with a chosen signal at its default disposition.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	struct rlimit no_core = { 0, 0 };
	sigset_t all;
	int number;

	if (argc == 3 && strcmp(argv[1], "exit") == 0) {
		fputs("child output\n", stdout);
		return atoi(argv[2]);
	}
	if (argc == 3 && strcmp(argv[1], "signal") == 0) {
		number = atoi(argv[2]);
		(void)setrlimit(RLIMIT_CORE, &no_core);
		(void)signal(number, SIG_DFL);
		(void)sigemptyset(&all);
		(void)sigprocmask(SIG_SETMASK, &all, NULL);
		(void)raise(number);
		_exit(99);
	}
	fputs("usage: probe exit STATUS | probe signal NUMBER\n", stderr);
	return 98;
}
