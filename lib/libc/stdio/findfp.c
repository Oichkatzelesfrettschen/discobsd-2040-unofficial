/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)findfp.c	8.3 (2.11BSD) 2025/12/25";
#endif /* LIBC_SCCS and not lint */

#include <sys/param.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "local.h"

/*
 * ragge 200607
 * Removed glue, not useful on pdp11.
 * Split out fileops to save data space.
 * f_prealloc() removed.
 */

int	__sdidinit;

#define	std(flags, file) \
	{ ._flags = (flags), ._file = (file), ._fops = &__sdefops }

struct __sfops __sdefops = {
	._close = __sclose, ._read = __sread, ._seek = __sseek, ._write = __swrite,
};

FILE __sF[FOPEN_MAX] = {
	std(__SRD, STDIN_FILENO),		/* stdin */
	std(__SWR, STDOUT_FILENO),		/* stdout */
	std(__SWR|__SNBF, STDERR_FILENO)	/* stderr */
};

static struct flink {
	struct flink *next;
	struct __sFILE sfile;
} *fpole;

/*
 * Find a free FILE for fopen et al.
 */
FILE *
__sfp()
{
	register FILE *fp;
	register int n;
	register struct flink *g;

	for (n = 0, fp = __sF; n < FOPEN_MAX; fp++, n++)
		if (fp->_flags == 0)
			goto found;
	for (g = fpole; g; g = g->next)
		if (g->sfile._flags == 0) {
			fp = &g->sfile;
			goto found;
		}
	if ((g = malloc(sizeof(struct flink))) == NULL)
		return NULL;
	g->next = fpole;
	fpole = g;
	fp = &g->sfile;

found:
	fp->_flags = 1;		/* reserve this slot; caller sets real flags */
	fp->_p = NULL;		/* no current pointer */
	fp->_w = 0;		/* nothing to read or write */
	fp->_r = 0;
	fp->_bf._base = NULL;	/* no buffer */
	fp->_bf._size = 0;
	fp->_lbfsize = 0;	/* not line buffered */
	fp->_file = -1;		/* no file */
	fp->_fops = &__sdefops;	/* set default fileops */
	return (fp);
}

/*
 * Copy the file operations struct when local changes are needed.
 */
int
__scopyfops(register FILE *fp)
{
	register struct __sfops *nf;

	if (ISFALL(fp))
		return 0; /* Already copied */

	if ((nf = malloc(sizeof(struct __sfops))) == NULL)
		return EOF;
	*nf = __sdefops;
	fp->_fops = nf;
	nf->_cookie = fp; /* default */
	return 0;
}

int
_fwalk(int (*function)(FILE *))
{
	register FILE *fp;
	register struct flink *g;
	register int n, ret;

	ret = 0;
	for (n = 0, fp = __sF; n < FOPEN_MAX; n++, fp++)
		if (fp->_flags != 0)
			ret |= (*function)(fp);
	for (g = fpole; g; g = g->next)
		if (g->sfile._flags != 0)
			ret |= (*function)(&g->sfile);
	return (ret);
}

/*
 * exit() calls _cleanup() through *__cleanup, set whenever we
 * open or buffer a file.  This chicanery is done so that programs
 * that do not use stdio need not link it all in.
 *
 * The name `_cleanup' is, alas, fairly well known outside stdio.
 */
void
_cleanup()
{
	/* (void) _fwalk(fclose); */
	(void) _fwalk(__sflush);		/* `cheating' */
}
