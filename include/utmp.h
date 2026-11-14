/*
 * Copyright (c) 1988 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _UTMP_H_
#define _UTMP_H_

#include <sys/_time_t.h>

#define UT_NAMESIZE 15
#define UT_LINESIZE 8
#define UT_HOSTSIZE 16

/*
 * One record of /var/run/utmp and /var/log/wtmp. The file holds the
 * login time as the kernel's clock counts it, 32 bits read unsigned
 * (utime32_t, valid to 2106), whatever width of time_t the program that
 * reads or writes the record compiles with; a program widens it to a
 * time_t before it converts or compares it.
 */
struct utmp {
    char    ut_line[UT_LINESIZE];
    char    ut_name[UT_NAMESIZE];
    char    ut_host[UT_HOSTSIZE];
    utime32_t ut_time;
};

/*
 * The size on the target, where long and utime32_t are four bytes; a host
 * build where long is wider (a contract test) lays the record out its own
 * way and reads no board file.
 */
#if defined(__SIZEOF_LONG__) && __SIZEOF_LONG__ == 4
_Static_assert(sizeof(struct utmp) == 44, "a utmp record is 44 bytes on disk");
#endif

#endif /* !_UTMP_H_ */
