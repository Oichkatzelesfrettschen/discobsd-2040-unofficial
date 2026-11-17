/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifndef _SYS__TIMEVAL_H_
#define _SYS__TIMEVAL_H_

#include <sys/_time_t.h>
#include <sys/_suseconds_t.h>

/*
 * Structure returned by gettimeofday(2) system call,
 * and used in other calls.
 */
#ifdef __TIME64_VIEW
struct timeval {
    time_t  tv_sec;         /* seconds */
    suseconds_t tv_usec;    /* and microseconds */
};
#else
struct timeval {
    long    tv_sec;         /* seconds */
    suseconds_t tv_usec;    /* and microseconds */
};
#endif

#endif /* !_SYS__TIMEVAL_H_ */
