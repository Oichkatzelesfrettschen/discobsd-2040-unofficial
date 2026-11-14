/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *  @(#)times.h 7.1 (Berkeley) 6/4/86
 */

/*
 * Structure returned by times()
 */
struct tms {
    time_t  tms_utime;      /* user time */
    time_t  tms_stime;      /* system time */
    time_t  tms_cutime;     /* user time, children */
    time_t  tms_cstime;     /* system time, children */
};

/*
 * times() fills four 32-bit fields, so the 64-bit view of time
 * (sys/_time_t.h), under which this structure is four 64-bit fields,
 * leaves it undeclared and a call fails to compile.
 */
#ifndef __TIME64_VIEW
int times(struct tms *tmsp);
#endif
