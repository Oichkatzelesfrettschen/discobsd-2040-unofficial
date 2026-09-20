/*
 * Copyright (c) 1987 Regents of the University of California.
 * This file may be freely redistributed provided that this
 * notice remains attached.
 */
#include "sys/param.h"
#include "sys/time.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "alloca.h"
#include "tzfile.h"
#include "paths.h"

char *
ctime(const time_t *timestamp)
{
	struct tm *local_time;

	local_time = localtime(timestamp);
	return local_time == 0 ? 0 : asctime(local_time);
}

char *tztab(int, int);
static struct tm *offtime(const time_t *, long);

struct ttinfo {				/* time type information */
	long		tt_gmtoff;	/* GMT offset in seconds */
	int		tt_isdst;	/* used to set tm_isdst */
	int		tt_abbrind;	/* abbreviation list index */
};

struct state {
	int		timecnt;
	int		typecnt;
	int		charcnt;
	time_t		ats[TZ_MAX_TIMES];
	unsigned char	types[TZ_MAX_TIMES];
	struct ttinfo	ttis[TZ_MAX_TYPES];
	char		chars[TZ_MAX_CHARS + 1];
};

static struct state	s;

static int		tz_is_set;

char *			tzname[2] = {
	"GMT",
	"GMT"
};

#ifdef USG_COMPAT
time_t			timezone = 0;
int			daylight = 0;
#endif /* USG_COMPAT */

static long
detzcode(const char *code_pointer)
{
	long result;
	int byte_index;

	result = 0;
	for (byte_index = 0; byte_index < 4; ++byte_index)
		result = (result << 8) | (code_pointer[byte_index] & 0xff);
	return result;
}

static int
tzload(const char *name)
{
	int index;
	int file_descriptor;

	if (name == 0 && (name = _PATH_LOCALTIME) == 0)
		return -1;
	{
		const char *zone_directory;
		int require_access_check;
		char *full_name;

		require_access_check = name[0] == '/';
		if (!require_access_check) {
			if ((zone_directory = _PATH_ZONEINFO) == 0)
				return -1;
			if ((strlen(zone_directory) + strlen(name) + 1) >= MAXPATHLEN)
				return -1;
			full_name = alloca(MAXPATHLEN);
			(void)strcpy(full_name, zone_directory);
			(void)strcat(full_name, "/");
			(void)strcat(full_name, name);
			/*
			** Set doaccess if '.' (as in "../") shows up in name.
			*/
			while (*name != '\0')
				if (*name++ == '.')
					require_access_check = 1;
			name = full_name;
		}
		if (require_access_check && access(name, 4) != 0)
			return -1;
		if ((file_descriptor = open(name, 0)) == -1)
			return -1;
	}
	{
		char *cursor;
		struct tzhead *header;
		char *buffer;
		ssize_t bytes_read;

		buffer = alloca(sizeof s);
		bytes_read = read(file_descriptor, buffer, sizeof s);
		if (close(file_descriptor) != 0 || bytes_read < 0 ||
		    (size_t)bytes_read < sizeof *header)
			return -1;
		header = (struct tzhead *)buffer;
		s.timecnt = (int)detzcode(header->tzh_timecnt);
		s.typecnt = (int)detzcode(header->tzh_typecnt);
		s.charcnt = (int)detzcode(header->tzh_charcnt);
		if (s.timecnt < 0 || s.timecnt > TZ_MAX_TIMES ||
			s.typecnt <= 0 ||
			s.typecnt > TZ_MAX_TYPES ||
			s.charcnt < 0 || s.charcnt > TZ_MAX_CHARS)
				return -1;
		if ((size_t)bytes_read < sizeof *header +
			s.timecnt * (4 + sizeof (char)) +
			s.typecnt * (4 + 2 * sizeof (char)) +
			s.charcnt * sizeof (char))
				return -1;
		cursor = buffer + sizeof *header;
		for (index = 0; index < s.timecnt; ++index) {
			s.ats[index] = detzcode(cursor);
			cursor += 4;
		}
		for (index = 0; index < s.timecnt; ++index)
			s.types[index] = (unsigned char)*cursor++;
		for (index = 0; index < s.typecnt; ++index) {
			struct ttinfo *time_type;

			time_type = &s.ttis[index];
			time_type->tt_gmtoff = detzcode(cursor);
			cursor += 4;
			time_type->tt_isdst = (unsigned char)*cursor++;
			time_type->tt_abbrind = (unsigned char)*cursor++;
		}
		for (index = 0; index < s.charcnt; ++index)
			s.chars[index] = *cursor++;
		s.chars[index] = '\0';	/* ensure '\0' at end */
	}
	/*
	** Check that all the local time type indices are valid.
	*/
	for (index = 0; index < s.timecnt; ++index)
		if (s.types[index] >= s.typecnt)
			return -1;
	/*
	** Check that all abbreviation indices are valid.
	*/
	for (index = 0; index < s.typecnt; ++index)
		if (s.ttis[index].tt_abbrind >= s.charcnt)
			return -1;
	/*
	** Set tzname elements to initial values.
	*/
	tzname[0] = tzname[1] = &s.chars[0];
#ifdef USG_COMPAT
	timezone = s.ttis[0].tt_gmtoff;
	daylight = 0;
#endif /* USG_COMPAT */
	for (index = 1; index < s.typecnt; ++index) {
		struct ttinfo *time_type;

		time_type = &s.ttis[index];
		if (time_type->tt_isdst) {
			tzname[1] = &s.chars[time_type->tt_abbrind];
#ifdef USG_COMPAT
			daylight = 1;
#endif /* USG_COMPAT */
		} else {
			tzname[0] = &s.chars[time_type->tt_abbrind];
#ifdef USG_COMPAT
			timezone = time_type->tt_gmtoff;
#endif /* USG_COMPAT */
		}
	}
	return 0;
}

static int
tzsetkernel(void)
{
	struct timeval	tv;
	struct timezone	tz;

	if (gettimeofday(&tv, &tz))
		return -1;
	s.timecnt = 0;		/* UNIX counts *west* of Greenwich */
	s.ttis[0].tt_gmtoff = tz.tz_minuteswest * -SECS_PER_MIN;
	s.ttis[0].tt_abbrind = 0;
	(void)strcpy(s.chars, tztab(tz.tz_minuteswest, 0));
	tzname[0] = tzname[1] = s.chars;
#ifdef USG_COMPAT
	timezone = tz.tz_minuteswest * 60;
	daylight = tz.tz_dsttime;
#endif /* USG_COMPAT */
	return 0;
}

static void
tzsetgmt(void)
{
	s.timecnt = 0;
	s.ttis[0].tt_gmtoff = 0;
	s.ttis[0].tt_abbrind = 0;
	(void) strcpy(s.chars, "GMT");
	tzname[0] = tzname[1] = s.chars;
#ifdef USG_COMPAT
	timezone = 0;
	daylight = 0;
#endif /* USG_COMPAT */
}

void
tzset(void)
{
	const char *name;

	tz_is_set = 1;
	name = getenv("TZ");
	if (!name || *name) {			/* did not request GMT */
		if (name && !tzload(name))	/* requested name worked */
			return;
		if (!tzload(0))			/* default name worked */
			return;
		if (!tzsetkernel())		/* kernel guess worked */
			return;
	}
	tzsetgmt();				/* GMT is default */
}

struct tm *
localtime(const time_t *time_pointer)
{
	struct ttinfo *time_type;
	struct tm *result;
	int index;
	time_t timestamp;

	if (!tz_is_set)
		tzset();
	timestamp = *time_pointer;
	if (s.timecnt == 0 || timestamp < s.ats[0]) {
		index = 0;
		while (s.ttis[index].tt_isdst)
			if (++index >= s.timecnt) {
				index = 0;
				break;
			}
	} else {
		for (index = 1; index < s.timecnt; ++index)
			if (timestamp < s.ats[index])
				break;
		index = s.types[index - 1];
	}
	time_type = &s.ttis[index];
	/*
	** To get (wrong) behavior that's compatible with System V Release 2.0
	** you'd replace the statement below with
	**	result = offtime((time_t) (timestamp + time_type->tt_gmtoff), 0L);
	*/
	result = offtime(&timestamp, time_type->tt_gmtoff);
	result->tm_isdst = time_type->tt_isdst;
	tzname[result->tm_isdst] = &s.chars[time_type->tt_abbrind];
	result->tm_zone = &s.chars[time_type->tt_abbrind];
	return result;
}

struct tm *
gmtime(const time_t *timestamp)
{
	struct tm *result;

	result = offtime(timestamp, 0L);
	tzname[0] = "GMT";
	result->tm_zone = "GMT";		/* UCT ? */
	return result;
}

static struct tm *
offtime(const time_t *timestamp, long offset)
{
	struct tm *result;
	long days;
	long remainder;
	int year;
	int leap_year;
	const unsigned char *month_length;
	static struct tm time_result;

	result = &time_result;
	days = *timestamp / SECS_PER_DAY;
	remainder = *timestamp % SECS_PER_DAY;
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
