/*
 * The two NetBSD <dev/clock_subr.h> conversions msdosfs_conv.c calls,
 * between seconds since 1970-01-01 UTC and a proleptic Gregorian date.
 * NetBSD's sys/dev/clock_subr.c holds them for the kernel; the tool needs
 * only these two, so they live here as inline functions. Dates run from
 * 1970, the earliest a DOS timestamp (1980) or the conversions reach.
 */
#ifndef MSDOSFS_COMPAT_CLOCK_SUBR_H
#define MSDOSFS_COMPAT_CLOCK_SUBR_H

#include <stdint.h>
#include <time.h>

struct clock_ymdhms {
	uint64_t dt_year;
	uint8_t dt_mon;		/* 1 to 12 */
	uint8_t dt_day;		/* 1 to 31 */
	uint8_t dt_wday;	/* 0 (Sunday) to 6 */
	uint8_t dt_hour;
	uint8_t dt_min;
	uint8_t dt_sec;
};

/* Days from 1970-01-01 to YEAR-MON-DAY (H. Hinnant's days_from_civil). */
static inline int64_t
clock_days_from_civil(int64_t year, unsigned mon, unsigned day)
{
	int64_t era;
	unsigned yoe, doy, doe;

	year -= mon <= 2;
	era = (year >= 0 ? year : year - 399) / 400;
	yoe = (unsigned)(year - era * 400);
	doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

static inline time_t
clock_ymdhms_to_secs(const struct clock_ymdhms *dt)
{
	int64_t days;

	days = clock_days_from_civil((int64_t)dt->dt_year, dt->dt_mon,
	    dt->dt_day);
	return (time_t)(days * 86400 + dt->dt_hour * 3600 + dt->dt_min * 60 +
	    dt->dt_sec);
}

static inline void
clock_secs_to_ymdhms(time_t secs, struct clock_ymdhms *dt)
{
	int64_t days = (int64_t)secs / 86400, era, year;
	unsigned rem = (unsigned)((int64_t)secs % 86400);
	unsigned doe, yoe, doy, mp;

	dt->dt_wday = (uint8_t)((days + 4) % 7);
	dt->dt_hour = (uint8_t)(rem / 3600);
	dt->dt_min = (uint8_t)(rem / 60 % 60);
	dt->dt_sec = (uint8_t)(rem % 60);
	days += 719468;
	era = days / 146097;
	doe = (unsigned)(days - era * 146097);
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	year = (int64_t)yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	dt->dt_day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
	dt->dt_mon = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
	dt->dt_year = (uint64_t)(year + (dt->dt_mon <= 2));
}

#endif /* !MSDOSFS_COMPAT_CLOCK_SUBR_H */
