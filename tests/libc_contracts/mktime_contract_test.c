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

	/*
	 * 2100 lies past the time_t this library is built with, so its
	 * February is refused and the sentinel stays. The century rule is
	 * measured where time_t reaches it: test_every_day() counts through
	 * 2000, which the 400-year rule keeps a leap year, and through the
	 * years on either side, whose day counts carry the century term.
	 */
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 200;
	tm.tm_mon = 1;
	tm.tm_mday = 29;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1 && tm.tm_wday == -1,
	    "leap: a date past time_t's range was accepted");

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
	check(tm.tm_wday == -1,
	    "range: a refused year past the span rewrote the sentinel");

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = -20000 - 1900;
	tm.tm_mday = 1;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1,
	    "range: a year before the representable span was accepted");
	check(tm.tm_wday == -1,
	    "range: a refused year before the span rewrote the sentinel");

	/* The years an int names at its two ends. */
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = INT_MAX;
	tm.tm_mday = 1;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1 && tm.tm_wday == -1,
	    "range: the last year an int names was accepted");
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = INT_MIN;
	tm.tm_mday = 1;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1 && tm.tm_wday == -1,
	    "range: the first year an int names was accepted");

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
 * The two ends of time_t's range, which <limits.h> gives as LONG_MIN and
 * LONG_MAX, with instants one second and one day inside and outside each.
 * An instant past either end has no count, so it answers -1 and leaves the
 * tm_wday sentinel in place. The fields name each instant directly, so the
 * answers do not pass through gmtime.
 */
struct edge {
	int		year, mon, mday, hour, min, sec;
	long long	secs;
};

static const struct edge edges[] = {
	{ 2038,  1, 19,  3, 14,  7,  2147483647LL },
	{ 2038,  1, 19,  3, 14,  8,  2147483648LL },
	{ 2038,  1, 19, 23,  0,  0,  2147554800LL },
	{ 2038,  1, 20,  0,  0,  0,  2147558400LL },
	{ 1901, 12, 13, 20, 45, 52, -2147483648LL },
	{ 1901, 12, 13, 20, 45, 51, -2147483649LL },
	{ 1901, 12, 13,  0,  0,  0, -2147558400LL },
	{ 1901, 12, 12, 23, 59, 59, -2147558401LL },
};

static void
test_range_edges(void)
{
	struct tm tm;
	time_t got;
	int fits;
	unsigned i;

	for (i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
		memset(&tm, 0, sizeof(tm));
		tm.tm_year = edges[i].year - 1900;
		tm.tm_mon = edges[i].mon - 1;
		tm.tm_mday = edges[i].mday;
		tm.tm_hour = edges[i].hour;
		tm.tm_min = edges[i].min;
		tm.tm_sec = edges[i].sec;
		tm.tm_wday = -1;
		fits = edges[i].secs >= (long long)LONG_MIN &&
		    edges[i].secs <= (long long)LONG_MAX;
		got = db_timegm(&tm);
		if (fits) {
			check((long long)got == edges[i].secs,
			    "edge: a representable instant has the wrong count");
			check(tm.tm_wday != -1,
			    "edge: a representable instant left the sentinel");
		} else {
			check(got == (time_t)-1,
			    "edge: an instant past time_t's range was accepted");
			check(tm.tm_wday == -1,
			    "edge: a refused instant rewrote the fields");
		}
	}

	/* A day count no calendar reaches, carried in tm_mday alone. */
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 70;
	tm.tm_mday = INT_MAX;
	got = db_timegm(&tm);
	check(got == (time_t)-1,
	    "edge: the largest tm_mday was accepted");
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 70;
	tm.tm_mday = INT_MIN;
	got = db_timegm(&tm);
	check(got == (time_t)-1,
	    "edge: the smallest tm_mday was accepted");
}

/*
 * Every whole day a four-byte time_t names, against a calendar that only counts:
 * it steps one day at a time from the epoch in each direction and carries
 * the month, year, weekday and day of the year by their own rules, sharing
 * no arithmetic with the library. timegm() must reach the day's count from
 * the fields, and gmtime() must read the same fields back from the count.
 */
static int
month_days(int year, int mon)
{
	static const int lengths[12] =
	    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (mon == 1 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
		return (29);
	return (lengths[mon]);
}

static void
check_day(long day, int year, int mon, int mday, int wday, int yday)
{
	struct tm tm, *back;
	time_t t;
	long second = (long)(((unsigned long)day * 7919UL) % 86400UL);

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = year - 1900;
	tm.tm_mon = mon;
	tm.tm_mday = mday;
	tm.tm_hour = (int)(second / 3600);
	tm.tm_min = (int)(second / 60 % 60);
	tm.tm_sec = (int)(second % 60);
	tm.tm_wday = -1;
	t = db_timegm(&tm);
	check(t == (time_t)(day * 86400L + second),
	    "every day: timegm has the wrong count");
	check(tm.tm_wday == wday && tm.tm_yday == yday &&
	    tm.tm_year == year - 1900 && tm.tm_mon == mon &&
	    tm.tm_mday == mday,
	    "every day: timegm reported the wrong fields");
	t = (time_t)(day * 86400L + second);
	back = db_gmtime(&t);
	check(back != 0 && back->tm_year == year - 1900 &&
	    back->tm_mon == mon && back->tm_mday == mday &&
	    back->tm_wday == wday && back->tm_yday == yday &&
	    back->tm_hour == (int)(second / 3600) &&
	    back->tm_min == (int)(second / 60 % 60) &&
	    back->tm_sec == (int)(second % 60),
	    "every day: gmtime read the wrong fields");
}

#define LAST_WHOLE_DAY	24854L		/* 2038-01-18 */
#define FIRST_WHOLE_DAY	(-24855L)	/* 1901-12-14 */

static void
test_every_day(void)
{
	long day;
	int year, mon, mday, wday, yday;

	year = 1970; mon = 0; mday = 1; wday = 4; yday = 0;
	for (day = 0; day <= LAST_WHOLE_DAY; day++) {
		check_day(day, year, mon, mday, wday, yday);
		wday = (wday + 1) % 7;
		yday++;
		if (++mday > month_days(year, mon)) {
			mday = 1;
			if (++mon == 12) {
				mon = 0;
				yday = 0;
				year++;
			}
		}
	}
	check(year == 2038 && mon == 0 && mday == 19,
	    "every day: the counting calendar ended on the wrong date");

	year = 1970; mon = 0; mday = 1; wday = 4; yday = 0;
	for (day = -1; day >= FIRST_WHOLE_DAY; day--) {
		wday = (wday + 6) % 7;
		if (--mday < 1) {
			if (--mon < 0) {
				mon = 11;
				year--;
				yday = 365 + (month_days(year, 1) == 29);
			}
			mday = month_days(year, mon);
		}
		yday--;
		check_day(day, year, mon, mday, wday, yday);
	}
	check(year == 1901 && mon == 11 && mday == 14,
	    "every day: the counting calendar began on the wrong date");
}

/*
 * Field carrying adds a quotient to the field above, and that sum can leave
 * an int before any range check on the result runs. The second case is the
 * one a wrapped sum would answer wrongly rather than refuse: INT_MAX minutes
 * and sixty seconds wrap to a large negative minute count, which the hour
 * field given here cancels to 00:52 on the epoch day, an accepted time,
 * while the fields name an instant some four thousand years on.
 */
static void
test_carry_overflow(void)
{
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = INT_MAX;
	tm.tm_mon = 12;
	tm.tm_mday = 1;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1 && tm.tm_wday == -1,
	    "carry: a month carried past the last year was accepted");
	check(tm.tm_year == INT_MAX && tm.tm_mon == 12,
	    "carry: a refused month carry rewrote its fields");

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 70;
	tm.tm_mday = 1;
	tm.tm_hour = 35791395;
	tm.tm_min = INT_MAX;
	tm.tm_sec = 60;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1 && tm.tm_wday == -1,
	    "carry: seconds carried past the last minute were accepted");

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 70;
	tm.tm_mday = INT_MAX;
	tm.tm_hour = 24;
	tm.tm_wday = -1;
	check(db_timegm(&tm) == (time_t)-1 && tm.tm_wday == -1,
	    "carry: hours carried past the last day were accepted");

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = INT_MIN;
	tm.tm_mon = -1;
	tm.tm_mday = 1;
	tm.tm_isdst = -1;
	tm.tm_wday = -1;
	check(db_mktime(&tm) == (time_t)-1 && tm.tm_wday == -1,
	    "carry: mktime accepted a month borrowed before the first year");
}

/* timegm reports the zone it read the fields in, as gmtime does. */
static void
test_timegm_zone_fields(void)
{
	struct tm tm;

	fill(&tm, &knowns[0]);
	tm.tm_gmtoff = 12345L;
	tm.tm_zone = 0;
	(void)db_timegm(&tm);
	check(tm.tm_gmtoff == 0 && tm.tm_isdst == 0,
	    "zone fields: timegm left an offset or a daylight flag");
	check(tm.tm_zone != 0 && strcmp(tm.tm_zone, "GMT") == 0,
	    "zone fields: timegm did not name GMT");
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

	/*
	 * The last instant of the range round-trips through the zone, and
	 * the same fields read as this zone's local time five hours later
	 * name an instant past it, which is refused.
	 */
	t = (time_t)LONG_MAX;
	back = db_localtime(&t);
	if (back != 0) {
		copy = *back;
		copy.tm_isdst = -1;
		check(db_mktime(&copy) == t,
		    "zone: the last instant of the range did not round-trip");
	}
	back = db_gmtime(&t);
	if (back != 0) {
		copy = *back;
		copy.tm_isdst = -1;
		copy.tm_wday = -1;
		check(db_mktime(&copy) == (time_t)-1 && copy.tm_wday == -1,
		    "zone: a local time past the range was accepted");
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
	test_range_edges();
	test_carry_overflow();
	test_every_day();
	test_timegm_zone_fields();
	test_zone_offset();

	report("checks: ", check_count);
	if (failure_count != 0) {
		report("failures: ", failure_count);
		return (1);
	}
	(void)write(STDERR_FILENO, "mktime contract tests passed\n", 29);
	return (0);
}
