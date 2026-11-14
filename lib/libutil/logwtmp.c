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
#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <utmp.h>
#include <paths.h>

void
logwtmp(line, name, host)
	char *line, *name, *host;
{
	struct utmp ut;
	struct stat buf;
	int fd;
	char *strncpy();

	if ((fd = open(_PATH_WTMP, O_WRONLY|O_APPEND, 0)) < 0)
		return;
	if (!fstat(fd, &buf)) {
		(void)strncpy(ut.ut_line, line, sizeof(ut.ut_line));
		(void)strncpy(ut.ut_name, name, sizeof(ut.ut_name));
		(void)strncpy(ut.ut_host, host, sizeof(ut.ut_host));
		/* The record holds the kernel's 32-bit count (<utmp.h>). */
		ut.ut_time = (utime32_t)time((time_t *)0);
		if (write(fd, (char *)&ut, sizeof(struct utmp)) !=
		    sizeof(struct utmp))
			(void)ftruncate(fd, buf.st_size);
	}
	(void)close(fd);
}
