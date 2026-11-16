/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Backwards compatible time call.
 */
#include <sys/types.h>
#include <sys/time.h>
/*
 * <time.h> declares time(), which in a build with _TIME_BITS defined as 64
 * (time64.o, lib/libc/gen/Makefile) binds the definition below to __time64
 * and its gettimeofday() call to the __gettimeofday64 wrapper.
 */
#include <time.h>

time_t
time(t)
	time_t *t;
{
	struct timeval tt;

	if (gettimeofday(&tt, (struct timezone *)0) < 0)
		return (-1);
	if (t)
		*t = tt.tv_sec;
	return (tt.tv_sec);
}
