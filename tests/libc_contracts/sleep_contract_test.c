/*
 * Contract gate for lib/libc/gen/sleep.c, compiled from the tree's source
 * at the 32-bit view with gettimeofday() and select() moved onto the stubs
 * below, which replay two clock readings and record the timeout.
 *
 * sleep() returns the part of its request it did not sleep, computed from
 * the clock before and after its select(). Each reading is the kernel's
 * 32-bit count in the signed tv_sec of struct timeval, so the elapsed time
 * is a difference modulo 2^32: the carry out of bit 31 in January 2038 is
 * one second like any other, and a clock set back reads as an elapsed time
 * past the request and returns 0. sleep_signed_bad.c is the comparison
 * made signed, which the 2038 cases reject.
 */
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>

unsigned int	db_sleep(unsigned int);

static unsigned long	readings[2];
static int		reading_count;
static int		select_count;
static long		select_seconds;

int
test_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	(void)tz;
	if (reading_count >= 2)
		return (-1);
	tv->tv_sec = (long)readings[reading_count++];
	tv->tv_usec = 0;
	return (0);
}

int
test_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout)
{
	(void)nfds;
	(void)readfds;
	(void)writefds;
	(void)exceptfds;
	select_count++;
	select_seconds = timeout != 0 ? timeout->tv_sec : -1;
	return (0);
}

struct sleep_case {
	const char	*name;
	unsigned long	before, after;
	unsigned int	request, unslept;
};

static const struct sleep_case cases[] = {
	{ "full sleep", 1000UL, 1010UL, 10, 0 },
	{ "interrupted sleep", 1000UL, 1003UL, 10, 7 },
	{ "oversleep", 1000UL, 1020UL, 10, 0 },
	{ "clock set back", 1000UL, 900UL, 10, 0 },
	{ "interrupted across the 2038 carry", 0x7ffffffeUL, 0x80000001UL,
	    10, 7 },
	{ "full sleep across the 2038 carry", 0x7ffffffbUL, 0x80000005UL,
	    10, 0 },
	{ "interrupted after 2038", 0x90000000UL, 0x90000002UL, 10, 8 },
	{ "interrupted across the 2106 wrap", 0xfffffffeUL, 0x00000001UL,
	    10, 7 },
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
		left = db_sleep(cases[i].request);
		if (left != cases[i].unslept)
			fail(cases[i].name, "wrong unslept time");
		if (reading_count != 2 || select_count != 1 ||
		    select_seconds != (long)cases[i].request)
			fail(cases[i].name, "wrong calls");
	}

	reading_count = 0;
	select_count = 0;
	if (db_sleep(0) != 0 || reading_count != 0 || select_count != 0)
		fail("zero request", "a zero request read the clock or slept");

	if (failure_count != 0)
		return (1);
	(void)write(STDERR_FILENO, "sleep contract passed\n", 22);
	return (0);
}
