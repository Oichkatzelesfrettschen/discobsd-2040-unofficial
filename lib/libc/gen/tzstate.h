/*
 * Copyright (c) 1987 Regents of the University of California.
 * This file may be freely redistributed provided that this
 * notice remains attached.
 */
/*
 * The zone in force, which tzset.c loads and ctime.c reads.
 *
 * ctime.c is compiled once for each width of time_t (sys/_time_t.h), and
 * both builds read the one state tzset() fills, so no member here follows
 * that width: a transition time is the four-byte count a zone file stores,
 * held in a long.
 */
#ifndef _LIBC_GEN_TZSTATE_H_
#define _LIBC_GEN_TZSTATE_H_

#include "tzfile.h"

struct ttinfo {				/* time type information */
	long		tt_gmtoff;	/* GMT offset in seconds */
	int		tt_isdst;	/* used to set tm_isdst */
	int		tt_abbrind;	/* abbreviation list index */
};

/*
** The zone in force. TZ_ZONEINFO selects the form that reads a zone file:
** ats[] and types[] hold that file's transitions and ttis[] the local time
** types it switches between, which is 2033 bytes of zero-initialized data
** per process and a second 2033 on the stack while tzload() parses.
**
** Without it the zone is one offset, which is what the kernel keeps and
** gettimeofday(2) reports, so a single type is the whole state: 68 bytes.
** An image that ships no zone file can reach no other answer -- this port's
** manifest ships neither /etc/localtime nor /usr/share/zoneinfo, and
** share/zoneinfo builds the files an installation would have to add. A
** system that adds them builds libc with TZ_ZONEINFO defined.
*/
struct state {
	int		timecnt;
	int		typecnt;
	int		charcnt;
#ifdef TZ_ZONEINFO
	long		ats[TZ_MAX_TIMES];
	unsigned char	types[TZ_MAX_TIMES];
	struct ttinfo	ttis[TZ_MAX_TYPES];
#else
	struct ttinfo	ttis[1];
#endif
	char		chars[TZ_MAX_CHARS + 1];
};

/*
 * Defined in tzset.c: the state, a flag that is nonzero once tzset() has
 * filled it, and the standard and daylight abbreviations localtime()
 * reports.
 */
extern struct state	__discobsd_zone;
extern int		__discobsd_zone_set;
extern char		*tzname[2];

#endif /* !_LIBC_GEN_TZSTATE_H_ */
