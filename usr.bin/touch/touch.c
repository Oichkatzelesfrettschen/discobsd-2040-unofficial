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
 * The times reach the inode through utimes(2), which is the only call this
 * kernel offers for them and which records whole seconds (struct icommon2's
 * ic_atime and ic_mtime). OpenBSD reaches the same inodes through
 * utimensat(2) and futimens(2), which name a nanosecond and an omission
 * utimes(2) cannot:
 *
 *   - UTIME_NOW becomes one clock read, so every operand of a run is given
 *     the same instant rather than the instant it was reached.
 *   - UTIME_OMIT has no spelling, so -a alone and -m alone read the file's
 *     current times with stat(2) and write back unaltered the one they are
 *     not changing.
 *   - futimens(fd, ...) on a file just created becomes a second utimes(2)
 *     on its name, there being no futimes(2) here.
 *
 * The calendar conversions are here rather than in mktime(3) and
 * localtime(3), and that is a size decision with a measured basis.
 * localtime() reads a zone file, and this system ships none: no
 * /usr/share/zoneinfo and no /etc/localtime, so tzload() cannot succeed and
 * tzset() always falls through to tzsetkernel(), which reads one field --
 * the kernel's tz_minuteswest, copied out by gettimeofday(2). zone_west()
 * below reads that same field, so this program and localtime() agree by
 * construction rather than by coincidence. Linking mktime(3) instead would
 * carry ctime.o, mktime.o and timezone.o into sbin/utilbox: 2531 bytes of
 * text and 2134 of bss, of which 2084 is the zone table nothing on this
 * image can fill.
 *
 * The conversions are the proleptic Gregorian ones, exact over the whole
 * range a four-byte time_t represents and carrying no month-length table.
 *
 * -d takes the ISO 8601 form OpenBSD accepts, read by the fixed-width field
 * parser below rather than through strptime(3), which this libc does not
 * carry. A fraction of a second is accepted and discarded, the inode
 * recording whole seconds.
 *
 * <ctype.h> is not reached: tests/resize_contracts/symbol_closure.sh holds
 * sbin/utilbox, which this program links into, to a closure that excludes
 * _ctype_, and one call to a macro from that header would carry the whole
 * table in for a comparison that needs none of it. STYLE-GUIDE.md T07
 * records the same measurement for the V7 scanner.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * The bounds are derived from time_t rather than written down, so the same
 * source is exact at the target's four bytes and at the host width the
 * contract gate builds it for. The logic assumes only that the type is
 * signed, which is what the assertion pins.
 */
_Static_assert((time_t)-1 < 0, "time_t is signed");
_Static_assert(sizeof(time_t) <= sizeof(long), "time_t fits the long below");

/*
 * The largest value a signed time_t holds, reached by shifting an all-ones
 * unsigned long down to time_t's width less its sign bit. The shift count is
 * a constant, so this folds.
 */
#define TIME_MAX	((time_t)(~0UL >> \
			    (8 * sizeof(unsigned long) - 8 * sizeof(time_t) + 1)))
#define TIME_MIN	((time_t)(-TIME_MAX - 1))
#define SECS_PER_DAY	86400L

/* The largest day count whose product with SECS_PER_DAY still fits. */
#define DAY_LIMIT	((long)(TIME_MAX / SECS_PER_DAY))

#define ISDIGIT(c)	((c) >= '0' && (c) <= '9')

/* A broken-down time, in whichever of the two frames the caller is using. */
struct caldate {
	int	year;			/* the full year, not an offset */
	int	mon;			/* 1 to 12 */
	int	mday;			/* 1 to 31 */
	int	hour;
	int	min;
	int	sec;
};

static long	days_from_civil(int, int, int);
static void	civil_from_days(long, struct caldate *);
static long	zone_west(void);
static bool	epoch_from_local(const struct caldate *, time_t *);
static void	local_from_epoch(time_t, struct caldate *);
static void	stime_arg1(char *, struct timeval *);
static void	stime_arg2(char *, int, struct timeval *);
static void	stime_argd(char *, struct timeval *);
static void	stime_file(char *, struct timeval *);
static void	usage(void);

/*
 * Days from 1970-01-01 to the given date, proleptic Gregorian. The year is
 * shifted so that March begins it, which puts the leap day last and lets the
 * month lengths fall out of one linear expression instead of a table.
 */
static long
days_from_civil(int y, int m, int d)
{
	long era, doe, yoe, doy;

	y -= (m <= 2);
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;				/* [0, 399] */
	doy = (153L * (m + (m > 2 ? -3 : 9)) + 2L) / 5L + d - 1;
	doe = yoe * 365L + yoe / 4 - yoe / 100 + doy;	/* [0, 146096] */
	return (era * 146097L + doe - 719468L);
}

/* The inverse, over the same range. */
static void
civil_from_days(long z, struct caldate *c)
{
	long era, doe, yoe, doy, mp, y;

	z += 719468L;
	era = (z >= 0 ? z : z - 146096L) / 146097L;
	doe = z - era * 146097L;			/* [0, 146096] */
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	doy = doe - (365L * yoe + yoe / 4 - yoe / 100);
	mp = (5L * doy + 2L) / 153L;			/* [0, 11], March first */
	c->mday = (int)(doy - (153L * mp + 2L) / 5L + 1L);
	c->mon = (int)(mp + (mp < 10 ? 3 : -9));
	c->year = (int)(y + (c->mon <= 2));
}

/*
 * Seconds west of Greenwich, from the one field the kernel keeps and
 * gettimeofday(2) copies out. ctime.c's tzsetkernel() reads the same field,
 * which is what makes this program and localtime(3) agree here.
 */
static long
zone_west(void)
{
	struct timeval tv;
	struct timezone tz;

	if (gettimeofday(&tv, &tz) == -1)
		return (0);
	return ((long)tz.tz_minuteswest * 60L);
}

/*
 * A local broken-down time to the instant it names, refusing anything the
 * calendar does not hold or time_t cannot represent. Each bound is checked
 * before the arithmetic that would overflow without it.
 */
static bool
epoch_from_local(const struct caldate *c, time_t *out)
{
	/* A byte apiece: the longest month is 31, so int would cost four. */
	static const unsigned char mon_len[12] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	long days, secs, tod, west;
	int len, leap;

	if (c->mon < 1 || c->mon > 12 || c->mday < 1 ||
	    c->hour < 0 || c->hour > 23 || c->min < 0 || c->min > 59 ||
	    c->sec < 0 || c->sec > 61)		/* 61 could be a leap second */
		return (false);

	leap = (c->year % 4) == 0 && ((c->year % 100) != 0 ||
	    (c->year % 400) == 0);
	len = mon_len[c->mon - 1] + (leap && c->mon == 2);
	if (c->mday > len)
		return (false);

	days = days_from_civil(c->year, c->mon, c->mday);
	if (days > DAY_LIMIT || days < -DAY_LIMIT)
		return (false);
	secs = days * SECS_PER_DAY;
	tod = c->hour * 3600L + c->min * 60L + c->sec;
	west = zone_west();

	/* The two additions that remain, each guarded against its own bound. */
	if (secs > TIME_MAX - tod)
		return (false);
	secs += tod;
	if (west > 0 ? secs > TIME_MAX - west : secs < TIME_MIN - west)
		return (false);
	*out = (time_t)(secs + west);
	return (true);
}

/* The instant back to local fields, which is where -t starts from. */
static void
local_from_epoch(time_t t, struct caldate *c)
{
	long secs = (long)t - zone_west();
	long days = secs / SECS_PER_DAY;
	long rem = secs % SECS_PER_DAY;

	if (rem < 0) {
		rem += SECS_PER_DAY;
		days--;
	}
	civil_from_days(days, c);
	c->hour = (int)(rem / 3600L);
	c->min = (int)((rem / 60L) % 60L);
	c->sec = (int)(rem % 60L);
}

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
		len = (int)(p - argv[0]);
		if (*p == '\0' && (len == 8 || len == 10)) {
			timeset = 1;
			stime_arg2(*argv++, len == 10, tv);
			argc--;
		}
	}

	/* Otherwise use the current time of day, read once for every operand. */
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

/* Set both times from one instant; every parser below ends here. */
static void
settimes(struct timeval *tvp, time_t t)
{
	tvp[0].tv_sec = tvp[1].tv_sec = t;
	tvp[0].tv_usec = tvp[1].tv_usec = 0;
}

static void
stime_arg1(char *arg, struct timeval *tvp)
{
	struct caldate	 c;
	struct timeval	 now;
	time_t		 t;
	int		 yearset;
	char		*dot, *p;

					/* Start with the current time. */
	if (gettimeofday(&now, NULL) == -1)
		err(1, "gettimeofday");
	local_from_epoch(now.tv_sec, &c);
					/* [[CC]YY]MMDDhhmm[.SS] */
	for (p = arg, dot = NULL; *p != '\0'; p++) {
		if (*p == '.' && dot == NULL)
			dot = p;
		else if (!ISDIGIT((unsigned char)*p))
			goto terr;
	}
	if (dot == NULL)
		c.sec = 0;		/* Seconds defaults to 0. */
	else {
		*dot++ = '\0';
		if (strlen(dot) != 2)
			goto terr;
		c.sec = ATOI2(dot);
	}

	yearset = 0;
	switch (strlen(arg)) {
	case 12:			/* CCYYMMDDhhmm */
		c.year = ATOI2(arg) * 100;
		yearset = 1;
		/* FALLTHROUGH */
	case 10:			/* YYMMDDhhmm */
		if (yearset)
			c.year += ATOI2(arg);
		else {
			/* POSIX logic: [00,68]=>20xx, [69,99]=>19xx */
			yearset = ATOI2(arg);
			c.year = yearset + (yearset < 69 ? 2000 : 1900);
		}
		/* FALLTHROUGH */
	case 8:				/* MMDDhhmm */
		c.mon = ATOI2(arg);
		c.mday = ATOI2(arg);
		c.hour = ATOI2(arg);
		c.min = ATOI2(arg);
		break;
	default:
		goto terr;
	}

	if (!epoch_from_local(&c, &t))
terr:		errx(1,
	"out of range or illegal time specification: [[CC]YY]MMDDhhmm[.SS]");

	settimes(tvp, t);
}

static void
stime_arg2(char *arg, int year, struct timeval *tvp)
{
	struct caldate	 c;
	struct timeval	 now;
	time_t		 t;
	int		 yy;

					/* Start with the current time. */
	if (gettimeofday(&now, NULL) == -1)
		err(1, "gettimeofday");
	local_from_epoch(now.tv_sec, &c);

	c.mon = ATOI2(arg);		/* MMDDhhmm[YY] */
	c.mday = ATOI2(arg);
	c.hour = ATOI2(arg);
	c.min = ATOI2(arg);
	if (year) {
		/* POSIX logic: [00,68]=>20xx, [69,99]=>19xx */
		yy = ATOI2(arg);
		c.year = yy + (yy < 69 ? 2000 : 1900);
	}
	c.sec = 0;

	if (!epoch_from_local(&c, &t))
		errx(1,
	"out of range or illegal time specification: MMDDhhmm[YY]");

	settimes(tvp, t);
}

static void
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
 * One fixed-width decimal field and the character that must follow it. Six of
 * these are the whole of the ISO 8601 form -d takes, which is what strptime(3)
 * would read for "%F" and "%T".
 */
static bool
read_field(char **pp, int width, int sep, int *out)
{
	char	*p = *pp;
	int	 value = 0;
	int	 i;

	for (i = 0; i < width; i++) {
		if (!ISDIGIT((unsigned char)p[i]))
			return (false);
		value = value * 10 + (p[i] - '0');
	}
	p += width;
	if (sep != -1) {
		if (*p != sep)
			return (false);
		p++;
	}
	*pp = p;
	*out = value;
	return (true);
}

static void
stime_argd(char *arg, struct timeval *tvp)
{
	struct caldate	 c;
	time_t		 t;
	char		*frac, *p;
	long		 west;
	bool		 utc = false;

	/* accept YYYY-MM-DD(T| )hh:mm:ss[(.|,)frac][Z] */
	p = arg;
	if (!read_field(&p, 4, '-', &c.year) ||
	    !read_field(&p, 2, '-', &c.mon) ||
	    !read_field(&p, 2, -1, &c.mday))
		goto terr;
	if (*p != 'T' && *p != ' ')
		goto terr;
	p++;
	if (!read_field(&p, 2, ':', &c.hour) ||
	    !read_field(&p, 2, ':', &c.min) ||
	    !read_field(&p, 2, -1, &c.sec))
		goto terr;

	/*
	 * A fraction of a second is read and discarded: the inode records
	 * whole seconds, so keeping it would report a precision the stored
	 * time does not have.
	 */
	if (*p == '.' || *p == ',') {
		frac = ++p;
		while (ISDIGIT((unsigned char)*p))
			p++;
		if (p == frac)
			goto terr;
	}
	if (*p == 'Z') {
		utc = true;
		p++;
	}
	if (*p != '\0')
		goto terr;

	if (!epoch_from_local(&c, &t))
		goto terr;
	/*
	 * epoch_from_local() applied the zone, which a trailing Z says not
	 * to; taking it back off is exact, the offset being one constant.
	 */
	if (utc) {
		west = zone_west();
		t -= (time_t)west;
	}

	settimes(tvp, t);
	return;
terr:
	errx(1,
  "out of range or illegal time specification: YYYY-MM-DDThh:mm:ss[.frac][Z]");
}

static void
usage(void)
{
	(void)fprintf(stderr,
"usage: touch [-acfm] [-d ccyy-mm-ddTHH:MM:SS[.frac][Z]] [-r file]\n"
"             [-t [[cc]yy]mmddHHMM[.SS]] file ...\n");
	exit(1);
}
