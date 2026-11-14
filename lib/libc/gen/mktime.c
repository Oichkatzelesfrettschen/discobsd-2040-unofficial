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
 * from the legitimate time (time_t)-1 (one second before 1970 in the 64-bit
 * view, 2106-02-07 06:28:15 UTC in the 32-bit view) sets a field to a
 * sentinel and reads it back, which is what usr.bin/touch does with tm_wday.
 */
#include "time.h"
#include "limits.h"
#include "offtime.h"
#ifdef __TIME64_VIEW
#include "stdint.h"
#endif

#define SECSPERMIN	60
#define MINSPERHOUR	60
#define HOURSPERDAY	24
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY	((long)SECSPERHOUR * HOURSPERDAY)
#define MONSPERYEAR	12
#define TM_YEAR_BASE	1900

#ifndef __TIME64_VIEW
/*
 * The 32-bit view: time_t is the kernel's unsigned count (utime32_t), and
 * the range is 0 to 4294967295, 1970-01-01 00:00:00 to 2106-02-07 06:28:15
 * UTC. The refusal, TIME_REFUSED, is the last count of the range, which on
 * the target is (time_t)-1, so a caller tells the two apart by the tm_wday
 * sentinel. The bounds and the refusal are written as 32-bit constants
 * rather than derived from time_t, so a host build whose unsigned long is
 * eight bytes (tests/libc_contracts) accepts and refuses exactly what the
 * target does.
 */
_Static_assert((time_t)-1 > 0 && sizeof(time_t) == sizeof(long),
    "timegm's range check reads time_t as an unsigned long");
#define TIME_MAX	((time_t)0xffffffffUL)
#define TIME_MIN	((time_t)0)
#define TIME_REFUSED	TIME_MAX

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
 * instead of carrying the wrong way. The field above is compared with the
 * room an int has left before the quotient is added; a sum outside an int
 * returns nonzero with both fields as they were.
 */
static int
carry(int *lo, int *hi, int base)
{
	int q = *lo / base;
	int r = *lo % base;

	if (r < 0) {
		r += base;
		q--;
	}
	if (q > 0 ? *hi > INT_MAX - q : *hi < INT_MIN - q)
		return (1);
	*lo = r;
	*hi += q;
	return (0);
}

/*
 * Bring tm_sec through tm_mon into range and settle tm_year with them.
 * tm_mday is left alone: a day count past the end of its month is carried
 * by the day arithmetic below, which is where the month lengths are known.
 * The answer is nonzero when a field above cannot hold what is carried into
 * it, and the fields from that one up are then as the caller gave them.
 */
static int
normalize(struct tm *tm)
{
	return (carry(&tm->tm_sec, &tm->tm_min, SECSPERMIN) ||
	    carry(&tm->tm_min, &tm->tm_hour, MINSPERHOUR) ||
	    carry(&tm->tm_hour, &tm->tm_mday, HOURSPERDAY) ||
	    carry(&tm->tm_mon, &tm->tm_year, MONSPERYEAR));
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
#else /* __TIME64_VIEW */
/*
 * mktime64.o, this file compiled with _TIME_BITS defined as 64: time_t is
 * a signed 64-bit count, and the range is the calendar's, every instant
 * whose year an int tm_year holds (offtime.h), rather than time_t's.
 */
_Static_assert((time_t)-1 < 0 && sizeof(time_t) == 8,
    "the 64-bit build reads time_t as a signed 64-bit count");
#define TIME_MAX	((time_t)CAL64_MAX)
#define TIME_MIN	((time_t)CAL64_MIN)
#define TIME_REFUSED	((time_t)-1)

/*
 * The product of value and a factor below 2^16, modulo 2^64. Each partial
 * product of a 16-bit piece stays inside 32 bits, so Thumb-1 forms it
 * without the compiler's 64-bit multiply helper, which libc does not carry.
 * A negative count passes as its two's complement and comes back as one.
 */
static uint64_t
multiply_small(uint64_t value, uint32_t factor)
{
	uint32_t high = (uint32_t)(value >> 32);
	uint32_t low = (uint32_t)value;

	return ((uint64_t)(high * factor) << 32) +
	    ((uint64_t)((low >> 16) * factor) << 16) + (low & 0xffffU) * factor;
}

/* The floored quotient of value by a positive divisor; *rest gets the remainder. */
static int
floor_divide(int value, int divisor, int *rest)
{
	int quotient = value / divisor;

	*rest = value % divisor;
	if (*rest < 0) {
		*rest += divisor;
		quotient--;
	}
	return (quotient);
}

/*
 * The days from the epoch to the day before the first of the given month,
 * for any tm_year and any tm_mon, which folds into the year. The year
 * (tm_year + 1900, plus that carry) can leave an int, so tm_year and the
 * carry are each split into eras and years of the era in 32 bits, and the
 * era count, biased by CAL64_BIAS_ERAS, is multiplied into days by the two
 * factors of an era's 146097. The answer lies within 2^40 of the epoch.
 */
static long long
days_before_month(int tm_year, int tm_mon)
{
	int carry, month, year, carry_year, era;
	uint32_t day_of_era;

	carry = floor_divide(tm_mon, MONSPERYEAR, &month);
	era = floor_divide(tm_year, (int)CAL64_ERA_YEARS, &year) +
	    floor_divide(carry, (int)CAL64_ERA_YEARS, &carry_year);
	/* 1900 is four eras and 300 years. */
	era += 4;
	year += carry_year + TM_YEAR_BASE - 4 * (int)CAL64_ERA_YEARS;
	if (month < 2) {
		/* January and February close the year that began in March. */
		year--;
		month += 10;
	} else
		month -= 2;
	while (year >= (int)CAL64_ERA_YEARS) {
		year -= (int)CAL64_ERA_YEARS;
		era++;
	}
	day_of_era = (uint32_t)year * 365 + (uint32_t)year / 4 -
	    (uint32_t)year / 100 + (153 * (uint32_t)month + 2) / 5;
	return ((long long)(multiply_small(((uint32_t)era +
	    (uint32_t)CAL64_BIAS_ERAS) * (uint32_t)CAL64_ERA_FACTOR_LOW,
	    CAL64_ERA_FACTOR_HIGH) + day_of_era - CAL64_BIAS_DAYS) - 1);
}
#endif /* __TIME64_VIEW */

/*
 * The calendar conversion with no zone: the fields name a UTC time and the
 * answer is the count of seconds from the epoch to it. Every field is then
 * read back from the answer by the conversion gmtime() uses, which settles
 * a tm_mday outside its month and fills in tm_wday and tm_yday, as C
 * requires; tm_isdst is zero because no zone was consulted.
 *
 * A time outside the range answers TIME_REFUSED, (time_t)-1. Each
 * comparison is made before the operation it guards, against a bound the
 * operation cannot pass: the day count before tm_mday is added, the whole
 * days before they are multiplied into seconds, and the seconds before the
 * time of day is added. In the 32-bit view the range starts at the epoch,
 * so a day before it is refused whole, and the time of day, which
 * normalize() leaves in 0..86399, only moves the count up.
 */
time_t
timegm(struct tm *tm)
{
#ifndef __TIME64_VIEW
	long days, seconds;
	time_t result;

	if (normalize(tm))
		return (TIME_REFUSED);

	if (tm->tm_year < TM_YEAR_MIN || tm->tm_year > TM_YEAR_MAX)
		return (TIME_REFUSED);
	days = days_before_month(tm->tm_year, tm->tm_mon);
	if (tm->tm_mday > 0 ? days > LONG_MAX - tm->tm_mday :
	    days < LONG_MIN - tm->tm_mday)
		return (TIME_REFUSED);
	days += tm->tm_mday;

	seconds = tm->tm_hour * (long)SECSPERHOUR + tm->tm_min * SECSPERMIN +
	    tm->tm_sec;
	if (days < 0 || (time_t)days > TIME_MAX / SECSPERDAY)
		return (TIME_REFUSED);
	result = (time_t)days * SECSPERDAY;
	if (result > TIME_MAX - (time_t)seconds)
		return (TIME_REFUSED);
	result += (time_t)seconds;
#else
	/*
	 * Every field enters the count as it stands, with no carry between
	 * fields that an int could overflow: the day count is 64 bits wide
	 * for any tm_year, tm_mon and tm_mday, and the products by a day, an
	 * hour and a minute stay within 2^63 for any int field, so the sum
	 * formed modulo 2^64 is the true count. One comparison against the
	 * calendar's range then decides it, and the conversion back
	 * normalizes the fields.
	 */
	uint64_t count;
	time_t result;

	count = multiply_small((uint64_t)(days_before_month(tm->tm_year,
	    tm->tm_mon) + tm->tm_mday), SECSPERDAY / 128) << 7;
	count += multiply_small((uint64_t)(long long)tm->tm_hour, SECSPERHOUR);
	count += multiply_small((uint64_t)(long long)tm->tm_min, SECSPERMIN);
	count += (uint64_t)(long long)tm->tm_sec;
	result = (time_t)count;
	if (result < TIME_MIN || result > TIME_MAX)
		return (TIME_REFUSED);
#endif

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

	/*
	 * The 64-bit build searches from the fields as they stand: its
	 * timegm() sums them without a carry, where a carry into an int could
	 * refuse an instant inside the range, and the struct is rewritten
	 * only with the answer.
	 */
#ifndef __TIME64_VIEW
	if (normalize(tm))
		return (TIME_REFUSED);
#endif

	/*
	 * The answer as though the fields were UTC, which starts the search.
	 * Fields that read as an instant outside the range in UTC are refused
	 * here even when the local time they name lies inside it: in the
	 * 32-bit view, a local time in a zone west of Greenwich whose fields
	 * fall before 1970-01-01 00:00:00, the first hours of the range there.
	 */
	probe = *tm;
	guess = timegm(&probe);
	if (guess == TIME_REFUSED)
		return (TIME_REFUSED);

	/*
	 * Each pass asks what offset is in force at the current answer and
	 * moves the answer by it. Two passes settle every zone whose offset
	 * is piecewise constant; the third is the bound, so a zone file that
	 * is not gives up rather than looping.
	 */
	for (pass = 0; pass < 3; pass++) {
		back = localtime(&guess);
		if (back == NULL)
			return (TIME_REFUSED);

		/*
		 * localtime reports the zone's offset at that instant in
		 * tm_gmtoff, seconds east of UTC.
		 */
		offset = back->tm_gmtoff;

		probe = *tm;
		probe.tm_isdst = back->tm_isdst;
		guess = timegm(&probe);
		if (guess == TIME_REFUSED)
			return (TIME_REFUSED);
		/*
		 * The fields are in range as UTC; the instant they name in
		 * this zone lies offset seconds away and may not be.
		 */
		if (offset < 0 ? guess > TIME_MAX + offset :
		    guess < TIME_MIN + offset)
			return (TIME_REFUSED);
		guess -= offset;
	}

	/*
	 * The caller's struct reports the time that was reached, normalized
	 * and with tm_wday, tm_yday and tm_isdst filled in, as C requires.
	 */
	back = localtime(&guess);
	if (back == NULL)
		return (TIME_REFUSED);
	*tm = *back;

	return (guess);
}
