/*
 * Copyright (c) 1983, 1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifndef _TIME_H
#define _TIME_H

#ifndef NULL
#define NULL    ((void *)0)
#endif

#include <sys/_time_t.h>

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned size_t;
#endif

/*
 * Structure returned by gmtime and localtime calls (see ctime(3)).
 */
struct tm {
    int     tm_sec;
    int     tm_min;
    int     tm_hour;
    int     tm_mday;
    int     tm_mon;
    int     tm_year;
    int     tm_wday;
    int     tm_yday;
    int     tm_isdst;
    long    tm_gmtoff;
    char    *tm_zone;
};

/*
 * Each function that passes a time_t binds, in a unit built with the
 * 64-bit view (sys/_time_t.h), to a second symbol, __NAME64, which libc
 * builds from the same source with that view (lib/libc/gen/Makefile).
 * Its gmtime() and localtime() cover every year an int tm_year holds and
 * answer a null pointer with errno EOVERFLOW outside it; mktime() and
 * timegm() answer (time_t)-1 there (lib/libc/gen/offtime.h).
 */
struct tm *gmtime(const time_t *) __TIME64_NAME(gmtime);
struct tm *localtime(const time_t *) __TIME64_NAME(localtime);
time_t mktime(struct tm *) __TIME64_NAME(mktime);
/*
 * timegm reads the fields as UTC. It is the part of the inverse that has a
 * closed form, and mktime searches with it; lib/libc/gen/mktime.c says how.
 */
time_t timegm(struct tm *) __TIME64_NAME(timegm);
char *ctime(const time_t *) __TIME64_NAME(ctime);
double difftime(time_t, time_t) __TIME64_NAME(difftime);
time_t time(time_t *) __TIME64_NAME(time);
char *asctime(const struct tm *);
char *timezone(int, int);
void tzset(void);

size_t strftime (char *s, size_t maxsize, const char *format,
    const struct tm *timeptr);

#endif
