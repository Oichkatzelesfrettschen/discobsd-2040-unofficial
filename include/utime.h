/*
 * utime(3): set a file's access and modification times, each a count of
 * seconds since 1970, through utimes(2) (lib/libc/compat/utime.c).
 *
 * The structure holds two time_t, so it follows the program's view of
 * time_t (sys/_time_t.h). In a unit built with the 64-bit view utime()
 * binds to __utime64, which passes the counts to the __utimes64 wrapper:
 * a count outside 0..4294967294 fails with EINVAL before the kernel is
 * entered. The kernel's file times are unsigned 32-bit counts, and it
 * reads 4294967295 as "leave this time alone" (lib/libc/gen/time64.h).
 */
#ifndef _UTIME_H_
#define _UTIME_H_

#include <sys/_time_t.h>

struct utimbuf {
	time_t	actime;		/* access time */
	time_t	modtime;	/* modification time */
};

int utime(const char *, const struct utimbuf *) __TIME64_NAME(utime);

#endif /* !_UTIME_H_ */
