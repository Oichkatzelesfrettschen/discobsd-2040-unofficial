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

/*
 * The kernel reads tv_sec as a 32-bit count in which 2^31 and above is a
 * negative value and is refused; setitimer() itself clamps a value the
 * timer cannot hold. A request above 2,147,483,647 s is clamped to that
 * count, so that alarm() fails for no size of request.
 */
#define	ALARM_MAX	2147483647UL

unsigned int
alarm(unsigned int secs)
{
	struct itimerval it, oitv;
	register struct itimerval *itp = &it;

	timerclear(&itp->it_interval);
	itp->it_value.tv_sec = secs > ALARM_MAX ? ALARM_MAX : secs;
	itp->it_value.tv_usec = 0;
	if (setitimer(ITIMER_REAL, itp, &oitv) < 0)
		return ((unsigned int)-1);
	if (oitv.it_value.tv_usec != 0)
		oitv.it_value.tv_sec++;
	return ((unsigned int)oitv.it_value.tv_sec);
}
