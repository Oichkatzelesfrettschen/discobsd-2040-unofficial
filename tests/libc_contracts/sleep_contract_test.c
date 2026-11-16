/*
 * Contract gate for lib/libc/gen/sleep.c, compiled from the tree's source
 * at the 32-bit view with gettimeofday() and select() moved onto the stubs
 * below. Two stub models run against it.
 *
 * Replay: two clock readings and a recorded select() result. sleep()
 * returns the part of its request it did not sleep, computed from the
 * clock before and after its select(). Each reading is the kernel's
 * 32-bit count in the signed tv_sec of struct timeval, so the elapsed time
 * is a difference modulo 2^32: the carry out of bit 31 in January 2038 is
 * one second like any other, and a clock set back reads as an elapsed time
 * past the request and returns 0. sleep_signed_bad.c is the comparison
 * made signed, which the 2038 cases reject.
 *
 * Kernel: a clock that moves only while select() sleeps, and a select()
 * that applies the two limits of the kernel's timeout path
 * (sys/kern/kern_time.c itimerfix(), sys/kern/kern_clock.c hzto()): a
 * timeout over 100,000,000 s, or one whose tv_sec is negative, fails with
 * EINVAL after sleeping nothing, and a timeout over 0x7fffffff / 1000 s
 * (the tick count of a 1000 Hz clock saturates at 0x7fffffff) expires
 * after that many seconds with a return of 0. A timeout whose deadline, the
 * clock plus the timeout modulo 2^32, wraps below the clock is due at once:
 * select1() compares the two unsigned (wall_reached(), sys/sys/time.h), so
 * within one timeout of the rollover in February 2106 it returns 0 with the
 * clock unmoved. Against that model sleep()
 * must sleep the whole request, in chunks, for every unsigned request;
 * a signal must return the part not yet slept; a select() failure must
 * stop the loop and return the part not yet slept.
 * sleep_unchunked_bad.c is the single-select implementation, which the
 * kernel cases reject.
 */
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>

unsigned int	db_sleep(unsigned int);

#define KERNEL_SELECT_BOUND	100000000L	/* itimerfix() */
#define KERNEL_TICK_CLAMP	2147483UL	/* 0x7fffffff / HZ 1000, hzto() */
#define RUNAWAY_CALLS		20000

static void		fail(const char *, const char *);

static unsigned long	readings[2];
static int		reading_count;
static int		select_count;
static long		select_seconds;
static int		select_result;

static int		model_active;
static unsigned long	model_clock;
static unsigned long	model_slept;
static long		model_longest;
static int		model_refused;
static unsigned long	model_signal_at;
static int		model_fail_call;

int
test_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	(void)tz;
	if (model_active) {
		tv->tv_sec = (long)model_clock;
		tv->tv_usec = 0;
		return (0);
	}
	if (reading_count >= 2)
		return (-1);
	tv->tv_sec = (long)readings[reading_count++];
	tv->tv_usec = 0;
	return (0);
}

static unsigned long
model_advance(unsigned long seconds)
{
	model_clock = (model_clock + seconds) & 0xffffffffUL;
	model_slept += seconds;
	return (seconds);
}

int
test_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout)
{
	unsigned long want;

	(void)nfds;
	(void)readfds;
	(void)writefds;
	(void)exceptfds;
	select_count++;
	select_seconds = timeout != 0 ? timeout->tv_sec : -1;
	if (select_count > RUNAWAY_CALLS) {
		fail("select", "select() was called without bound");
		_exit(1);
	}
	if (!model_active)
		return (select_result);
	if (timeout == 0)
		return (-1);
	if (model_fail_call != 0 && select_count == model_fail_call) {
		return (-1);
	}
	if (timeout->tv_sec >= 0 && timeout->tv_sec <= KERNEL_SELECT_BOUND &&
	    (unsigned long long)model_clock + (unsigned long long)timeout->tv_sec
	    > 0xffffffffULL)
		return (0);
	if (timeout->tv_sec < 0 || timeout->tv_sec > KERNEL_SELECT_BOUND) {
		model_refused++;
		return (-1);
	}
	if (timeout->tv_sec > model_longest)
		model_longest = timeout->tv_sec;
	want = (unsigned long)timeout->tv_sec;
	if (want > KERNEL_TICK_CLAMP)
		want = KERNEL_TICK_CLAMP;
	if (model_signal_at != 0 && model_slept + want > model_signal_at) {
		(void)model_advance(model_signal_at - model_slept);
		return (-1);
	}
	(void)model_advance(want);
	return (0);
}

struct sleep_case {
	const char	*name;
	unsigned long	before, after;
	unsigned int	request, unslept;
	int		result;		/* what select() returns */
};

static const struct sleep_case cases[] = {
	{ "full sleep", 1000UL, 1010UL, 10, 0, 0 },
	{ "interrupted sleep", 1000UL, 1003UL, 10, 7, -1 },
	{ "oversleep", 1000UL, 1020UL, 10, 0, 0 },
	{ "clock set back", 1000UL, 900UL, 10, 0, 0 },
	{ "interrupted across the 2038 carry", 0x7ffffffeUL, 0x80000001UL,
	    10, 7, -1 },
	{ "full sleep across the 2038 carry", 0x7ffffffbUL, 0x80000005UL,
	    10, 0, 0 },
	{ "interrupted after 2038", 0x90000000UL, 0x90000002UL, 10, 8, -1 },
	{ "interrupted across the 2106 wrap", 0xfffffffeUL, 0x00000001UL,
	    10, 7, -1 },
};

struct model_case {
	const char	*name;
	unsigned long	start, request;
	unsigned long	signal_at;	/* seconds slept when a signal lands */
	int		fail_call;	/* select() call that fails */
	int		stalls;		/* the deadline wraps; select() is due at once */
};

static const struct model_case models[] = {
	{ "request 1", 1000UL, 1UL, 0, 0, 0 },
	{ "request 1,000,000", 1000UL, 1000000UL, 0, 0, 0 },
	{ "request 2,147,483 (last exact tick count)", 1000UL, 2147483UL,
	    0, 0, 0 },
	{ "request 2,147,484 (first saturated tick count)", 1000UL,
	    2147484UL, 0, 0, 0 },
	{ "request 100,000,000 (the select bound)", 1000UL, 100000000UL,
	    0, 0, 0 },
	{ "request 100,000,001 (first refused)", 1000UL, 100000001UL, 0, 0, 0 },
	{ "request LONG_MAX", 1000UL, 2147483647UL, 0, 0, 0 },
	{ "request LONG_MAX + 1", 1000UL, 2147483648UL, 0, 0, 0 },
	{ "request UINT_MAX (clock at the count's zero)", 0UL, 4294967295UL,
	    0, 0, 0 },
	{ "deadline lands on the last count before the 2106 rollover",
	    0xfff0bdbfUL, 1000000UL, 0, 0, 0 },
	{ "deadline wraps at the 2106 rollover", 0xfffff000UL, 3000000UL,
	    0, 0, 1 },
	{ "deadline wraps one second past the last count", 0xfff0bdc0UL,
	    1000000UL, 0, 0, 1 },
	{ "signal in the first chunk of a long request", 1000UL,
	    200000000UL, 10UL, 0, 0 },
	{ "signal after one chunk", 1000UL, 3000000UL, 1500000UL, 0, 0 },
	{ "signal late in UINT_MAX", 0UL, 4294967295UL, 4000000000UL, 0, 0 },
	{ "select fails on the first call", 1000UL, 5000000UL, 0, 1, 0 },
	{ "select fails on the second call", 1000UL, 5000000UL, 0, 2, 0 },
};

static int failure_count;

static void
fail(const char *name, const char *what)
{
	(void)write(STDERR_FILENO, "FAIL ", 5);
	(void)write(STDERR_FILENO, name, strlen(name));
	(void)write(STDERR_FILENO, ": ", 2);
	(void)write(STDERR_FILENO, what, strlen(what));
	(void)write(STDERR_FILENO, "\n", 1);
	failure_count++;
}

static void
run_model(const struct model_case *c)
{
	unsigned int left;

	model_active = 1;
	model_clock = c->start;
	model_slept = 0;
	model_longest = 0;
	model_refused = 0;
	model_signal_at = c->signal_at;
	model_fail_call = c->fail_call;
	select_count = 0;
	left = db_sleep((unsigned int)c->request);
	model_active = 0;

	if (model_refused != 0)
		fail(c->name, "a timeout reached the kernel's bound");
	if (model_longest > (long)KERNEL_TICK_CLAMP)
		fail(c->name, "a timeout past the saturated tick count");
	if ((unsigned long)left != c->request - model_slept)
		fail(c->name, "unslept time differs from the time not slept");
	if (c->stalls != 0) {
		if (select_count > 3)
			fail(c->name, "a stalled select() was repeated");
		if (left != c->request - model_slept || left == 0)
			fail(c->name, "a stalled sleep reported a full sleep");
	} else if (c->signal_at != 0) {
		if (model_slept != c->signal_at)
			fail(c->name, "the signal did not end the sleep");
	} else if (c->fail_call != 0) {
		if (select_count != c->fail_call)
			fail(c->name, "a failed select() did not end the sleep");
		if (left == 0)
			fail(c->name, "a failed select() reported a full sleep");
	} else {
		if (left != 0 || model_slept != c->request)
			fail(c->name, "the request was not slept in full");
	}
}

int
main(void)
{
	size_t i;
	unsigned int left;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		readings[0] = cases[i].before;
		readings[1] = cases[i].after;
		reading_count = 0;
		select_count = 0;
		select_result = cases[i].result;
		left = db_sleep(cases[i].request);
		if (left != cases[i].unslept)
			fail(cases[i].name, "wrong unslept time");
		if (reading_count != 2 || select_count != 1 ||
		    select_seconds != (long)cases[i].request)
			fail(cases[i].name, "wrong calls");
	}

	reading_count = 0;
	select_count = 0;
	select_result = 0;
	if (db_sleep(0) != 0 || reading_count != 0 || select_count != 0)
		fail("zero request", "a zero request read the clock or slept");

	for (i = 0; i < sizeof(models) / sizeof(models[0]); i++)
		run_model(&models[i]);

	if (failure_count != 0)
		return (1);
	(void)write(STDERR_FILENO, "sleep contract passed\n", 22);
	return (0);
}
