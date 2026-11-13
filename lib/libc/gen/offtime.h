/*
 * The calendar conversion ctime.c and mktime.c share.
 *
 * __discobsd_offtime() reads the instant at *timestamp, moved offset seconds
 * east, into every field of *result, including tm_gmtoff and an empty
 * tm_zone, and returns result. gmtime() and localtime() call it on the object they return;
 * timegm() calls it on the caller's own struct, which leaves that object
 * as the last gmtime() or localtime() call wrote it.
 */
#ifndef _LIBC_GEN_OFFTIME_H_
#define _LIBC_GEN_OFFTIME_H_

#include <time.h>

struct tm *__discobsd_offtime(const time_t *timestamp, long offset,
	    struct tm *result);

#endif /* !_LIBC_GEN_OFFTIME_H_ */
