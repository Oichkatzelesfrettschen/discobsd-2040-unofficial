/*
 * The members of the 64-bit view that are a second build of a narrow
 * source and reach the kernel through a wrapper: time() (time.c), ftime()
 * (lib/libc/compat/ftime.c), utime() (lib/libc/compat/utime.c) and
 * wait3() (wait3.c). Each is linked with the wrapper it calls
 * (lib/libc/gen/time64_*.c) and the stand-in 32-bit entry points of
 * tests/time64_wrappers/narrow_calls.c, so a case checks the member's own
 * width end to end: a clock with bit 31 set reads past 2038, a file time
 * outside 0..4294967294 fails with EINVAL before the entry point, and a
 * child's usage comes back in the wide layout. Run from calendar64_test.c
 * under qemu-arm.
 */
#define _TIME_BITS 64
#include <sys/types.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <utime.h>
#include <errno.h>

#include "narrow_calls.h"

int printf(const char *, ...);
int calls64_checks(int *);

_Static_assert(sizeof(struct utimbuf) == 16, "utimbuf holds two 64-bit counts");
_Static_assert(sizeof(struct timeb) == 16, "timeb holds a 64-bit count");

#define PAST_2038	0x90000000UL	/* a count with bit 31 set */

static int checks, failures;

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("FAIL line %d: %s\n", __LINE__, #cond);		\
	}								\
} while (0)

static void
check_time(void)
{
	time_t stored = 0;

	narrow_reset();
	narrow_give_sec[0] = PAST_2038;
	CHECK(time(&stored) == (time_t)PAST_2038);
	CHECK(stored == (time_t)PAST_2038 && narrow_calls == 1);
	narrow_reset();
	narrow_give_sec[0] = 0xffffffffUL;
	CHECK(time(0) == 4294967295LL);
	narrow_reset();
	narrow_result = -1;
	stored = 7;
	CHECK(time(&stored) == -1 && stored == 7);
}

static void
check_ftime(void)
{
	struct timeb tb;

	narrow_reset();
	narrow_give_sec[0] = PAST_2038;
	narrow_give_sub[0] = 999999;
	CHECK(ftime(&tb) == 0);
	CHECK(tb.time == (time_t)PAST_2038 && tb.millitm == 999);
	narrow_reset();
	narrow_result = -1;
	CHECK(ftime(&tb) == -1);
}

static void
check_utime(void)
{
	struct utimbuf times;

	narrow_reset();
	times.actime = 0xfffffffeLL;
	times.modtime = (time_t)PAST_2038;
	CHECK(utime("f", &times) == 0 && narrow_calls == 1);
	CHECK(narrow_seen_sec[0] == 0xfffffffeUL &&
	    narrow_seen_sec[1] == PAST_2038);
	CHECK(narrow_seen_sub[0] == 0 && narrow_seen_sub[1] == 0);

	narrow_reset();
	CHECK(utime("f", 0) == 0 && narrow_calls == 1 && narrow_null == 1);

	narrow_reset();
	times.actime = 0x100000000LL;
	times.modtime = 0;
	CHECK(utime("f", &times) == -1 && errno == EINVAL &&
	    narrow_calls == 0);
	narrow_reset();
	times.actime = 0xffffffffLL;	/* the kernel's leave-alone value */
	times.modtime = 0;
	CHECK(utime("f", &times) == -1 && errno == EINVAL &&
	    narrow_calls == 0);
	narrow_reset();
	times.actime = 0;
	times.modtime = -1;
	CHECK(utime("f", &times) == -1 && errno == EINVAL &&
	    narrow_calls == 0);
}

static void
check_wait3(void)
{
	struct rusage usage;
	int status = 0;

	narrow_reset();
	narrow_result = 42;
	narrow_give_sec[0] = 0x7fffffffUL;
	narrow_give_sec[1] = 0xffffffffUL;	/* -1 seconds as a long */
	usage.ru_utime.tv_sec = 5;
	CHECK(wait3(&status, 0, &usage) == 42 && status == 0x300);
	CHECK(usage.ru_utime.tv_sec == 0x7fffffffLL &&
	    usage.ru_stime.tv_sec == -1 && usage.ru_maxrss == 11 &&
	    usage.ru_nivcsw == 13);
	narrow_reset();
	narrow_result = 0;
	usage.ru_utime.tv_sec = 5;
	CHECK(wait3(&status, WNOHANG, &usage) == 0 &&
	    usage.ru_utime.tv_sec == 5);
}

int
calls64_checks(int *total)
{
	check_time();
	check_ftime();
	check_utime();
	check_wait3();
	*total += checks;
	return failures;
}
