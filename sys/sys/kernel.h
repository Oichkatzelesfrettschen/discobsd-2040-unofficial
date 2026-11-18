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
 * The monotonic clock counts ticks in [0, hz) and carries into seconds.
 * The expiry scan keeps the earliest ITIMER_REAL deadline or an earlier
 * bound. Keep the initialized seconds sentinel separate so the tick record
 * stays in BSS and the tick and expiry paths can share its base address.
 */
extern struct itimer_clock {
    long seconds;
    u_short ticks;
    u_short deadline_ticks;
} itimer_clock;
extern long itimer_deadline_seconds;
void monoclock_advance(unsigned int ticks);

extern short avenrun[3];
