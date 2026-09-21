/*
 * Contract gate for lib/libc/gen/mktime.c, linked against the ctime.c it
 * inverts. Both are compiled from the tree's own sources with the names
 * moved aside, so the gate measures those and not the host's.
 *
 * The property that decides mktime is the round trip: for every instant the
 * library can represent, mktime(localtime(&t)) is t again. That needs no
 * table of expected answers and holds in whatever zone is loaded, so the
 * gate sweeps instants rather than naming them. timegm is the same statement
 * against gmtime, and it also has a closed form, so a set of dates whose
 * second counts are known independently pins the arithmetic itself.
 *
 * The gate fixes the zone to GMT, which tzset() selects when TZ is set and
 * empty: with TZ unset it would load the host's own zone file, and this
 * tree's tzload reads the first-version layout alone while a current host
 * ships a file whose first-version block is degenerate. The round trip is
 * the same statement in any zone, so fixing it costs the gate nothing and
 * keeps the answers from depending on which host runs it. A zone carrying a
 * transition is the case this cannot reach.
 */
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>

int		creat(const char *, mode_t);
void		db_tzset(void);

time_t		db_mktime(struct tm *);
time_t		db_timegm(struct tm *);
struct tm      *db_localtime(const time_t *);
struct tm      *db_gmtime(const time_t *);

static int failure_count;
static int check_count;

static void
check(int condition, const char *message)
{
	++check_count;
	if (condition)
		return;
	(void)write(STDERR_FILENO, "FAIL ", 5);
	(void)write(STDERR_FILENO, message, strlen(message));
	(void)write(STDERR_FILENO, "\n", 1);
	++failure_count;
}

static void
report(const char *message, long value)
{
	char digits[24];
	int i = (int)sizeof(digits);
	int negative = value < 0;
	unsigned long magnitude = negative ? -(unsigned long)value :
	    (unsigned long)value;

	digits[--i] = '\n';
	if (magnitude == 0)
		digits[--i] = '0';
	while (magnitude != 0 && i > 1) {
		digits[--i] = (char)('0' + magnitude % 10);
		magnitude /= 10;
	}
	if (negative && i > 0)
		digits[--i] = '-';
	(void)write(STDERR_FILENO, "  ", 2);
	(void)write(STDERR_FILENO, message, strlen(message));
	(void)write(STDERR_FILENO, digits + i, sizeof(digits) - (size_t)i);
}

/*
 * A date whose second count from the epoch is known independently, so the
 * arithmetic is pinned rather than only self-consistent.
 */
struct known {
	int	year, mon, mday, hour, min, sec;
	long	secs;
	int	wday, yday;
};

static const struct known knowns[] = {
	/* The epoch itself: a Thursday, day 0 of 1970. */
	{ 1970,  1,  1,  0,  0,  0,           0L, 4,   0 },
	/* One second later, which pins the unit. */
	{ 1970,  1,  1,  0,  0,  1,           1L, 4,   0 },
	/* The first day of 1971, 365 days on. */
	{ 1971,  1,  1,  0,  0,  0,    31536000L, 5,   0 },
	/* 1972 is a leap year, so its 29 February exists and is day 59. */
	{ 1972,  2, 29,  0,  0,  0,    68169600L, 2,  59 },
	{ 1972,  3,  1,  0,  0,  0,    68256000L, 3,  60 },
	/* 2000 is a leap year by the 400-year rule, unlike 1900. */
	{ 2000,  2, 29,  0,  0,  0,   951782400L, 2,  59 },
	{ 2000,  3,  1,  0,  0,  0,   951868800L, 3,  60 },
	/* A round billion. */
	{ 2001,  9,  9,  1, 46, 40,  1000000000L, 0, 251 },
	/* The last second a signed 32-bit time_t represents. */
	{ 2038,  1, 19,  3, 14,  7,  2147483647L, 2,  18 },
};

#define NKNOWN	((int)(sizeof(knowns) / sizeof(knowns[0])))

static void
fill(struct tm *tm, const struct known *k)
{
	memset(tm, 0, sizeof(*tm));
	tm->tm_year = k->year - 1900;
	tm->tm_mon = k->mon - 1;
	tm->tm_mday = k->mday;
	tm->tm_hour = k->hour;
	tm->tm_min = k->min;
	tm->tm_sec = k->sec;
	tm->tm_isdst = -1;
}

/* timegm reaches the count the calendar names, and reports the day it lands on. */
static void
test_timegm_known_dates(void)
{
	struct tm tm;
	int i;

	for (i = 0; i < NKNOWN; i++) {
		fill(&tm, &knowns[i]);
		check(db_timegm(&tm) == (time_t)knowns[i].secs,
		    "timegm: wrong second count for a known date");
		check(tm.tm_wday == knowns[i].wday,
		    "timegm: wrong day of the week");
		check(tm.tm_yday == knowns[i].yday,
		    "timegm: wrong day of the year");
		check(tm.tm_year == knowns[i].year - 1900 &&
		    tm.tm_mon == knowns[i].mon - 1 &&
		    tm.tm_mday == knowns[i].mday,
		    "timegm: an in-range date was altered");
	}
}

/* mktime agrees with timegm while the zone is UTC, which is how it is run. */
static void
test_mktime_known_dates(void)
{
	struct tm tm;
	int i;

	for (i = 0; i < NKNOWN; i++) {
		fill(&tm, &knowns[i]);
		check(db_mktime(&tm) == (time_t)knowns[i].secs,
		    "mktime: wrong second count for a known date");
		check(tm.tm_wday == knowns[i].wday,
		    "mktime: wrong day of the week");
		check(tm.tm_yday == knowns[i].yday,
		    "mktime: wrong day of the year");
	}
}

/*
 * The round trip, which is what makes mktime the inverse rather than a
 * second calendar. The step is prime, so the instants swept share no divisor
 * with a day, an hour or a year.
 */
#define STEP	1999993L		/* prime, about 23 days */
#define SWEEP_LO	(-2147000000L)
#define SWEEP_HI	2147000000L

/*
 * The sweep walks the instant itself rather than a multiplier, because long
 * is four bytes at the target's width and a product would leave its range
 * long before the sweep did.
 */
static void
test_round_trip(void)
{
	struct tm *back, copy;
	time_t t, again;
	long v;
	int trips = 0;

	for (v = SWEEP_LO; v <= SWEEP_HI - STEP; v += STEP) {
		t = (time_t)v;
		back = db_localtime(&t);
		if (back == 0)
			continue;
		copy = *back;
		copy.tm_isdst = -1;
		again = db_mktime(&copy);
		check(again == t, "mktime: the round trip moved the instant");
		trips++;
	}
	report("round trip instants: ", trips);
	check(trips > 2000, "mktime: the sweep covered too few instants");
}

/* The same statement against gmtime, which timegm inverts exactly. */
static void
test_gm_round_trip(void)
{
	struct tm *back, copy;
	time_t t, again;
	long v;

	for (v = SWEEP_LO; v <= SWEEP_HI - STEP; v += STEP) {
		t = (time_t)v;
		back = db_gmtime(&t);
		if (back == 0)
			continue;
		copy = *back;
		again = db_timegm(&copy);
		check(again == t, "timegm: the round trip moved the instant");
	}
}

/*
 * C requires the fields to be normalized, which is what lets a caller add to
 * one and ask for the result.
 */
static void
test_normalization(void)
{
	struct tm tm;

	/* Month 12 is January of the following year. */
	fill(&tm, &knowns[0]);
	tm.tm_mon = 12;
	check(db_timegm(&tm) == (time_t)31536000L,
	    "normalize: month 12 is not the next January");
	check(tm.tm_year == 71 && tm.tm_mon == 0,
	    "normalize: month 12 left the fields wrong");

	/* Day 32 of January is 1 February. */
	fill(&tm, &knowns[0]);
	tm.tm_mday = 32;
	check(db_timegm(&tm) == (time_t)(31L * 86400L),
	    "normalize: day 32 of January is not 1 February");
	check(tm.tm_mon == 1 && tm.tm_mday == 1,
	    "normalize: day 32 left the fields wrong");

	/* Day 0 of January is 31 December of the year before. */
	fill(&tm, &knowns[0]);
	tm.tm_mday = 0;
	check(db_timegm(&tm) == (time_t)(-86400L),
	    "normalize: day 0 is not the last day of the year before");
	check(tm.tm_year == 69 && tm.tm_mon == 11 && tm.tm_mday == 31,
	    "normalize: day 0 left the fields wrong");

	/* Hour 24 is midnight of the following day. */
	fill(&tm, &knowns[0]);
	tm.tm_hour = 24;
	check(db_timegm(&tm) == (time_t)86400L,
	    "normalize: hour 24 is not the next midnight");
	check(tm.tm_mday == 2 && tm.tm_hour == 0,
	    "normalize: hour 24 left the fields wrong");

	/* A negative second borrows from the minute above it. */
	fill(&tm, &knowns[2]);
	tm.tm_sec = -1;
	check(db_timegm(&tm) == (time_t)(knowns[2].secs - 1),
	    "normalize: a negative second did not borrow");

	/* Seconds spanning whole days fold all the way up. */
	fill(&tm, &knowns[0]);
	tm.tm_sec = 86400 * 3;
	check(db_timegm(&tm) == (time_t)(3L * 86400L),
	    "normalize: three days of seconds did not carry");
	check(tm.tm_mday == 4, "normalize: the carried day is wrong");
}

/* 1900 and 2100 are not leap years and 2000 is: the rule has three parts. */
static void
test_leap_year_rule(void)
{
	struct tm tm;

	/* 2000-12-31 is day 365, so that year had 366 days. */
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 100;
	tm.tm_mon = 11;
	tm.tm_mday = 31;
	tm.tm_isdst = -1;
	check(db_timegm(&tm) != (time_t)-1, "leap: 2000-12-31 was refused");
	check(tm.tm_yday == 365, "leap: 2000 was not a leap year");

	/* 2100-02-29 does not exist, so it normalizes to 1 March. */
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 200;
	tm.tm_mon = 1;
	tm.tm_mday = 29;
	tm.tm_isdst = -1;
	(void)db_timegm(&tm);
	check(tm.tm_mon == 2 && tm.tm_mday == 1,
	    "leap: 2100 was treated as a leap year");

	/* 2004-12-31 is day 365 again. */
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 104;
	tm.tm_mon = 11;
	tm.tm_mday = 31;
	tm.tm_isdst = -1;
	check(db_timegm(&tm) != (time_t)-1, "leap: 2004-12-31 was refused");
	check(tm.tm_yday == 365, "leap: 2004 was not a leap year");
}

/*
 * A time that cannot be represented answers -1. A caller tells that from the
 * legitimate time -1 by a sentinel it reads back, which is what
 * usr.bin/touch does with tm_wday.
 */
static void
test_out_of_range(void)
{
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 20000 - 1900;
	tm.tm_mday = 1;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1,
	    "range: a year past the representable span was accepted");

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = -20000 - 1900;
	tm.tm_mday = 1;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1,
	    "range: a year before the representable span was accepted");

	/* The legitimate time -1 is one second before the epoch. */
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 69;
	tm.tm_mon = 11;
	tm.tm_mday = 31;
	tm.tm_hour = 23;
	tm.tm_min = 59;
	tm.tm_sec = 59;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1,
	    "range: the second before the epoch is not -1");
	check(tm.tm_wday != -1,
	    "range: the legitimate -1 left the sentinel in place");
}

/*
 * A zone file of the first version, which is the one this tree's tzload
 * reads: thirty-two reserved bytes, three counts, and one local time type.
 * No transitions, so the offset is the same at every instant, which is what
 * makes the answers here checkable by arithmetic.
 *
 * Without it the gate would run in GMT alone, where the offset is zero and
 * the search that finds it cannot be seen to work at all.
 */
#define ZONE_OFFSET	(-18000L)	/* five hours west */
#define ZONE_PATH	"/tmp/discobsd_mktime_zone"

static void
put32(unsigned char *p, long v)
{
	unsigned long u = (unsigned long)v;

	p[0] = (unsigned char)((u >> 24) & 0xff);
	p[1] = (unsigned char)((u >> 16) & 0xff);
	p[2] = (unsigned char)((u >> 8) & 0xff);
	p[3] = (unsigned char)(u & 0xff);
}

static int
write_zone(void)
{
	unsigned char buf[54];
	int fd;

	memset(buf, 0, sizeof(buf));
	memcpy(buf, "TZif", 4);		/* the reserved span, which tzload skips */
	put32(buf + 32, 0);		/* tzh_timecnt */
	put32(buf + 36, 1);		/* tzh_typecnt */
	put32(buf + 40, 4);		/* tzh_charcnt */
	put32(buf + 44, ZONE_OFFSET);	/* the one type's offset from GMT */
	buf[48] = 0;			/* tt_isdst */
	buf[49] = 0;			/* tt_abbrind */
	memcpy(buf + 50, "EST", 4);	/* the abbreviation and its terminator */

	/*
	 * creat(), not open(): the open flags this tree's fcntl.h spells are
	 * the target kernel's numbers, which are not the host kernel's, and
	 * creat() carries none.
	 */
	fd = creat(ZONE_PATH, 0644);
	if (fd == -1)
		return (-1);
	if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
		(void)close(fd);
		return (-1);
	}
	return (close(fd));
}

/*
 * The same statements again in a zone whose offset is not zero. mktime must
 * now subtract an offset to reach the instant, and timegm must not, so the
 * two differ by exactly the zone's offset -- which is the arithmetic the
 * search exists to perform.
 */
static void
test_zone_offset(void)
{
	struct tm tm, *back, copy;
	time_t local, utc, t, again;
	long v;
	int i, trips = 0;

	if (write_zone() == -1) {
		(void)write(STDERR_FILENO,
		    "  zone file unavailable; offset cases skipped\n", 45);
		return;
	}
	putenv("TZ=" ZONE_PATH);
	/*
	 * tzset() reads the environment once and remembers that it has, so
	 * the zone only changes when it is asked again.
	 */
	db_tzset();

	for (i = 0; i < NKNOWN; i++) {
		fill(&tm, &knowns[i]);
		local = db_mktime(&tm);
		fill(&tm, &knowns[i]);
		utc = db_timegm(&tm);
		if (utc == (time_t)-1 || local == (time_t)-1)
			continue;
		/*
		 * The fields name a local time, so the instant they name is
		 * later than the same fields read as GMT by the offset west.
		 */
		check((long)(local - utc) == -ZONE_OFFSET,
		    "zone: mktime did not apply the zone's offset");
	}

	/* The round trip holds through the zone, which is the whole claim. */
	for (v = SWEEP_LO; v <= SWEEP_HI - STEP; v += STEP) {
		t = (time_t)v;
		back = db_localtime(&t);
		if (back == 0)
			continue;
		check(back->tm_gmtoff == ZONE_OFFSET,
		    "zone: the zone file was not the one loaded");
		copy = *back;
		copy.tm_isdst = -1;
		again = db_mktime(&copy);
		check(again == t, "zone: the round trip moved the instant");
		trips++;
	}
	report("zone round trip instants: ", trips);
	check(trips > 2000, "zone: the sweep covered too few instants");

	(void)unlink(ZONE_PATH);
	putenv("TZ=");
	db_tzset();
}

int
main(void)
{
	/*
	 * TZ set and empty is tzset()'s own selector for GMT, and the only
	 * one that does not consult a file this tree's tzload cannot read.
	 */
	putenv("TZ=");

	test_timegm_known_dates();
	test_mktime_known_dates();
	test_round_trip();
	test_gm_round_trip();
	test_normalization();
	test_leap_year_rule();
	test_out_of_range();
	test_zone_offset();

	report("checks: ", check_count);
	if (failure_count != 0) {
		report("failures: ", failure_count);
		return (1);
	}
	(void)write(STDERR_FILENO, "mktime contract tests passed\n", 29);
	return (0);
}
