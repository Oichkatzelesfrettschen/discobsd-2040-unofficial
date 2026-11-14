/*
 * The system call wrappers of the 64-bit view of time (lib/libc/gen/
 * time64_*.c), compiled for Thumb-1 and run under qemu-arm against the
 * stand-in 32-bit entry points of narrow_calls.c. This unit is built with
 * _TIME_BITS=64, so each call below binds to a __NAME64 wrapper, and each
 * case checks one conversion: a count since 1970 widens zero-extended and
 * narrows only from 0..4294967295, an interval keeps its sign and narrows
 * only from the range of a long, a null time stays null, and a refused
 * value never reaches the entry point.
 */
#define _TIME_BITS 64
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <errno.h>
#include <unistd.h>

#include "narrow_calls.h"

int printf(const char *, ...);

_Static_assert(sizeof(time_t) == 8, "time_t is 64 bits in this unit");
_Static_assert(sizeof(struct timeval) == 16, "the wide timeval is 16 bytes");
_Static_assert(_Alignof(struct timeval) == 8, "and aligned to eight");
_Static_assert(sizeof(struct stat) == 72, "the wide stat is 72 bytes");
_Static_assert(sizeof(struct __stat32) == 56, "the narrow stat is the kernel's");

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
stat_family(void)
{
	struct stat sb;
	int which;

	for (which = 0; which < 3; which++) {
		narrow_reset();
		narrow_give_sec[0] = 1;
		narrow_give_sec[1] = PAST_2038;
		narrow_give_sec[2] = 0xffffffffUL;
		sb.st_mtime = -1;
		CHECK((which == 0 ? stat("f", &sb) : which == 1 ?
		    lstat("f", &sb) : fstat(3, &sb)) == 0);
		CHECK(narrow_calls == 1);
		CHECK(sb.st_atime == 1);
		CHECK(sb.st_mtime == 2415919104LL);
		CHECK(sb.st_ctime == 4294967295LL);
		CHECK(sb.st_dev == 3 && sb.st_ino == 77 && sb.st_mode == 0100644);
		CHECK(sb.st_nlink == 2 && sb.st_uid == 5 && sb.st_gid == 6);
		CHECK(sb.st_rdev == 9 && sb.st_size == 123456);
		CHECK(sb.st_blksize == 1024 && sb.st_blocks == 121);
		CHECK(sb.st_flags == 0x10);
	}
	/* A failed call leaves the caller's structure alone. */
	narrow_reset();
	narrow_result = -1;
	sb.st_mtime = 42;
	CHECK(stat("f", &sb) == -1 && sb.st_mtime == 42);
	/* A null structure reaches the entry point as null. */
	narrow_reset();
	CHECK(stat("f", 0) == -1 && narrow_null && narrow_calls == 1);
	narrow_reset();
	CHECK(lstat("f", 0) == -1 && narrow_null);
	narrow_reset();
	CHECK(fstat(3, 0) == -1 && narrow_null);
}

static void
clock_calls(void)
{
	struct timeval tv, pair[2];

	narrow_reset();
	narrow_give_sec[0] = PAST_2038;
	narrow_give_sub[0] = 250000;
	CHECK(gettimeofday(&tv, 0) == 0);
	CHECK(tv.tv_sec == 2415919104LL && tv.tv_usec == 250000);
	narrow_reset();
	CHECK(gettimeofday(0, 0) == 0 && narrow_null);

	narrow_reset();
	tv.tv_sec = 2415919104LL;
	tv.tv_usec = 7;
	CHECK(settimeofday(&tv, 0) == 0 && narrow_calls == 1);
	CHECK(narrow_seen_sec[0] == PAST_2038 && narrow_seen_sub[0] == 7);
	narrow_reset();
	tv.tv_sec = 4294967295LL;
	CHECK(settimeofday(&tv, 0) == 0 && narrow_seen_sec[0] == 0xffffffffUL);
	narrow_reset();
	tv.tv_sec = 4294967296LL;
	CHECK(settimeofday(&tv, 0) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	tv.tv_sec = -1;
	CHECK(settimeofday(&tv, 0) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	CHECK(settimeofday(0, 0) == 0 && narrow_null);

	narrow_reset();
	pair[0].tv_sec = 1;
	pair[0].tv_usec = 2;
	pair[1].tv_sec = 2415919104LL;
	pair[1].tv_usec = 3;
	CHECK(utimes("f", pair) == 0 && futimes(3, pair) == 0);
	CHECK(narrow_calls == 2 && narrow_seen_sec[0] == 1);
	CHECK(narrow_seen_sec[1] == PAST_2038 && narrow_seen_sub[1] == 3);
	narrow_reset();
	pair[1].tv_sec = -1;
	CHECK(utimes("f", pair) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	pair[1].tv_sec = 1;
	pair[0].tv_sec = 4294967296LL;
	CHECK(futimes(3, pair) == -1 && errno == EINVAL && narrow_calls == 0);
	/* The last count is the kernel's "leave alone" value for a file time. */
	narrow_reset();
	pair[0].tv_sec = 4294967294LL;
	pair[1].tv_sec = 4294967294LL;
	CHECK(utimes("f", pair) == 0 && narrow_seen_sec[1] == 0xfffffffeUL);
	narrow_reset();
	pair[1].tv_sec = 4294967295LL;
	CHECK(utimes("f", pair) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	pair[1].tv_sec = 1;
	pair[0].tv_sec = 4294967295LL;
	CHECK(futimes(3, pair) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	CHECK(utimes("f", 0) == 0 && narrow_null);
	CHECK(futimes(3, 0) == 0 && narrow_null && narrow_calls == 2);
}

static void
interval_calls(void)
{
	struct timeval tv, old;
	struct timespec ts;
	struct itimerval value, before;

	/* A correction and the correction still owed keep their signs. */
	narrow_reset();
	narrow_give_sec[0] = (unsigned long)-3L;
	narrow_give_sub[0] = -500000;
	tv.tv_sec = -5;
	tv.tv_usec = -1;
	CHECK(adjtime(&tv, &old) == 0);
	CHECK(narrow_seen_sec[0] == (unsigned long)-5L && narrow_seen_sub[0] == -1);
	CHECK(old.tv_sec == -3 && old.tv_usec == -500000);
	narrow_reset();
	CHECK(adjtime(&tv, 0) == 0 && narrow_old_null);
	narrow_reset();
	CHECK(adjtime(0, &old) == -1 && narrow_null && narrow_calls == 1);
	narrow_reset();
	tv.tv_sec = 2147483648LL;
	CHECK(adjtime(&tv, &old) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	tv.tv_sec = -2147483649LL;
	CHECK(adjtime(&tv, &old) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	tv.tv_sec = -2147483647LL - 1;
	CHECK(adjtime(&tv, 0) == 0 && narrow_seen_sec[0] == 0x80000000UL);

	narrow_reset();
	tv.tv_sec = 5;
	tv.tv_usec = 7;
	CHECK(select(0, 0, 0, 0, &tv) == 0);
	CHECK(narrow_seen_sec[0] == 5 && narrow_seen_sub[0] == 7);
	CHECK(tv.tv_sec == 5 && tv.tv_usec == 7);
	narrow_reset();
	CHECK(select(0, 0, 0, 0, 0) == 0 && narrow_null);
	narrow_reset();
	tv.tv_sec = 1LL << 40;
	CHECK(select(0, 0, 0, 0, &tv) == -1 && errno == EINVAL && narrow_calls == 0);

	narrow_reset();
	ts.tv_sec = 5;
	ts.tv_nsec = 9;
	CHECK(pselect(0, 0, 0, 0, &ts, 0) == 0);
	CHECK(narrow_seen_sec[0] == 5 && narrow_seen_sub[0] == 9);
	narrow_reset();
	CHECK(pselect(0, 0, 0, 0, 0, 0) == 0 && narrow_null);
	narrow_reset();
	ts.tv_sec = -1;
	CHECK(pselect(0, 0, 0, 0, &ts, 0) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	ts.tv_sec = 2147483648LL;
	CHECK(pselect(0, 0, 0, 0, &ts, 0) == -1 && errno == EINVAL && narrow_calls == 0);

	narrow_reset();
	narrow_give_sec[0] = 2;
	narrow_give_sub[0] = 3;
	narrow_give_sec[1] = 4;
	narrow_give_sub[1] = 5;
	CHECK(getitimer(0, &value) == 0);
	CHECK(value.it_interval.tv_sec == 2 && value.it_interval.tv_usec == 3);
	CHECK(value.it_value.tv_sec == 4 && value.it_value.tv_usec == 5);
	narrow_reset();
	CHECK(getitimer(0, 0) == -1 && narrow_null && narrow_calls == 1);

	narrow_reset();
	narrow_give_sec[2] = 6;
	narrow_give_sub[2] = 7;
	narrow_give_sec[3] = 8;
	narrow_give_sub[3] = 9;
	CHECK(setitimer(0, &value, &before) == 0);
	CHECK(narrow_seen_sec[0] == 2 && narrow_seen_sub[0] == 3);
	CHECK(narrow_seen_sec[1] == 4 && narrow_seen_sub[1] == 5);
	CHECK(before.it_interval.tv_sec == 6 && before.it_interval.tv_usec == 7);
	CHECK(before.it_value.tv_sec == 8 && before.it_value.tv_usec == 9);
	narrow_reset();
	CHECK(setitimer(0, &value, 0) == 0 && narrow_old_null);
	narrow_reset();
	CHECK(setitimer(0, 0, &before) == 0 && narrow_null);
	narrow_reset();
	value.it_value.tv_sec = 1LL << 33;
	CHECK(setitimer(0, &value, 0) == -1 && errno == EINVAL && narrow_calls == 0);
	narrow_reset();
	value.it_value.tv_sec = 4;
	value.it_interval.tv_sec = 1LL << 33;
	CHECK(setitimer(0, &value, 0) == -1 && errno == EINVAL && narrow_calls == 0);
}

static void
usage_calls(void)
{
	struct rusage usage;
	int status = 0;

	narrow_reset();
	narrow_give_sec[0] = 7;
	narrow_give_sub[0] = 8;
	narrow_give_sec[1] = 9;
	narrow_give_sub[1] = 10;
	CHECK(getrusage(0, &usage) == 0);
	CHECK(usage.ru_utime.tv_sec == 7 && usage.ru_utime.tv_usec == 8);
	CHECK(usage.ru_stime.tv_sec == 9 && usage.ru_stime.tv_usec == 10);
	CHECK(usage.ru_maxrss == 11 && usage.ru_minflt == 12);
	CHECK(usage.ru_nivcsw == 13 && usage.ru_ixrss == 0);
	narrow_reset();
	CHECK(getrusage(0, 0) == -1 && narrow_null && narrow_calls == 1);

	narrow_reset();
	narrow_result = 42;
	usage.ru_nivcsw = 0;
	CHECK(wait4(-1, &status, 0, &usage) == 42 && status == 0x300);
	CHECK(usage.ru_utime.tv_sec == 7 && usage.ru_stime.tv_usec == 10);
	CHECK(usage.ru_nivcsw == 13);
	narrow_reset();
	narrow_result = 42;
	CHECK(wait4(-1, &status, 0, 0) == 42 && narrow_null);
	/*
	 * A stopped child: the entry point writes no usage, so the caller's
	 * structure stays as it was, with or without a status pointer.
	 */
	narrow_reset();
	narrow_result = 42;
	narrow_stopped = 1;
	usage.ru_nivcsw = 77;
	usage.ru_utime.tv_sec = 5;
	CHECK(wait4(-1, &status, 2, &usage) == 42 && status == 0177);
	CHECK(usage.ru_nivcsw == 77 && usage.ru_utime.tv_sec == 5);
	narrow_reset();
	narrow_result = 42;
	narrow_stopped = 1;
	CHECK(wait4(-1, 0, 2, &usage) == 42 && usage.ru_nivcsw == 77);
	/* No child reported: the caller's structure stays as it was. */
	narrow_reset();
	usage.ru_nivcsw = 99;
	CHECK(wait4(-1, &status, 1, &usage) == 0 && usage.ru_nivcsw == 99);
}

int
main(void)
{
	stat_family();
	clock_calls();
	interval_calls();
	usage_calls();
	if (failures) {
		printf("time64 wrappers: %d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("time64 wrappers: %d checks passed\n", checks);
	return 0;
}
