/*
 * Stands in for wait3 in the program under test. TIME_TEST_WAIT3=eintr
 * fails the first call with EINTR and then waits for real;
 * TIME_TEST_WAIT3=echild fails every call with ECHILD and ends the process
 * with status 99 after 100 calls, so a caller that retries on any error
 * is observed instead of hanging. Without the variable the call passes
 * through to the host wait3.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int
time_test_wait3(int *status, int options, struct rusage *usage)
{
	static int calls;
	const char *mode = getenv("TIME_TEST_WAIT3");

	calls++;
	if (mode != NULL && strcmp(mode, "eintr") == 0 && calls == 1) {
		errno = EINTR;
		return -1;
	}
	if (mode != NULL && strcmp(mode, "echild") == 0) {
		if (calls >= 100) {
			(void)write(2, "wait3 retried\n", 14);
			_exit(99);
		}
		errno = ECHILD;
		return -1;
	}
	return wait3(status, options, usage);
}
