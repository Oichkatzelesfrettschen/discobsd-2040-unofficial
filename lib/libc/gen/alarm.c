/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Backwards compatible alarm.
 */
#include <sys/time.h>
#include <unistd.h>

unsigned int
alarm(unsigned int secs)
{
	struct itimerval it, oitv;
	register struct itimerval *itp = &it;

	timerclear(&itp->it_interval);
	itp->it_value.tv_sec = secs;
	itp->it_value.tv_usec = 0;
	if (setitimer(ITIMER_REAL, itp, &oitv) < 0)
		return ((unsigned int)-1);
	if (oitv.it_value.tv_usec != 0)
		oitv.it_value.tv_sec++;
	return ((unsigned int)oitv.it_value.tv_sec);
}
