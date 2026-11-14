/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/timeb.h>

/*
 * Backwards compatible ftime.
 */

/*
 * struct timeb is declared by <sys/timeb.h>. ftime64.o
 * (lib/libc/compat/Makefile) is this file built with _TIME_BITS defined
 * as 64, where ftime() is __ftime64 and gettimeofday() the
 * __gettimeofday64 wrapper.
 */
int
ftime(tp)
	register struct timeb *tp;
{
	struct timeval t;
	struct timezone tz;

	if (gettimeofday(&t, &tz) < 0)
		return (-1);
	tp->time = t.tv_sec;
	tp->millitm = t.tv_usec / 1000;
	tp->timezone = tz.tz_minuteswest;
	tp->dstflag = tz.tz_dsttime;
	return 0;
}
