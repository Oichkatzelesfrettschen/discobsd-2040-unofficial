/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *  @(#)lastlog.h   5.1 (Berkeley) 5/30/85
 */

#ifndef _LASTLOG_H_
#define _LASTLOG_H_

#include <sys/_time_t.h>

/*
 * One record of /var/log/lastlog, indexed by user id. ll_time is the
 * kernel's 32-bit clock count read unsigned (utime32_t), as ut_time is in
 * <utmp.h>, so the record keeps its 28 bytes whatever width of time_t the
 * program compiles with.
 */
struct lastlog {
    utime32_t ll_time;
    char    ll_line[8];
    char    ll_host[16];        /* same as in utmp */
};

/*
 * The size on the target, where long and utime32_t are four bytes; a host
 * build where long is wider (a contract test) lays the record out its own
 * way and reads no board file.
 */
#if defined(__SIZEOF_LONG__) && __SIZEOF_LONG__ == 4 && !defined(__SMALLER_C__)
_Static_assert(sizeof(struct lastlog) == 28,
    "a lastlog record is 28 bytes on disk");
#endif

#endif /* !_LASTLOG_H_ */
