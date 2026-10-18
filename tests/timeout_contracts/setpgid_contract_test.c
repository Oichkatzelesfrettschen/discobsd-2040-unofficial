#include <errno.h>
#include <stdio.h>
#include <sys/types.h>

int db_setpgid(pid_t, pid_t);
int test_setpgrp(pid_t, pid_t);
pid_t test_getpid(void);

static pid_t observed_process;
static pid_t observed_group;
static int forwarded_result;
static int forwarded_error;
static unsigned int checks;
static unsigned int failures;

pid_t
test_getpid(void)
{
	return 23;
}

int
test_setpgrp(pid_t process_id, pid_t process_group)
{
	observed_process = process_id;
	observed_group = process_group;
	if (forwarded_result < 0)
		errno = forwarded_error;
	return forwarded_result;
}

static void
check(int condition, const char *description)
{
	checks++;
	if (!condition) {
		fprintf(stderr, "setpgid: %s\n", description);
		failures++;
	}
}

int
main(void)
{
	int result;

	forwarded_result = 0;
	result = db_setpgid(0, 0);
	check(result == 0, "does not return setpgrp success for zero IDs");
	check(observed_process == 23, "does not resolve process zero to the caller");
	check(observed_group == 23, "does not resolve group zero to the effective process");

	result = db_setpgid(41, 0);
	check(result == 0, "does not return setpgrp success for group zero");
	check(observed_process == 41, "changes an explicit process ID");
	check(observed_group == 41, "does not resolve group zero to the explicit process");

	result = db_setpgid(0, 37);
	check(result == 0, "does not return setpgrp success for process zero");
	check(observed_process == 23, "does not resolve process zero with an explicit group");
	check(observed_group == 37, "changes an explicit process-group ID");

	result = db_setpgid(41, 37);
	check(result == 0, "does not return setpgrp success");
	check(observed_process == 41, "does not forward the process ID");
	check(observed_group == 37, "does not forward the process-group ID");

	forwarded_result = -1;
	forwarded_error = EPERM;
	errno = 0;
	result = db_setpgid(41, 37);
	check(result == -1, "does not return setpgrp failure");
	check(errno == EPERM, "does not preserve the setpgrp error");

	if (failures != 0)
		return 1;
	printf("PASS setpgid contract: %u checks\n", checks);
	return 0;
}
