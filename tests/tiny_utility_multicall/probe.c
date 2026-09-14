#include <sys/resource.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	if (strcmp(argv[1], "arguments") == 0) {
		int argument_index;

		for (argument_index = 2; argument_index < argc; argument_index++)
			printf("%d:%zu:%s\n", argument_index - 2,
			    strlen(argv[argument_index]), argv[argument_index]);
		return 0;
	}
	if (strcmp(argv[1], "exit") == 0 && argc == 3)
		return atoi(argv[2]);
	if (strcmp(argv[1], "priority") == 0) {
		printf("%d\n", getpriority(PRIO_PROCESS, 0));
		return 0;
	}
	if (strcmp(argv[1], "priority-plus-five") == 0) {
		int priority;

		priority = getpriority(PRIO_PROCESS, 0);
		if (setpriority(PRIO_PROCESS, 0, priority + 5) < 0)
			return 2;
		printf("%d\n", getpriority(PRIO_PROCESS, 0));
		return 0;
	}
	if (strcmp(argv[1], "signals") == 0) {
		kill(getpid(), SIGHUP);
		kill(getpid(), SIGTERM);
		puts("signals survived");
		return 0;
	}
	if (strcmp(argv[1], "streams") == 0) {
		fputs("stdout\n", stdout);
		fflush(stdout);
		fputs("stderr\n", stderr);
		return 0;
	}
	return 2;
}
