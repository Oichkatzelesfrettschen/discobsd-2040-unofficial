/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/time.h>
#include <sys/types.h>
#include <utime.h>

/*
 * Backwards compatible utime.
 */
/*
 * struct utimbuf is the two counts a time_t[2] holds, in the same order.
 * utime64.o (lib/libc/compat/Makefile) is this file built with _TIME_BITS
 * defined as 64: utime() is then __utime64 and utimes() the __utimes64
 * wrapper, which refuses a count outside 0..4294967294 with EINVAL.
 */
int
utime(const char *name, const struct utimbuf *otv)
{
	struct timeval tv[2];

#ifdef __TIME64_VIEW
	/*
	 * A null pair goes to utimes() as null, which sets both times to the
	 * present. The 32-bit build reads through the pointer as it always
	 * has.
	 */
	if (otv == 0)
		return utimes(name, 0);
#endif
	tv[0].tv_sec = otv->actime; tv[0].tv_usec = 0;
	tv[1].tv_sec = otv->modtime; tv[1].tv_usec = 0;
	return utimes(name, tv);
}
