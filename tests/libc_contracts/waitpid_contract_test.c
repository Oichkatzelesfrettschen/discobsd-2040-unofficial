#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

pid_t db_waitpid(pid_t, int *, int);
pid_t test_wait4(pid_t, int *, int, struct rusage *);

static pid_t observed_process_id;
static int *observed_status;
static int observed_options;
static struct rusage *observed_usage;
static pid_t wait4_result;
static int wait4_error;
static unsigned int checks;
static unsigned int failures;

pid_t
test_wait4(pid_t process_id, int *status, int options, struct rusage *usage)
{
	observed_process_id = process_id;
	observed_status = status;
	observed_options = options;
	observed_usage = usage;
	if (wait4_result < 0)
		errno = wait4_error;
	return wait4_result;
}

static void
check(int condition, const char *description)
{
	checks++;
	if (!condition) {
		fprintf(stderr, "waitpid: %s\n", description);
		failures++;
	}
}

int
main(void)
{
	int status;
	pid_t result;

	wait4_result = 37;
	result = db_waitpid(-19, &status, WNOHANG | WUNTRACED);
	check(result == 37, "returns the wait4 result");
	check(observed_process_id == -19, "forwards the process selector");
	check(observed_status == &status, "forwards the status pointer");
	check(observed_options == (WNOHANG | WUNTRACED),
	    "forwards the option bits");
	check(observed_usage == NULL, "requests no resource usage");

	wait4_result = -1;
	wait4_error = EINTR;
	errno = 0;
	result = db_waitpid(4, NULL, 0);
	check(result == -1, "returns the wait4 failure");
	check(errno == EINTR, "preserves the wait4 error");
	check(observed_status == NULL, "forwards a null status pointer");

	if (failures != 0)
		return 1;
	printf("PASS waitpid contract: %u checks\n", checks);
	return 0;
}
