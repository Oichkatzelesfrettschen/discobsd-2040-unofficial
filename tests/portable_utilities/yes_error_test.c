#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int discobsd_yes_main(int, char **);

static int stdout_calls;
static char diagnostic[32];

ssize_t
test_write(int descriptor, const void *buffer, size_t length)
{
	if (descriptor == STDOUT_FILENO) {
		++stdout_calls;
		if (stdout_calls == 1) {
			errno = EINTR;
			return -1;
		}
		errno = EPIPE;
		return -1;
	}
	if (descriptor == STDERR_FILENO) {
		if (length >= sizeof(diagnostic))
			return -1;
		memcpy(diagnostic, buffer, length);
		diagnostic[length] = '\0';
		return (ssize_t)length;
	}
	return -1;
}

int
main(void)
{
	char *arguments[] = { (char *)"yes", NULL };

	if (discobsd_yes_main(1, arguments) != 1)
		return 1;
	if (stdout_calls != 2)
		return 1;
	if (strcmp(diagnostic, "yes: write failed\n") != 0)
		return 1;
	puts("yes error tests passed");
	return 0;
}
