/*
 * The environment and clock tzset.c reads, compiled with the default view
 * of time like tzset.c itself. TZ set and empty selects GMT; TZ unset
 * selects the kernel's offset, here zone_minuteswest. DEFINE_ERRNO gives
 * the tree's errno a definition where no other unit of the program does.
 */
#include <sys/types.h>
#include <sys/time.h>

#include "zone_hooks.h"

const char *zone_tz;
int zone_minuteswest;

#ifdef DEFINE_ERRNO
int errno;
#endif

char *calendar_getenv(const char *);
int calendar_zone_gettimeofday(struct timeval *, struct timezone *);

char *
calendar_getenv(const char *name)
{
	(void)name;
	return (char *)zone_tz;
}

int
calendar_zone_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	tv->tv_sec = 0;
	tv->tv_usec = 0;
	tz->tz_minuteswest = zone_minuteswest;
	tz->tz_dsttime = 0;
	return 0;
}
