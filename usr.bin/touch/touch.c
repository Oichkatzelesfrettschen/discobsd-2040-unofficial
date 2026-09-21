/*	$OpenBSD: touch.c,v 1.28 2026/06/21 19:24:43 tb Exp $	*/
/*	$NetBSD: touch.c,v 1.11 1995/08/31 22:10:06 jtc Exp $	*/

/*
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This kernel carries utimes(2) and records a file's times as whole seconds
 * (struct icommon2's ic_atime and ic_mtime), so the times here are a struct
 * timeval pair whose microseconds are always zero. The interface OpenBSD
 * reaches the same inodes through, utimensat(2) and futimens(2), names two
 * things utimes(2) cannot:
 *
 *   - UTIME_NOW is utimes(path, NULL); a run with no time specification
 *     reads the clock instead, so that every operand of one run is given
 *     the same instant rather than the instant it was reached.
 *   - UTIME_OMIT has no spelling at all, so -a alone and -m alone read the
 *     file's current times with stat(2) and write back unaltered the one
 *     they are not changing.
 *   - futimens(fd, ...) on a file just created becomes a second utimes(2)
 *     on its name, there being no futimes(2) here.
 *
 * -d takes the ISO 8601 form OpenBSD accepts and is read by the two
 * fixed-width field parsers below rather than through strptime(3), which
 * this libc does not carry. A fraction of a second is accepted and
 * discarded, the filesystem having nowhere to record it.
 *
 * The times reach the inode through utimes(2) alone. The implementation
 * this replaces set them by reading a file's first byte and writing it
 * back, which could not touch anything but a regular file, could not set
 * the two times apart, and rewrote a block of a flash-backed filesystem to
 * record a date.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * The digit test, spelled out rather than taken from <ctype.h>.
 * tests/resize_contracts/symbol_closure.sh holds sbin/utilbox, which this
 * program links into, to a closure that excludes _ctype_, and one call to
 * the macro from that header would carry the whole table in for a
 * comparison that needs none of it.
 */
#define ISDIGIT(c)	((c) >= '0' && (c) <= '9')

void		stime_arg1(char *, struct timeval *);
void		stime_arg2(char *, int, struct timeval *);
void		stime_argd(char *, struct timeval *);
void		stime_file(char *, struct timeval *);
void		usage(void);

int
main(int argc, char *argv[])
{
	struct timeval	 tv[2], set[2];
	struct stat	 sb;
	int		 aflag, cflag, mflag, ch, fd, len, rval, timeset;
	char		*p;

	aflag = cflag = mflag = timeset = 0;
	while ((ch = getopt(argc, argv, "acd:fmr:t:")) != -1)
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'd':
			timeset = 1;
			stime_argd(optarg, tv);
			break;
		case 'f':
			break;
		case 'm':
			mflag = 1;
			break;
		case 'r':
			timeset = 1;
			stime_file(optarg, tv);
			break;
		case 't':
			timeset = 1;
			stime_arg1(optarg, tv);
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	/* Default is both -a and -m. */
	if (aflag == 0 && mflag == 0)
		aflag = mflag = 1;

	/*
	 * If no -r or -t flag, at least two operands, the first of which
	 * is an 8 or 10 digit number, use the obsolete time specification.
	 */
	if (!timeset && argc > 1) {
		(void)strtol(argv[0], &p, 10);
		len = p - argv[0];
		if (*p == '\0' && (len == 8 || len == 10)) {
			timeset = 1;
			stime_arg2(*argv++, len == 10, tv);
			argc--;
		}
	}

	/* Otherwise use the current time of day. */
	if (!timeset) {
		if (gettimeofday(&tv[0], NULL) == -1)
			err(1, "gettimeofday");
		tv[1] = tv[0];
	}

	if (*argv == NULL)
		usage();

	for (rval = 0; *argv; ++argv) {
		set[0] = tv[0];
		set[1] = tv[1];

		/*
		 * utimes(2) writes both times or neither, so a run changing
		 * one of them reads the other back and writes it unaltered.
		 * A file that does not exist yet has no times to keep, and
		 * the branch below creates it with both set.
		 */
		if ((!aflag || !mflag) && stat(*argv, &sb) == 0) {
			if (!aflag) {
				set[0].tv_sec = sb.st_atime;
				set[0].tv_usec = 0;
			}
			if (!mflag) {
				set[1].tv_sec = sb.st_mtime;
				set[1].tv_usec = 0;
			}
		}

		/* Update the file's timestamp if it exists. */
		if (utimes(*argv, set) == 0)
			continue;
		if (errno != ENOENT) {
			rval = 1;
			warn("%s", *argv);
			continue;
		}

		/* Didn't exist; should we create it? */
		if (cflag)
			continue;

		/* Create the file. */
		fd = open(*argv, O_WRONLY | O_CREAT, 0666);
		if (fd == -1) {
			rval = 1;
			warn("%s", *argv);
			continue;
		}
		if (close(fd) == -1) {
			warn("%s", *argv);
			rval = 1;
			continue;
		}
		if (utimes(*argv, set) == -1) {
			warn("%s", *argv);
			rval = 1;
		}
	}
	return rval;
}

#define	ATOI2(s)	((s) += 2, ((s)[-2] - '0') * 10 + ((s)[-1] - '0'))

void
stime_arg1(char *arg, struct timeval *tvp)
{
	struct tm	*lt;
	time_t		 tmptime;
	int		 yearset;
	char		*dot, *p;
					/* Start with the current time. */
	tmptime = time(NULL);
	if ((lt = localtime(&tmptime)) == NULL)
		err(1, "localtime");
					/* [[CC]YY]MMDDhhmm[.SS] */
	for (p = arg, dot = NULL; *p != '\0'; p++) {
		if (*p == '.' && dot == NULL)
			dot = p;
		else if (!ISDIGIT((unsigned char)*p))
			goto terr;
	}
	if (dot == NULL)
		lt->tm_sec = 0;		/* Seconds defaults to 0. */
	else {
		*dot++ = '\0';
		if (strlen(dot) != 2)
			goto terr;
		lt->tm_sec = ATOI2(dot);
		if (lt->tm_sec > 61)	/* Could be leap second. */
			goto terr;
	}

	yearset = 0;
	switch (strlen(arg)) {
	case 12:			/* CCYYMMDDhhmm */
		lt->tm_year = (ATOI2(arg) * 100) - 1900;
		yearset = 1;
		/* FALLTHROUGH */
	case 10:			/* YYMMDDhhmm */
		if (yearset) {
			yearset = ATOI2(arg);
			lt->tm_year += yearset;
		} else {
			yearset = ATOI2(arg);
			/* POSIX logic: [00,68]=>20xx, [69,99]=>19xx */
			lt->tm_year = yearset;
			if (yearset < 69)
				lt->tm_year += 100;
		}
		/* FALLTHROUGH */
	case 8:				/* MMDDhhmm */
		lt->tm_mon = ATOI2(arg);
		if (lt->tm_mon > 12 || lt->tm_mon == 0)
			goto terr;
		--lt->tm_mon;		/* Convert from 01-12 to 00-11 */
		lt->tm_mday = ATOI2(arg);
		if (lt->tm_mday > 31 || lt->tm_mday == 0)
			goto terr;
		lt->tm_hour = ATOI2(arg);
		if (lt->tm_hour > 23)
			goto terr;
		lt->tm_min = ATOI2(arg);
		if (lt->tm_min > 59)
			goto terr;
		break;
	default:
		goto terr;
	}

	lt->tm_isdst = -1;		/* Figure out DST. */
	lt->tm_wday = -1;		/* sentinel for error */
	tvp[0].tv_sec = tvp[1].tv_sec = mktime(lt);
	if (tvp[0].tv_sec == -1 && lt->tm_wday == -1)
terr:		errx(1,
	"out of range or illegal time specification: [[CC]YY]MMDDhhmm[.SS]");

	tvp[0].tv_usec = tvp[1].tv_usec = 0;
}

void
stime_arg2(char *arg, int year, struct timeval *tvp)
{
	struct tm	*lt;
	time_t		 tmptime;
					/* Start with the current time. */
	tmptime = time(NULL);
	if ((lt = localtime(&tmptime)) == NULL)
		err(1, "localtime");

	lt->tm_mon = ATOI2(arg);	/* MMDDhhmm[YY] */
	if (lt->tm_mon > 12 || lt->tm_mon == 0)
		goto terr;
	--lt->tm_mon;			/* Convert from 01-12 to 00-11 */
	lt->tm_mday = ATOI2(arg);
	if (lt->tm_mday > 31 || lt->tm_mday == 0)
		goto terr;
	lt->tm_hour = ATOI2(arg);
	if (lt->tm_hour > 23)
		goto terr;
	lt->tm_min = ATOI2(arg);
	if (lt->tm_min > 59)
		goto terr;
	if (year) {
		year = ATOI2(arg);
		/* POSIX logic: [00,68]=>20xx, [69,99]=>19xx */
		lt->tm_year = year;
		if (year < 69)
			lt->tm_year += 100;
	}
	lt->tm_sec = 0;

	lt->tm_isdst = -1;		/* Figure out DST. */
	lt->tm_wday = -1;		/* sentinel for error */
	tvp[0].tv_sec = tvp[1].tv_sec = mktime(lt);
	if (tvp[0].tv_sec == -1 && lt->tm_wday == -1)
terr:		errx(1,
	"out of range or illegal time specification: MMDDhhmm[YY]");

	tvp[0].tv_usec = tvp[1].tv_usec = 0;
}

void
stime_file(char *fname, struct timeval *tvp)
{
	struct stat	sb;

	if (stat(fname, &sb))
		err(1, "%s", fname);
	tvp[0].tv_sec = sb.st_atime;
	tvp[0].tv_usec = 0;
	tvp[1].tv_sec = sb.st_mtime;
	tvp[1].tv_usec = 0;
}

/*
 * One fixed-width decimal field, and the character that has to follow it.
 * The ISO 8601 form -d takes is six of these, which is the whole of what
 * strptime(3) would read for "%F" and "%T".
 */
static int
read_field(char **pp, int width, int sep, int *out)
{
	char	*p = *pp;
	int	 value = 0;
	int	 i;

	for (i = 0; i < width; i++) {
		if (!ISDIGIT((unsigned char)p[i]))
			return (-1);
		value = value * 10 + (p[i] - '0');
	}
	p += width;
	if (sep != -1) {
		if (*p != sep)
			return (-1);
		p++;
	}
	*pp = p;
	*out = value;
	return (0);
}

void
stime_argd(char *arg, struct timeval *tvp)
{
	struct tm	 tm;
	char		*frac, *p;
	int		 utc = 0;
	int		 year, mon, mday, hour, min, sec;

	/* accept YYYY-MM-DD(T| )hh:mm:ss[(.|,)frac][Z] */
	memset(&tm, 0, sizeof(tm));
	p = arg;
	if (read_field(&p, 4, '-', &year) ||
	    read_field(&p, 2, '-', &mon) ||
	    read_field(&p, 2, -1, &mday))
		goto terr;
	if (*p != 'T' && *p != ' ')
		goto terr;
	p++;
	if (read_field(&p, 2, ':', &hour) ||
	    read_field(&p, 2, ':', &min) ||
	    read_field(&p, 2, -1, &sec))
		goto terr;

	/*
	 * The fields are range-checked here because mktime() normalizes
	 * rather than refuses: a thirteenth month is next January to it, so
	 * a specification naming one would otherwise be accepted and set a
	 * time the caller did not ask for. strptime(3) is what performs
	 * these checks where libc carries one.
	 */
	if (mon < 1 || mon > 12 || mday < 1 || mday > 31 || hour > 23 ||
	    min > 59 || sec > 61)		/* sec 61 could be a leap second */
		goto terr;

	tm.tm_year = year - 1900;
	tm.tm_mon = mon - 1;
	tm.tm_mday = mday;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;

	/*
	 * A fraction of a second is read and discarded: the filesystem
	 * records whole seconds, so keeping it would report a precision the
	 * stored time does not have.
	 */
	if (*p == '.' || *p == ',') {
		frac = ++p;
		while (ISDIGIT((unsigned char)*p))
			p++;
		if (p == frac)
			goto terr;
	}
	if (*p == 'Z') {
		utc = 1;
		p++;
	}
	if (*p != '\0')
		goto terr;

	tm.tm_isdst = -1;
	tm.tm_wday = -1;		/* sentinel for error */
	tvp[0].tv_sec = utc ? timegm(&tm) : mktime(&tm);
	if (tvp[0].tv_sec == -1 && tm.tm_wday == -1)
terr:		errx(1,
  "out of range or illegal time specification: YYYY-MM-DDThh:mm:ss[.frac][Z]");
	tvp[0].tv_usec = 0;
	tvp[1] = tvp[0];
}

void
usage(void)
{
	(void)fprintf(stderr,
"usage: touch [-acfm] [-d ccyy-mm-ddTHH:MM:SS[.frac][Z]] [-r file]\n"
"             [-t [[cc]yy]mmddHHMM[.SS]] file ...\n");
	exit(1);
}
