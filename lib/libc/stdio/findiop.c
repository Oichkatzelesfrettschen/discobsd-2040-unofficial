/*
 * Copyright (c) 1983, 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define active(iop)	((iop)->_flag & (_IOREAD|_IOWRT|_IORW))

/*
 * The static streams: stdin, stdout, stderr and five more. A ninth
 * concurrent stream allocates through _f_morefiles. Each FILE is 24 bytes,
 * a 20-byte cursor and flag block plus the one-byte pushback slot padded to
 * the word; the assertion pins the layout every program's getc and putc
 * macros were compiled against.
 */
#define NSTATIC	8

_Static_assert(sizeof(FILE) == 24, "FILE layout: getc/putc macros are compiled into every program");

FILE _iob[NSTATIC] = {
	{ ._flag = _IOREAD,        ._file = 0 },	/* stdin  */
	{ ._flag = _IOWRT,         ._file = 1 },	/* stdout */
	{ ._flag = _IOWRT|_IONBF,  ._file = 2 },	/* stderr */
};

static	char sbuf[NSTATIC];
char	*_smallbuf = sbuf;
static	FILE	**iobglue;
static	FILE	**endglue;

/*
 * Grow past the static array: a glue table of pointers indexed like the
 * descriptor table, and a one-byte unbuffered slot per descriptor. Both
 * allocations must succeed together, because _filbuf indexes _smallbuf by
 * descriptor and the static sbuf covers only NSTATIC of them.
 */
static int
_f_morefiles(void)
{
	FILE **iov;
	FILE *fp;
	char *small;
	int nfiles;

	nfiles = getdtablesize();
	iobglue = calloc((size_t) nfiles, sizeof *iobglue);
	if (iobglue == NULL)
		return (0);
	small = calloc((size_t) nfiles, sizeof *small);
	if (small == NULL) {
		free(iobglue);
		iobglue = NULL;
		return (0);
	}
	endglue = iobglue + nfiles;
	for (fp = _iob, iov = iobglue; fp < &_iob[NSTATIC]; /* void */)
		*iov++ = fp++;
	_smallbuf = small;
	return (1);
}

/*
 * Find a free FILE for fopen et al: a free static slot first, then the
 * glue table, allocating a FILE into an empty glue entry on demand.
 */
FILE *
_findiop(void)
{
	FILE **iov, *iop;

	if (iobglue == NULL) {
		for (iop = _iob; iop < _iob + NSTATIC; iop++)
			if (!active(iop))
				return (iop);
		if (_f_morefiles() == 0) {
			errno = ENOMEM;
			return (NULL);
		}
	}

	iov = iobglue;
	while (*iov != NULL && active(*iov))
		if (++iov >= endglue) {
			errno = EMFILE;
			return (NULL);
		}

	if (*iov == NULL)
		*iov = calloc(1, sizeof **iov);
	return (*iov);
}

void
f_prealloc(void)
{
	FILE **iov;

	if (iobglue == NULL && _f_morefiles() == 0)
		return;
	for (iov = iobglue; iov < endglue; iov++)
		if (*iov == NULL)
			*iov = calloc(1, sizeof **iov);
}

/*
 * Apply a function to every open stream, and report EOF when any one call
 * did. fflush(NULL) is the caller that needs the verdict: C17 7.21.5.2p3
 * makes it the flush of every stream and p4 makes EOF its answer to a write
 * error on any of them, so the walk carries the status.
 *
 * The status is the bitwise or of the returns, which fflush and fclose,
 * the two functions passed here, answer with EOF or zero; EOF is every bit
 * set, so the or is the verdict and costs no branch.
 */
int
_fwalk(int (*function)(FILE *))
{
	FILE **iov;
	FILE *fp;
	int status;

	status = 0;
	if (iobglue == NULL) {
		for (fp = _iob; fp < &_iob[NSTATIC]; fp++)
			if (active(fp))
				status |= (*function)(fp);
	} else {
		for (iov = iobglue; iov < endglue; iov++)
			if (*iov && active(*iov))
				status |= (*function)(*iov);
	}
	return (status);
}

/*
 * Close every stream at exit. C17 7.22.4.4p2 leaves the program no way to
 * observe a failure here, so the walk's status is dropped.
 */
void
_cleanup(void)
{
	(void) _fwalk(fclose);
}
