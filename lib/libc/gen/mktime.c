/*
 * mktime and timegm: the inverse of localtime and gmtime.
 *
 * ctime.c carries the forward direction, reading a time_t and the zone file
 * into a struct tm. The inverse has no closed form once a zone is involved,
 * because the offset it must subtract is the offset in force at the answer
 * and that is what is being solved for. timegm is the part that does have
 * one: with no zone the conversion is calendar arithmetic alone, so it is
 * written directly and mktime searches with it.
 *
 * The search converges in two steps for every zone this system can load.
 * timegm gives a first answer as though the fields were UTC; localtime on
 * that answer reports the offset in force near it, and subtracting the
 * offset gives a second. A transition between the two answers moves the
 * offset once more, and one further step settles it, because a zone's
 * offset is piecewise constant and the step is bounded by the size of the
 * jump. The loop bound is what makes that a statement rather than a hope.
 *
 * mktime also normalizes out-of-range fields, as C requires: tm_mon of 12
 * means January of the following year, tm_mday of 0 means the last day of
 * the previous month. The normalization runs before the search, so the
 * search only ever sees a proper date.
 *
 * On overflow both return (time_t)-1 without touching the caller's struct
 * beyond the normalization C requires. A caller that must tell that apart
 * from the legitimate time -1 sets a field to a sentinel and reads it back,
 * which is what usr.bin/touch does with tm_wday.
 */
#include "time.h"
#include "limits.h"

#define SECSPERMIN	60
#define MINSPERHOUR	60
#define HOURSPERDAY	24
#define DAYSPERWEEK	7
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY	((long)SECSPERHOUR * HOURSPERDAY)
#define MONSPERYEAR	12
#define EPOCH_YEAR	1970
#define EPOCH_WDAY	4			/* 1970-01-01 was a Thursday */
#define TM_YEAR_BASE	1900

#define isleap(y)	(((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

static const int mon_lengths[2][MONSPERYEAR] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static const int year_lengths[2] = { 365, 366 };

/*
 * Fold a field that has run past its base into the field above it. The
 * quotient is floored rather than truncated, so a negative field borrows
 * instead of carrying the wrong way.
 */
static void
carry(int *lo, int *hi, int base)
{
	int q = *lo / base;
	int r = *lo % base;

	if (r < 0) {
		r += base;
		q--;
	}
	*lo = r;
	*hi += q;
}

/*
 * Bring tm_sec through tm_mon into range and settle tm_year with them.
 * tm_mday is left alone: a day count past the end of its month is carried
 * by the day arithmetic below, which is where the month lengths are known.
 */
static void
normalize(struct tm *tm)
{
	carry(&tm->tm_sec, &tm->tm_min, SECSPERMIN);
	carry(&tm->tm_min, &tm->tm_hour, MINSPERHOUR);
	carry(&tm->tm_hour, &tm->tm_mday, HOURSPERDAY);
	carry(&tm->tm_mon, &tm->tm_year, MONSPERYEAR);
}

/*
 * The days from the epoch to the first of the given year, which is where
 * the per-year loop would otherwise run for as many iterations as there are
 * years. Counting leap days by their own rule keeps it a constant-time
 * calculation over the whole representable range.
 */
static long
days_before_year(int year)
{
	long y = year - 1;
	long e = EPOCH_YEAR - 1;

	return ((y * 365 + y / 4 - y / 100 + y / 400) -
	    (e * 365 + e / 4 - e / 100 + e / 400));
}

/*
 * The calendar conversion with no zone: the fields name a UTC time and the
 * answer is the count of seconds from the epoch to it. tm_wday and tm_yday
 * are filled in from the result, as C requires, and tm_isdst is cleared
 * because no zone was consulted.
 */
time_t
timegm(struct tm *tm)
{
	long days, secs, yday;
	int i, leap, year;

	normalize(tm);

	year = tm->tm_year + TM_YEAR_BASE;
	leap = isleap(year);

	/* The day of the year, which tm_mday may still carry past. */
	yday = 0;
	for (i = 0; i < tm->tm_mon; i++)
		yday += mon_lengths[leap][i];
	yday += tm->tm_mday - 1;

	days = days_before_year(year) + yday;

	/*
	 * A tm_mday past its month, or below 1, has moved the date into
	 * another year; recover the year the day count actually names so
	 * tm_year and tm_yday report it.
	 */
	while (yday < 0) {
		year--;
		yday += year_lengths[isleap(year)];
	}
	while (yday >= year_lengths[isleap(year)]) {
		yday -= year_lengths[isleap(year)];
		year++;
	}

	if (year - TM_YEAR_BASE < INT_MIN || year - TM_YEAR_BASE > INT_MAX)
		return ((time_t)-1);
	tm->tm_year = year - TM_YEAR_BASE;
	tm->tm_yday = (int)yday;

	/* The month and day within the year the day count names. */
	leap = isleap(year);
	for (i = 0; yday >= mon_lengths[leap][i]; i++)
		yday -= mon_lengths[leap][i];
	tm->tm_mon = i;
	tm->tm_mday = (int)yday + 1;

	tm->tm_wday = (int)((days + EPOCH_WDAY) % DAYSPERWEEK);
	if (tm->tm_wday < 0)
		tm->tm_wday += DAYSPERWEEK;
	tm->tm_isdst = 0;

	/*
	 * time_t is a signed 32-bit count here, so the day count alone can
	 * leave the range before the seconds are added. The bound is checked
	 * on the days rather than on the product, because the product is
	 * what would wrap.
	 */
	if (days > (long)(INT_MAX / SECSPERDAY) + 1 ||
	    days < (long)(INT_MIN / SECSPERDAY) - 1)
		return ((time_t)-1);

	secs = days * SECSPERDAY;
	secs += tm->tm_hour * SECSPERHOUR;
	secs += tm->tm_min * SECSPERMIN;
	secs += tm->tm_sec;

	return ((time_t)secs);
}

/*
 * The same conversion through the local zone. The fields name a local time,
 * so the answer is the instant whose localtime() is those fields.
 */
time_t
mktime(struct tm *tm)
{
	struct tm probe, *back;
	time_t guess;
	long offset;
	int pass;

	normalize(tm);

	/* The answer as though the fields were UTC, which starts the search. */
	probe = *tm;
	guess = timegm(&probe);
	if (guess == (time_t)-1)
		return ((time_t)-1);

	/*
	 * Each pass asks what offset is in force at the current answer and
	 * moves the answer by it. Two passes settle every zone whose offset
	 * is piecewise constant; the third is the bound, so a zone file that
	 * is not gives up rather than looping.
	 */
	for (pass = 0; pass < 3; pass++) {
		back = localtime(&guess);
		if (back == NULL)
			return ((time_t)-1);

		/*
		 * The offset is the difference between the fields localtime
		 * reports and the instant they were read from, which is the
		 * zone's offset at that instant.
		 */
		probe = *back;
		offset = (long)(timegm(&probe) - guess);

		probe = *tm;
		probe.tm_isdst = back->tm_isdst;
		guess = timegm(&probe);
		if (guess == (time_t)-1)
			return ((time_t)-1);
		guess -= offset;
	}

	/*
	 * The caller's struct reports the time that was reached, normalized
	 * and with tm_wday, tm_yday and tm_isdst filled in, as C requires.
	 */
	back = localtime(&guess);
	if (back == NULL)
		return ((time_t)-1);
	*tm = *back;

	return (guess);
}
