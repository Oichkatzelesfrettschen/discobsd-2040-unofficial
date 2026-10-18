#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

unsigned int db_alarm(unsigned int);
int test_setitimer(int, const struct itimerval *, struct itimerval *);

int alarm_test_errno;

static int setitimer_result;
static struct itimerval prior_timer;
static struct itimerval requested_timer;
static unsigned int checks;
static unsigned int failures;

int
test_setitimer(int timer_kind, const struct itimerval *new_timer,
    struct itimerval *old_timer)
{
	if (timer_kind != ITIMER_REAL) {
		fprintf(stderr, "alarm: setitimer used timer kind %d\n", timer_kind);
		failures++;
	}
	requested_timer = *new_timer;
	*old_timer = prior_timer;
	if (setitimer_result < 0)
		alarm_test_errno = EIO;
	return (setitimer_result);
}

static void
check(int condition, const char *description)
{
	checks++;
	if (!condition) {
		fprintf(stderr, "alarm: %s\n", description);
		failures++;
	}
}

static void
prepare(long prior_seconds, long prior_microseconds, int result)
{
	memset(&prior_timer, 0, sizeof(prior_timer));
	memset(&requested_timer, 0xa5, sizeof(requested_timer));
	prior_timer.it_value.tv_sec = prior_seconds;
	prior_timer.it_value.tv_usec = prior_microseconds;
	setitimer_result = result;
}

int
main(void)
{
	unsigned int remaining;

	prepare(7, 0, 0);
	remaining = db_alarm(13);
	check(remaining == 7, "returns the prior whole-second value");
	check(requested_timer.it_value.tv_sec == 13,
	    "passes the requested seconds to setitimer");
	check(requested_timer.it_value.tv_usec == 0,
	    "clears the requested microseconds");
	check(requested_timer.it_interval.tv_sec == 0 &&
	    requested_timer.it_interval.tv_usec == 0,
	    "requests a one-shot timer");

	prepare(7, 1, 0);
	remaining = db_alarm(0);
	check(remaining == 8, "rounds a nonzero prior microsecond upward");
	check(requested_timer.it_value.tv_sec == 0,
	    "passes a zero request that cancels the timer");

	prepare(7, 999999, 0);
	remaining = db_alarm(UINT_MAX);
	check(remaining == 8,
	    "rounds the largest valid prior microsecond upward");
	check((unsigned long)requested_timer.it_value.tv_sec == UINT_MAX,
	    "preserves the unsigned request through time_t");

	prepare(5, 500000, -1);
	alarm_test_errno = 0;
	remaining = db_alarm(3);
	check(remaining == UINT_MAX,
	    "maps a setitimer failure to the unsigned error sentinel");
	check(alarm_test_errno == EIO, "preserves errno from setitimer");

	if (failures != 0)
		return (1);
	printf("PASS alarm contract: %u checks\n", checks);
	return (0);
}
