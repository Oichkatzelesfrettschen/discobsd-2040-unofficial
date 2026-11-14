/*
 * ftime(3): the clock as seconds since 1970 and milliseconds, with the
 * kernel's zone offset and daylight flag (lib/libc/compat/ftime.c).
 *
 * time follows the program's view of time_t (sys/_time_t.h); in a unit
 * built with the 64-bit view ftime() binds to __ftime64, which reads the
 * clock through the __gettimeofday64 wrapper.
 */
#ifndef _SYS_TIMEB_H_
#define _SYS_TIMEB_H_

#include <sys/types.h>

struct timeb {
	time_t	time;		/* seconds since 1970 */
	u_short	millitm;	/* milliseconds */
	short	timezone;	/* minutes west of Greenwich */
	short	dstflag;	/* nonzero during daylight saving time */
};

int ftime(struct timeb *) __TIME64_NAME(ftime);

#endif /* !_SYS_TIMEB_H_ */
