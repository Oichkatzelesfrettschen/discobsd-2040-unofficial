/*
 * Copyright (c) 1987 Regents of the University of California.
 * This file may be freely redistributed provided that this
 * notice remains attached.
 */
/*
 * The forward calendar conversion: ctime(), localtime() and gmtime(), and
 * __discobsd_offtime(), which they and mktime.c share (offtime.h). The
 * zone they read is tzset.c's.
 *
 * The file is compiled once for each width of time_t (sys/_time_t.h):
 * ctime.o at the program default, and ctime64.o with _TIME_BITS defined
 * as 64 (lib/libc/gen/Makefile), where <time.h> binds the three public
 * names to __ctime64, __localtime64 and __gmtime64 and offtime.h binds the
 * conversion to __discobsd_offtime64. Each build returns its own struct
 * tm, and only the 64-bit build can fail: its range is bounded by the int
 * tm_year rather than by time_t.
 */
#include "sys/time.h"
#include "offtime.h"
#include "tzstate.h"
#ifdef __TIME64_VIEW
#include "errno.h"
#include "stdint.h"
#endif

#ifdef TZ_ZONEINFO
/*
 * Nonzero when the instant precedes a zone file's transition. A transition
 * is the signed four-byte count the file stores (tzstate.h), so in the
 * 32-bit view, where time_t is the unsigned utime32_t, a transition before
 * 1970 precedes every instant and is never compared as a count near 2^32.
 */
#ifdef __TIME64_VIEW
#define BEFORE_TRANSITION(timestamp, at)	((timestamp) < (at))
#else
#define BEFORE_TRANSITION(timestamp, at) \
	((at) >= 0 && (timestamp) < (time_t)(at))
#endif
#endif /* TZ_ZONEINFO */

char *
ctime(const time_t *timestamp)
{
	struct tm *local_time;

	local_time = localtime(timestamp);
	return local_time == 0 ? 0 : asctime(local_time);
}

/* The object gmtime() and localtime() return. */
static struct tm time_result;

struct tm *
localtime(const time_t *time_pointer)
{
	struct ttinfo *time_type;
	struct tm *result;
	int index;
	time_t timestamp;

	if (!__discobsd_zone_set)
		tzset();
	timestamp = *time_pointer;
#ifndef TZ_ZONEINFO
	/*
	** One type, so it is the answer: timecnt is zero by construction and
	** there is no transition table to consult.
	*/
	index = 0;
#else
	if (__discobsd_zone.timecnt == 0 ||
	    BEFORE_TRANSITION(timestamp, __discobsd_zone.ats[0])) {
		/*
		** The first standard-time type, for an instant before the
		** first transition. The walk is over ttis[], which holds
		** typecnt entries; timecnt counts the transitions in ats[]
		** and is the larger of the two for any real zone, so
		** bounding by it would read past the end of ttis[].
		*/
		index = 0;
		while (__discobsd_zone.ttis[index].tt_isdst)
			if (++index >= __discobsd_zone.typecnt) {
				index = 0;
				break;
			}
	} else {
		for (index = 1; index < __discobsd_zone.timecnt; ++index)
			if (BEFORE_TRANSITION(timestamp,
			    __discobsd_zone.ats[index]))
				break;
		index = __discobsd_zone.types[index - 1];
	}
#endif /* TZ_ZONEINFO */
	time_type = &__discobsd_zone.ttis[index];
	/*
	** To get (wrong) behavior that's compatible with System V Release 2.0
	** you'd replace the statement below with
	**	result = offtime((time_t) (timestamp + time_type->tt_gmtoff), 0L);
	*/
	result = __discobsd_offtime(&timestamp, time_type->tt_gmtoff,
	    &time_result);
#ifdef __TIME64_VIEW
	if (result == 0)
		return 0;
#endif
	result->tm_isdst = time_type->tt_isdst;
	tzname[result->tm_isdst] =
	    &__discobsd_zone.chars[time_type->tt_abbrind];
	result->tm_zone = &__discobsd_zone.chars[time_type->tt_abbrind];
	return result;
}

struct tm *
gmtime(const time_t *timestamp)
{
	struct tm *result;

	result = __discobsd_offtime(timestamp, 0L, &time_result);
#ifdef __TIME64_VIEW
	if (result == 0)
		return 0;
#endif
	tzname[0] = "GMT";
	result->tm_zone = "GMT";		/* UCT ? */
	return result;
}

#ifndef __TIME64_VIEW
/*
 * The count is the kernel's unsigned 32-bit clock (utime32_t), which spans
 * 136 years from 1970, so the conversion walks a year at a time. Every
 * count converts; an offset west of Greenwich can move the first hours
 * back into 1969. The count is read through its low 32 bits, which is
 * every bit on the target and keeps a host build whose long is eight
 * bytes (tests/libc_contracts) to the same range.
 */
struct tm *
__discobsd_offtime(const time_t *timestamp, long offset, struct tm *result)
{
	utime32_t count;
	long days;
	long remainder;
	int year;
	int leap_year;
	const unsigned char *month_length;

	count = *timestamp & 0xffffffffUL;
	days = (long)(count / SECS_PER_DAY);
	remainder = (long)(count % SECS_PER_DAY);
	remainder += offset;
	while (remainder < 0) {
		remainder += SECS_PER_DAY;
		--days;
	}
	while (remainder >= SECS_PER_DAY) {
		remainder -= SECS_PER_DAY;
		++days;
	}
	result->tm_hour = (int)(remainder / SECS_PER_HOUR);
	remainder %= SECS_PER_HOUR;
	result->tm_min = (int)(remainder / SECS_PER_MIN);
	result->tm_sec = (int)(remainder % SECS_PER_MIN);
	result->tm_wday = (int)((EPOCH_WDAY + days) % DAYS_PER_WEEK);
	if (result->tm_wday < 0)
		result->tm_wday += DAYS_PER_WEEK;
	year = EPOCH_YEAR;
	if (days >= 0)
		for ( ; ; ) {
			leap_year = isleap(year);
			if (days < DAYS_PER_NYEAR + leap_year)
				break;
			++year;
			days -= DAYS_PER_NYEAR + leap_year;
		}
	else do {
		--year;
		leap_year = isleap(year);
		days += DAYS_PER_NYEAR + leap_year;
	} while (days < 0);
	result->tm_year = year - TM_YEAR_BASE;
	result->tm_yday = (int)days;
	month_length = (const unsigned char *)(leap_year ?
	    "\037\035\037\036\037\036\037\037\036\037\036\037" :
	    "\037\034\037\036\037\036\037\037\036\037\036\037");
	for (result->tm_mon = 0;
	    days >= (long)month_length[result->tm_mon]; ++result->tm_mon)
		days -= (long)month_length[result->tm_mon];
	result->tm_mday = (int)(days + 1);
	result->tm_isdst = 0;
	result->tm_zone = "";
	result->tm_gmtoff = offset;
	return result;
}
#else /* __TIME64_VIEW */
/*
 * Divide *value by divisor, which is below 2^16, leave the quotient in
 * *value and return the remainder. The division runs over 16-bit limbs:
 * a remainder below the divisor shifted left by 16 stays inside 32 bits,
 * so each of the three steps is one 32-bit division.
 */
static uint32_t
divide_small(uint64_t *value, uint32_t divisor)
{
	uint32_t high = (uint32_t)(*value >> 32);
	uint32_t low = (uint32_t)*value;
	uint32_t quotient_high, quotient_mid, quotient_low, step;

	quotient_high = high / divisor;
	step = (high % divisor) << 16 | low >> 16;
	quotient_mid = step / divisor;
	step = (step % divisor) << 16 | (low & 0xffffU);
	quotient_low = step / divisor;
	*value = (uint64_t)quotient_high << 32 |
	    (uint32_t)(quotient_mid << 16 | quotient_low);
	return step % divisor;
}

/*
 * 1 March of year 0 fell on a Wednesday, CAL64_EPOCH_DAYS before the
 * Thursday that began 1970, and so did the first day of every era.
 */
#define ERA_WDAY	((EPOCH_WDAY + DAYS_PER_WEEK - \
			    CAL64_EPOCH_DAYS % DAYS_PER_WEEK) % DAYS_PER_WEEK)

/*
 * A 64-bit count spans more years than tm_year holds, so the conversion is
 * the closed form of offtime.h. The instant moved by offset is checked
 * against CAL64_MIN and CAL64_MAX before any field is written, and biased
 * by CAL64_BIAS_DAYS days, which leaves a non-negative count below 2^57.
 * 86400 seconds are 128 * 675, so the low seven bits of the count are the
 * low seven of the second of the day and one division by 675 gives the
 * day; the day divided by 189 and then by 773 gives the era and the day
 * within it.
 *
 * Within the era the year, month and day follow from 32-bit divisions.
 * The year is assembled modulo 2^32, since the era times 400 leaves an int
 * at the top of the range while the year itself, checked above, does not.
 */
struct tm *
__discobsd_offtime(const time_t *timestamp, long offset, struct tm *result)
{
	uint64_t count;
	uint32_t second, day_of_era, year_of_era, day_of_year, month, era;
	int leap_year;

	if (*timestamp < CAL64_MIN - offset ||
	    *timestamp > CAL64_MAX - offset) {
		errno = EOVERFLOW;
		return 0;
	}
	count = (uint64_t)(*timestamp + offset) + CAL64_BIAS_DAYS * 86400ULL;
	second = (uint32_t)count & 127U;
	count >>= 7;
	second |= divide_small(&count, SECS_PER_DAY / 128) << 7;
	day_of_era = divide_small(&count, CAL64_ERA_FACTOR_LOW);
	day_of_era += divide_small(&count, CAL64_ERA_FACTOR_HIGH) *
	    CAL64_ERA_FACTOR_LOW;
	era = (uint32_t)count;

	result->tm_hour = (int)(second / SECS_PER_HOUR);
	second %= SECS_PER_HOUR;
	result->tm_min = (int)(second / SECS_PER_MIN);
	result->tm_sec = (int)(second % SECS_PER_MIN);
	result->tm_wday = (int)((day_of_era + ERA_WDAY) % DAYS_PER_WEEK);

	/* The day count before each year of the era omits its leap days. */
	year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
	    day_of_era / 146096) / DAYS_PER_NYEAR;
	day_of_year = day_of_era - (DAYS_PER_NYEAR * year_of_era +
	    year_of_era / 4 - year_of_era / 100);
	/* March is month 0, and the days before a month are (153 m + 2) / 5. */
	month = (5 * day_of_year + 2) / 153;
	result->tm_mday = (int)(day_of_year - (153 * month + 2) / 5 + 1);
	if (month < 10) {
		/* March to December follow any 29 February of the year. */
		leap_year = (year_of_era & 3) == 0 &&
		    (year_of_era % 100 != 0 || year_of_era == 0);
		result->tm_mon = (int)month + TM_MARCH;
		result->tm_yday = (int)day_of_year + 59 + leap_year;
	} else {
		/* January and February belong to the next calendar year. */
		result->tm_mon = (int)month - 10;
		result->tm_yday = (int)day_of_year - 306;
		year_of_era++;
	}
	result->tm_year = (int)(year_of_era +
	    (era - (uint32_t)CAL64_BIAS_ERAS) * (uint32_t)CAL64_ERA_YEARS -
	    TM_YEAR_BASE);
	result->tm_isdst = 0;
	result->tm_zone = "";
	result->tm_gmtoff = offset;
	return result;
}
#endif /* __TIME64_VIEW */
