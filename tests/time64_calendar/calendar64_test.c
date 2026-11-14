/*
 * The calendar functions of the 64-bit view of time (lib/libc/gen/ctime.c,
 * mktime.c and difftime.c compiled with _TIME_BITS defined as 64) against
 * fields CPython's datetime computes (vectors.py, calendar64_vectors.h).
 * This unit is built with the same view, so each call binds to a __NAME64
 * symbol. The same source runs on the host (tests/libc_contracts
 * check-calendar64) and as Thumb-1 under qemu-arm (this directory's
 * Makefile), where CALENDAR64_CALLS adds the cases of calls64_test.c.
 *
 * gmtime() must read each instant into the oracle's fields; timegm() must
 * give the instant back from them; both refuse the instants outside the
 * years an int tm_year holds. Fields outside their ranges, past either end
 * of an int, reach timegm() with the count the oracle sums. localtime()
 * and mktime() are checked in GMT and in a zone five hours west, ctime()
 * against the oracle's lines, and difftime() where a double cannot hold
 * both endpoints apart.
 */
#define _TIME_BITS 64
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include "zone_hooks.h"

int printf(const char *, ...);

_Static_assert(sizeof(time_t) == 8, "time_t is 64 bits in this unit");

struct instant_vector {
	long long	count;
	int		year, mon, mday, hour, min, sec, wday, yday;
};

struct denormal_vector {
	int		year, mon, mday, hour, min, sec, accepted;
	long long	count;
};

struct ctime_vector {
	long long	count;
	const char	*text;
};

#include <calendar64_vectors.h>

#define COUNT(array)	((int)(sizeof(array) / sizeof((array)[0])))
#define ZONE_WEST	300		/* minutes: five hours west */
#define ZONE_OFFSET	(-ZONE_WEST * 60L)

#ifdef CALENDAR64_CALLS
int calls64_checks(int *);
#endif

static int checks, failures;

static void
check(int condition, int line, const char *what, long long count)
{
	checks++;
	if (condition)
		return;
	failures++;
	printf("FAIL line %d: %s at %lld\n", line, what, count);
}

#define CHECK(cond, count)	check((cond) != 0, __LINE__, #cond, (count))

static int
same_fields(const struct tm *tm, const struct instant_vector *v)
{
	return tm->tm_year == v->year && tm->tm_mon == v->mon &&
	    tm->tm_mday == v->mday && tm->tm_hour == v->hour &&
	    tm->tm_min == v->min && tm->tm_sec == v->sec &&
	    tm->tm_wday == v->wday && tm->tm_yday == v->yday;
}

static void
fill(struct tm *tm, int year, int mon, int mday, int hour, int min, int sec)
{
	memset(tm, 0, sizeof *tm);
	tm->tm_year = year;
	tm->tm_mon = mon;
	tm->tm_mday = mday;
	tm->tm_hour = hour;
	tm->tm_min = min;
	tm->tm_sec = sec;
	tm->tm_isdst = -1;
	tm->tm_wday = -1;
	tm->tm_yday = -1;
}

static void
select_zone(const char *tz, int minuteswest)
{
	zone_tz = tz;
	zone_minuteswest = minuteswest;
	tzset();
}

/* gmtime() reads every instant into the oracle's fields. */
static void
check_gmtime(void)
{
	const struct instant_vector *v;
	struct tm *tm;
	time_t t;
	int i;

	for (i = 0; i < COUNT(instant_vectors); i++) {
		v = &instant_vectors[i];
		t = v->count;
		tm = gmtime(&t);
		CHECK(tm != 0 && same_fields(tm, v), v->count);
		CHECK(tm != 0 && tm->tm_isdst == 0 && tm->tm_gmtoff == 0 &&
		    strcmp(tm->tm_zone, "GMT") == 0, v->count);
	}
}

/* timegm() gives the instant back from the fields and fills the rest. */
static void
check_timegm(void)
{
	const struct instant_vector *v;
	struct tm tm;
	int i;

	for (i = 0; i < COUNT(instant_vectors); i++) {
		v = &instant_vectors[i];
		fill(&tm, v->year, v->mon, v->mday, v->hour, v->min, v->sec);
		CHECK(timegm(&tm) == v->count, v->count);
		CHECK(same_fields(&tm, v) && tm.tm_isdst == 0, v->count);
	}
}

/*
 * The range is every year an int tm_year holds; one second past either
 * end, and the ends of a 64-bit count, are refused with EOVERFLOW.
 */
static void
check_range(void)
{
	static const long long outside[] = {
		ORACLE_CAL_MIN - 1, ORACLE_CAL_MAX + 1, LLONG_MIN, LLONG_MAX,
		ORACLE_CAL_MIN - 86400, ORACLE_CAL_MAX + 86400,
	};
	time_t t;
	int i;

	for (i = 0; i < COUNT(outside); i++) {
		t = outside[i];
		errno = 0;
		CHECK(gmtime(&t) == 0 && errno == EOVERFLOW, outside[i]);
		errno = 0;
		CHECK(localtime(&t) == 0 && errno == EOVERFLOW, outside[i]);
		CHECK(ctime(&t) == 0, outside[i]);
	}
	t = ORACLE_CAL_MIN;
	CHECK(gmtime(&t) != 0 && gmtime(&t)->tm_year == INT_MIN, t);
	t = ORACLE_CAL_MAX;
	CHECK(gmtime(&t) != 0 && gmtime(&t)->tm_year == INT_MAX, t);
}

/*
 * Fields outside their ranges: timegm() sums them without a carry an int
 * could overflow, so an instant inside the range is reached however far a
 * field strays, and one outside is refused with the fields untouched.
 * mktime() in GMT agrees.
 */
static void
check_denormal(void)
{
	const struct denormal_vector *v;
	struct tm tm, back;
	struct tm *expect;
	time_t got, t;
	int i, pass;

	for (i = 0; i < COUNT(denormal_vectors); i++) {
		v = &denormal_vectors[i];
		for (pass = 0; pass < 2; pass++) {
			fill(&tm, v->year, v->mon, v->mday, v->hour, v->min,
			    v->sec);
			got = pass == 0 ? timegm(&tm) : mktime(&tm);
			if (!v->accepted) {
				CHECK(got == -1 && tm.tm_wday == -1 &&
				    tm.tm_year == v->year && tm.tm_mon == v->mon &&
				    tm.tm_mday == v->mday && tm.tm_sec == v->sec,
				    v->count);
				continue;
			}
			CHECK(got == v->count, v->count);
			back = tm;
			t = v->count;
			expect = gmtime(&t);
			CHECK(expect != 0 && back.tm_year == expect->tm_year &&
			    back.tm_mon == expect->tm_mon &&
			    back.tm_mday == expect->tm_mday &&
			    back.tm_hour == expect->tm_hour &&
			    back.tm_sec == expect->tm_sec &&
			    back.tm_wday == expect->tm_wday, v->count);
		}
	}
}

/*
 * A zone five hours west, from the kernel's offset: localtime() reads each
 * instant five hours earlier, mktime() inverts it, and the fields of a
 * vector read as local time name the instant five hours later. An instant
 * whose local time leaves the range is refused.
 */
static void
check_zone(void)
{
	const struct instant_vector *v;
	struct tm *tm, local, utc;
	time_t t, earlier;
	int i;

	select_zone(0, ZONE_WEST);
	for (i = 0; i < COUNT(instant_vectors); i++) {
		v = &instant_vectors[i];
		t = v->count;
		errno = 0;
		tm = localtime(&t);
		if (v->count + ZONE_OFFSET < ORACLE_CAL_MIN) {
			CHECK(tm == 0 && errno == EOVERFLOW, v->count);
			continue;
		}
		CHECK(tm != 0 && tm->tm_gmtoff == ZONE_OFFSET &&
		    strcmp(tm->tm_zone, "EST") == 0, v->count);
		if (tm == 0)
			continue;
		local = *tm;
		earlier = t + ZONE_OFFSET;
		utc = *gmtime(&earlier);
		CHECK(local.tm_year == utc.tm_year && local.tm_mon == utc.tm_mon &&
		    local.tm_mday == utc.tm_mday && local.tm_hour == utc.tm_hour &&
		    local.tm_sec == utc.tm_sec && local.tm_wday == utc.tm_wday &&
		    local.tm_yday == utc.tm_yday, v->count);
		local.tm_isdst = -1;
		CHECK(mktime(&local) == t, v->count);

		fill(&local, v->year, v->mon, v->mday, v->hour, v->min, v->sec);
		t = mktime(&local);
		/*
		 * mktime() starts its search from timegm() of the fields and
		 * takes an answer of -1 as a refusal, so the local time whose
		 * UTC reading is the second before 1970 is refused in any zone
		 * but GMT. The 32-bit build shares the search and the refusal.
		 */
		if (v->count == -1)
			CHECK(t == -1, v->count);
		else if (v->count - ZONE_OFFSET > ORACLE_CAL_MAX)
			CHECK(t == -1 && local.tm_wday == -1, v->count);
		else
			CHECK(t == v->count - ZONE_OFFSET &&
			    local.tm_wday == v->wday, v->count);
	}
	select_zone("", 0);
}

/* ctime() is asctime() of localtime(), which refuses a year past 9999. */
static void
check_ctime(void)
{
	const struct ctime_vector *v;
	const char *text;
	time_t t;
	int i;

	for (i = 0; i < COUNT(ctime_vectors); i++) {
		v = &ctime_vectors[i];
		t = v->count;
		text = ctime(&t);
		if (v->text == 0)
			CHECK(text == 0, v->count);
		else
			CHECK(text != 0 && strcmp(text, v->text) == 0, v->count);
	}
}

/*
 * Each answer passes through a double parameter, which rounds it to a
 * double where the host evaluates in a wider format (x87 under -m32).
 */
static int
same_double(double got, double want)
{
	return got == want;
}

/*
 * Two counts a double cannot tell apart still differ by one, and a
 * difference beyond a long long converts each end first.
 */
static void
check_difftime(void)
{
	const long long big = 1LL << 60;

	CHECK(same_double(difftime(big + 1, big), 1.0), big);
	CHECK(same_double(difftime(big, big + 1), -1.0), big);
	CHECK(same_double(difftime(-big, -big - 1), 1.0), -big);
	CHECK(same_double(difftime(0, -1), 1.0), 0);
	CHECK(same_double(difftime(ORACLE_CAL_MAX, ORACLE_CAL_MIN),
	    135536076801417599.0), ORACLE_CAL_MAX);
	CHECK(same_double(difftime(LLONG_MAX, LLONG_MIN),
	    18446744073709551615.0), LLONG_MAX);
	CHECK(same_double(difftime(LLONG_MIN, LLONG_MAX),
	    -18446744073709551615.0), LLONG_MIN);
	CHECK(same_double(difftime(LLONG_MAX, -1), 9223372036854775808.0),
	    LLONG_MAX);
}

int
main(void)
{
	select_zone("", 0);
	check_gmtime();
	check_timegm();
	check_range();
	check_denormal();
	check_zone();
	check_ctime();
	check_difftime();
#ifdef CALENDAR64_CALLS
	failures += calls64_checks(&checks);
#endif
	printf("calendar64: %d checks, %d failures over %d instants\n",
	    checks, failures, COUNT(instant_vectors));
	return failures != 0;
}
