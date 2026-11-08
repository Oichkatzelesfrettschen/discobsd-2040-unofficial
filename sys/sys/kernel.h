/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Global variables for the kernel
 */

/* 1.1 */
extern long hostid;
extern char hostname[MAXHOSTNAMELEN];
extern int hostnamelen;

/* 1.2 */
#include <sys/time.h>

extern struct timeval boottime;
extern struct timeval time;
extern struct timezone tz;
extern int adjdelta;
extern int hz;
extern int usechz;                 /* # microseconds per hz */
extern int lbolt;                  /* awoken once a second */

/*
 * Monotonic clock: hardclock() and monoclock_advance() count ticks into
 * monotick (0..hz-1) and carry into monosec.  settimeofday(2) and adjtime(2)
 * leave it alone.  realsec and realtick hold the earliest ITIMER_REAL
 * deadline, or an earlier time, and LONG_MAX seconds when none is armed.
 */
extern long monosec;
extern u_short monotick;
extern long realsec;
extern u_short realtick;
void monoclock_advance(unsigned int ticks);

extern short avenrun[3];
