/*
 * Copyright (c) 1987 Regents of the University of California.
 * This file may be freely redistributed provided that this
 * notice remains attached.
 */
/*
 * The zone tzset() selects, shared by both builds of ctime.c: the state
 * tzstate.h describes, the names tzname[] reports, and the readers of a
 * zone file (TZ_ZONEINFO) and of the kernel's offset.
 */
#include "sys/param.h"
#include "sys/time.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "alloca.h"
#include "paths.h"
#include "tzstate.h"

char *tztab(int, int);

struct state		__discobsd_zone;

int			__discobsd_zone_set;

char *			tzname[2] = {
	"GMT",
	"GMT"
};

#ifdef USG_COMPAT
time_t			timezone = 0;
int			daylight = 0;
#endif /* USG_COMPAT */

#ifdef TZ_ZONEINFO
static long
detzcode(const char *code_pointer)
{
	unsigned long result;
	int byte_index;

	result = 0;
	for (byte_index = 0; byte_index < 4; ++byte_index)
		result = (result << 8) | (code_pointer[byte_index] & 0xff);
	/*
	** A zone file stores each value as four bytes of two's complement,
	** so the sign lives in bit 31 and has to be carried into the rest of
	** a long wider than four bytes. Where long is four bytes the
	** correction below is the identity; where it is eight, its absence
	** delivers every offset west of Greenwich and every transition
	** before the epoch as a value near 2^32.
	*/
	if (result & 0x80000000UL)
		return ((long)(result - 0x80000000UL) - 0x7fffffffL - 1L);
	return ((long)result);
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
			if ((strlen(zone_directory) + strlen(name) + 1) >=
			    MAXPATHLEN)
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

		buffer = alloca(sizeof __discobsd_zone);
		bytes_read = read(file_descriptor, buffer,
		    sizeof __discobsd_zone);
		if (close(file_descriptor) != 0 || bytes_read < 0 ||
		    (size_t)bytes_read < sizeof *header)
			return -1;
		header = (struct tzhead *)buffer;
		__discobsd_zone.timecnt = (int)detzcode(header->tzh_timecnt);
		__discobsd_zone.typecnt = (int)detzcode(header->tzh_typecnt);
		__discobsd_zone.charcnt = (int)detzcode(header->tzh_charcnt);
		if (__discobsd_zone.timecnt < 0 ||
			__discobsd_zone.timecnt > TZ_MAX_TIMES ||
			__discobsd_zone.typecnt <= 0 ||
			__discobsd_zone.typecnt > TZ_MAX_TYPES ||
			__discobsd_zone.charcnt < 0 ||
			__discobsd_zone.charcnt > TZ_MAX_CHARS)
				return -1;
		if ((size_t)bytes_read < sizeof *header +
			__discobsd_zone.timecnt * (4 + sizeof (char)) +
			__discobsd_zone.typecnt * (4 + 2 * sizeof (char)) +
			__discobsd_zone.charcnt * sizeof (char))
				return -1;
		cursor = buffer + sizeof *header;
		for (index = 0; index < __discobsd_zone.timecnt; ++index) {
			__discobsd_zone.ats[index] = detzcode(cursor);
			cursor += 4;
		}
		for (index = 0; index < __discobsd_zone.timecnt; ++index)
			__discobsd_zone.types[index] = (unsigned char)*cursor++;
		for (index = 0; index < __discobsd_zone.typecnt; ++index) {
			struct ttinfo *time_type;

			time_type = &__discobsd_zone.ttis[index];
			time_type->tt_gmtoff = detzcode(cursor);
			cursor += 4;
			time_type->tt_isdst = (unsigned char)*cursor++;
			time_type->tt_abbrind = (unsigned char)*cursor++;
		}
		for (index = 0; index < __discobsd_zone.charcnt; ++index)
			__discobsd_zone.chars[index] = *cursor++;
		__discobsd_zone.chars[index] = '\0';	/* ensure '\0' at end */
	}
	/*
	** Check that all the local time type indices are valid.
	*/
	for (index = 0; index < __discobsd_zone.timecnt; ++index)
		if (__discobsd_zone.types[index] >= __discobsd_zone.typecnt)
			return -1;
	/*
	** Check that all abbreviation indices are valid, and that each
	** daylight flag is one of the two values it is read as: localtime()
	** indexes tzname[2] with it, so a file naming any other value would
	** have a pointer written past the end of that array.
	*/
	for (index = 0; index < __discobsd_zone.typecnt; ++index)
		if (__discobsd_zone.ttis[index].tt_abbrind >=
			__discobsd_zone.charcnt ||
			(__discobsd_zone.ttis[index].tt_isdst != 0 &&
			__discobsd_zone.ttis[index].tt_isdst != 1))
				return -1;
	/*
	** Set tzname elements to initial values.
	*/
	tzname[0] = tzname[1] = &__discobsd_zone.chars[0];
#ifdef USG_COMPAT
	timezone = __discobsd_zone.ttis[0].tt_gmtoff;
	daylight = 0;
#endif /* USG_COMPAT */
	for (index = 1; index < __discobsd_zone.typecnt; ++index) {
		struct ttinfo *time_type;

		time_type = &__discobsd_zone.ttis[index];
		if (time_type->tt_isdst) {
			tzname[1] =
			    &__discobsd_zone.chars[time_type->tt_abbrind];
#ifdef USG_COMPAT
			daylight = 1;
#endif /* USG_COMPAT */
		} else {
			tzname[0] =
			    &__discobsd_zone.chars[time_type->tt_abbrind];
#ifdef USG_COMPAT
			timezone = time_type->tt_gmtoff;
#endif /* USG_COMPAT */
		}
	}
	return 0;
}

#endif /* TZ_ZONEINFO */

static int
tzsetkernel(void)
{
	struct timeval	tv;
	struct timezone	tz;

	if (gettimeofday(&tv, &tz))
		return -1;
	/* UNIX counts *west* of Greenwich */
	__discobsd_zone.timecnt = 0;
	/* the one type installed just below */
	__discobsd_zone.typecnt = 1;
	__discobsd_zone.ttis[0].tt_gmtoff = tz.tz_minuteswest * -SECS_PER_MIN;
	__discobsd_zone.ttis[0].tt_abbrind = 0;
	(void)strcpy(__discobsd_zone.chars, tztab(tz.tz_minuteswest, 0));
	tzname[0] = tzname[1] = __discobsd_zone.chars;
#ifdef USG_COMPAT
	timezone = tz.tz_minuteswest * 60;
	daylight = tz.tz_dsttime;
#endif /* USG_COMPAT */
	return 0;
}

static void
tzsetgmt(void)
{
	__discobsd_zone.timecnt = 0;
	/* the one type installed just below */
	__discobsd_zone.typecnt = 1;
	__discobsd_zone.ttis[0].tt_gmtoff = 0;
	__discobsd_zone.ttis[0].tt_abbrind = 0;
	(void) strcpy(__discobsd_zone.chars, "GMT");
	tzname[0] = tzname[1] = __discobsd_zone.chars;
#ifdef USG_COMPAT
	timezone = 0;
	daylight = 0;
#endif /* USG_COMPAT */
}

void
tzset(void)
{
	const char *name;

	__discobsd_zone_set = 1;
	name = getenv("TZ");
	if (!name || *name) {			/* did not request GMT */
#ifdef TZ_ZONEINFO
		if (name && !tzload(name))	/* requested name worked */
			return;
		if (!tzload(0))			/* default name worked */
			return;
#endif
		if (!tzsetkernel())		/* kernel guess worked */
			return;
	}
	tzsetgmt();				/* GMT is default */
}
