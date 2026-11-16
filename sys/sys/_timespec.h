/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifndef _SYS__TIMESPEC_H_
#define _SYS__TIMESPEC_H_

#include <sys/_time_t.h>

/*
 * POSIX.4 time value with nanoseconds instead of microseconds.
*/
struct timespec {
    time_t tv_sec;          /* seconds */
    long   tv_nsec;         /* and nanoseconds */
};

#endif /* !_SYS__TIMESPEC_H_ */
