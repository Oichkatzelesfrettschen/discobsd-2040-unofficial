/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef _SYS_TIME_H_
#define _SYS_TIME_H_

#include <sys/types.h>

/*
 * The layouts the kernel reads and writes, in every view of time: the
 * wrappers of the 64-bit view convert to and from them
 * (lib/libc/gen/time64.h), and a program that reads a kernel structure
 * holding one (the KERN_BOOTTIME payload of sysctl) names it.
 */
struct __timeval32 {
    long    tv_sec;
    long    tv_usec;
};

struct __timespec32 {
    utime32_t tv_sec;
    long    tv_nsec;
};

/*
 * Structure returned by gettimeofday(2) system call,
 * and used in other calls.
 */
#ifdef __TIME64_VIEW
struct timeval {
    time_t  tv_sec;         /* seconds */
    long    tv_usec;        /* and microseconds */
};
#else
struct timeval {
    long    tv_sec;         /* seconds */
    long    tv_usec;        /* and microseconds */
};
#endif

/*
 * POSIX.4 time value with nanoseconds instead of microseconds.
*/
struct timespec {
    time_t tv_sec;          /* seconds */
    long   tv_nsec;         /* and nanoseconds */
};

struct timezone {
    int     tz_minuteswest; /* minutes west of Greenwich */
    int     tz_dsttime;     /* type of dst correction */
};
#define DST_NONE    0       /* not on dst */
#define DST_USA     1       /* USA style dst */
#define DST_AUST    2       /* Australian style dst */
#define DST_WET     3       /* Western European dst */
#define DST_MET     4       /* Middle European dst */
#define DST_EET     5       /* Eastern European dst */
#define DST_CAN     6       /* Canada */

/*
 * Operations on timevals.
 *
 * NB: timercmp does not work for >= or <=.
 */
#define timerisset(tvp)     ((tvp)->tv_sec || (tvp)->tv_usec)
#define timercmp(tvp, uvp, cmp) \
                            ((tvp)->tv_sec cmp (uvp)->tv_sec || \
                             (tvp)->tv_sec == (uvp)->tv_sec && \
                             (tvp)->tv_usec cmp (uvp)->tv_usec)
#define timerclear(tvp)     (tvp)->tv_sec = (tvp)->tv_usec = 0

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define ITIMER_REAL     0
#define ITIMER_VIRTUAL  1
#define ITIMER_PROF     2

struct  k_itimerval {
    long    it_interval;            /* timer interval */
    long    it_value;               /* current value */
};

struct  itimerval {
    struct  timeval it_interval;    /* timer interval */
    struct  timeval it_value;       /* current value */
};

#ifdef KERNEL
/*
 * The wall clock, time.tv_sec, counts seconds since 1970 modulo 2^32 in
 * the signed field struct timeval shares with intervals; read unsigned
 * (utime32_t) it is valid to 2106. Its arithmetic runs in unsigned long,
 * where the carry out of bit 31 in January 2038 is defined: wall_advance()
 * moves a clock value by a number of seconds modulo 2^32, and
 * wall_interval() returns the signed distance from one clock value to
 * another, held at LONG_MAX or -LONG_MAX when the two are further apart
 * than a long holds (a clock stepped across more than 68 years).
 */
static inline long
wall_advance(long seconds, long by)
{
    return (long)((unsigned long)seconds + (unsigned long)by);
}

static inline long
wall_interval(long later, long earlier)
{
    unsigned long to = (unsigned long)later, from = (unsigned long)earlier;
    unsigned long limit = (unsigned long)-1 >> 1;

    if (to >= from)
        return to - from > limit ? (long)limit : (long)(to - from);
    return from - to > limit ? -(long)limit : -(long)(from - to);
}

/*
 * Nonzero when a wall clock reading of seconds and microseconds is at or
 * after the deadline. Both second counts are compared unsigned, so the
 * order holds across the carry and for any two values of the count.
 */
static inline int
wall_reached(const struct timeval *deadline, long seconds, long microseconds)
{
    unsigned long now = (unsigned long)seconds;
    unsigned long due = (unsigned long)deadline->tv_sec;

    return now > due || (now == due && microseconds >= deadline->tv_usec);
}

/*
 * Nonzero on each wall clock second that is a multiple of period. The
 * count is taken unsigned, since 2^32 is a multiple of few periods and a
 * signed remainder would shift the phase at the carry.
 */
static inline int
wall_every(long seconds, unsigned long period)
{
    return (unsigned long)seconds % period == 0;
}

/*
 * Round up a proposed time value to a minimal resolution of the clock.
 */
int itimerfix (struct timeval *tv);

/*
 * Add and subtract routines for timevals.
 */
void timevaladd (struct timeval *t1, struct timeval *t2);
void timevalsub (struct timeval *t1, struct timeval *t2);

/*
 * Compute number of hz until specified time.
 */
int hzto (struct timeval *tv);

#else
#include <time.h>

int gettimeofday (struct timeval *tv, struct timezone *tz)
           __TIME64_NAME(gettimeofday);
int settimeofday (const struct timeval *tv, const struct timezone *tz)
           __TIME64_NAME(settimeofday);
int utimes (const char *filename, const struct timeval times[2])
           __TIME64_NAME(utimes);
int futimes (int fd, const struct timeval times[2])
           __TIME64_NAME(futimes);
int getitimer (int which, struct itimerval *curr_value)
           __TIME64_NAME(getitimer);
int setitimer (int which, const struct itimerval *new_value,
           struct itimerval *old_value) __TIME64_NAME(setitimer);
#ifdef __TIME64_VIEW
int adjtime (const struct timeval *delta, struct timeval *olddelta)
           __TIME64_NAME(adjtime);
#endif
char *tztab (int zone, int dst);

#endif

/*
 * KERN_CLOCKRATE clock information structure
 */
struct clockinfo {
    int     tick;       /* micro-seconds per hz tick */
    int     hz;         /* clock frequency */
    int     stathz;     /* statistics clock frequency */
    int     profhz;     /* profiling clock frequency */
};

extern unsigned int msec(void);
#endif  /* !_SYS_TIME_H_ */
