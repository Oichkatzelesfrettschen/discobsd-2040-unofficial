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
 * The functions that pass a time_t exist in libc at the 32-bit width only,
 * so the 64-bit view (sys/_time_t.h) leaves them undeclared: a unit built
 * with that view fails to compile a call instead of binding a 64-bit
 * argument to a 32-bit function.
 */
#ifndef __TIME64_VIEW
struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);
time_t mktime(struct tm *);
/*
 * timegm reads the fields as UTC. It is the part of the inverse that has a
 * closed form, and mktime searches with it; lib/libc/gen/mktime.c says how.
 */
time_t timegm(struct tm *);
char *ctime(const time_t *);
double difftime(time_t, time_t);
time_t time(time_t *);
#endif
char *asctime(const struct tm *);
char *timezone(int, int);
void tzset(void);

size_t strftime (char *s, size_t maxsize, const char *format,
    const struct tm *timeptr);

#endif
