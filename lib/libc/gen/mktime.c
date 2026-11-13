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
#include "offtime.h"

#define SECSPERMIN	60
#define MINSPERHOUR	60
#define HOURSPERDAY	24
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY	((long)SECSPERHOUR * HOURSPERDAY)
#define MONSPERYEAR	12
#define TM_YEAR_BASE	1900

/*
 * The range check in timegm() derives its bounds from time_t, and the
 * derivation reads time_t as a signed type of long's width, which
 * <time.h> declares and this assertion holds it to.
 */
_Static_assert((time_t)-1 < 0 && sizeof(time_t) == sizeof(long),
    "timegm's range check reads time_t as a signed long");
#define TIME_MAX	((time_t)LONG_MAX)
#define TIME_MIN	((time_t)LONG_MIN)

/*
 * The Gregorian calendar repeats every 400 years, which are 146097 days
 * (an era). Counting years from 1 March puts the leap day last, so the days
 * before a month are one linear expression of the month, (153 m + 2) / 5
 * with March as month 0, and a year's leap days are counted by its number
 * within the era alone.
 *
 * Both counts are biased by ERA_BIAS eras before any division, so every
 * dividend is non-negative and floored division is the unsigned division
 * the compiler emits. The bias is as large as an unsigned 32-bit day count
 * allows with the top of the range still reaching the last day a signed
 * 32-bit count names: 14000 eras are 5,600,000 years and 2,045,358,000
 * days. EPOCH_DAYS is the count from 1 March of year 0 to 1 January 1970.
 * The two tm_year bounds are the years whose biased counts stay inside
 * those 32 bits.
 */
#define ERA_YEARS	400UL
#define ERA_DAYS	146097UL
#define ERA_BIAS	14000UL
#define EPOCH_DAYS	719468UL
#define BIAS_YEARS	(ERA_BIAS * ERA_YEARS + TM_YEAR_BASE)
#define BIAS_DAYS	(ERA_BIAS * ERA_DAYS + EPOCH_DAYS)
#define TM_YEAR_MIN	(-5601899L)
#define TM_YEAR_MAX	5879000L

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
 * The days from the epoch to the day before the first of the given month,
 * so that adding tm_mday gives the day the fields name. tm_mon is in range
 * and tm_year is within the two bounds above.
 */
static long
days_before_month(long tm_year, int tm_mon)
{
	unsigned long year, year_of_era, month, days;

	year = (unsigned long)(tm_year + (long)BIAS_YEARS);
	if (tm_mon < 2) {
		/* January and February close the year that began in March. */
		year--;
		month = (unsigned long)tm_mon + 10;
	} else
		month = (unsigned long)tm_mon - 2;
	year_of_era = year % ERA_YEARS;
	days = year / ERA_YEARS * ERA_DAYS + year_of_era * 365 +
	    year_of_era / 4 - year_of_era / 100 + (153 * month + 2) / 5;
	if (days > BIAS_DAYS)
		return ((long)(days - BIAS_DAYS) - 1);
	return (-(long)(BIAS_DAYS - days) - 1);
}

/*
 * The calendar conversion with no zone: the fields name a UTC time and the
 * answer is the count of seconds from the epoch to it. Every field is then
 * read back from the answer by the conversion gmtime() uses, which settles
 * a tm_mday outside its month and fills in tm_wday and tm_yday, as C
 * requires; tm_isdst is zero because no zone was consulted.
 *
 * A time outside time_t's range answers (time_t)-1. Each comparison is made
 * before the operation it guards, against a bound the operation cannot
 * pass: the day count before tm_mday is added, the whole days before they
 * are multiplied into seconds, and the seconds before the time of day is
 * added. A negative day count is taken as the day after it less a day's
 * seconds, so the product and the sum move the same way and the first
 * second of time_t's range is reached without passing it.
 */
time_t
timegm(struct tm *tm)
{
	long days, seconds;
	time_t result;

	normalize(tm);

	if (tm->tm_year < TM_YEAR_MIN || tm->tm_year > TM_YEAR_MAX)
		return ((time_t)-1);
	days = days_before_month(tm->tm_year, tm->tm_mon);
	if (tm->tm_mday > 0 ? days > LONG_MAX - tm->tm_mday :
	    days < LONG_MIN - tm->tm_mday)
		return ((time_t)-1);
	days += tm->tm_mday;

	seconds = tm->tm_hour * (long)SECSPERHOUR + tm->tm_min * SECSPERMIN +
	    tm->tm_sec;
	if (days < 0) {
		days++;
		seconds -= SECSPERDAY;
	}
	if (days > TIME_MAX / SECSPERDAY || days < TIME_MIN / SECSPERDAY)
		return ((time_t)-1);
	result = (time_t)days * SECSPERDAY;
	if (seconds < 0 ? result < TIME_MIN - seconds :
	    result > TIME_MAX - seconds)
		return ((time_t)-1);
	result += seconds;

	__discobsd_offtime(&result, 0L, tm)->tm_zone = "GMT";
	return (result);
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
