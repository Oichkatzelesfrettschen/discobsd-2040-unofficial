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
/*
 * Each field counts sixtieths of a second of CPU time
 * (lib/libc/compat/times.c), not seconds since 1970, so it is a long under
 * every view of time_t (sys/_time_t.h) and the structure keeps one layout
 * for programs built with either width.
 */
struct tms {
    long    tms_utime;      /* user time */
    long    tms_stime;      /* system time */
    long    tms_cutime;     /* user time, children */
    long    tms_cstime;     /* system time, children */
};

int times(struct tms *tmsp);
