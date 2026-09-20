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
 *
 *	@(#)local.h	8.4 (2.11BSD) 2025/12/25
 */

/*
 * Information local to this implementation of stdio,
 * in particular, macros and private variables.
 */

extern int	__sflush(FILE *);
extern FILE	*__sfp(void);
extern int	__srefill(FILE *);
extern int	__sread(void *, char *, int);
extern int	__swrite(void *, char const *, int);
extern fpos_t	__sseek(void *, fpos_t, int);
extern int	__sclose(void *);
extern void	_cleanup(void);
extern void	__smakebuf(FILE *);
extern int	__swhatbuf(FILE *, size_t *, int *);
extern int	_fwalk(int (*)(FILE *));
extern int	__swsetup(FILE *);
extern int	__sflags(const char *, int *);
extern int	__scopyfops(FILE *);
extern int	__shasub(FILE *);
extern void	__sfreeub(FILE *);

extern int	__sdidinit;
extern struct	__sfops __sdefops;

#define DEFFILEMODE     (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)

/*
 * The fops struct is allocated on demand. Nothing in this struct
 * is needed for the Ansi-C requirements.
 * If fops is the default struct then cookie is the FILE desc,
 * otherwise it is locally set in the allocated struct.
 */
#define	ISFALL(fp)  ((fp)->_fops != &__sdefops)
#define	COOKIE(fp)  (ISFALL(fp) ? (fp)->_fops->_cookie : (void *)(fp))

/*
 * Return true iff the given FILE cannot be written now.
 */
#define	cantwrite(fp) \
	((((fp)->_flags & __SWR) == 0 || (fp)->_bf._base == NULL) && \
	 __swsetup(fp))

/*
 * Test whether the given stdio file has an active ungetc buffer;
 * release such a buffer, without restoring ordinary unread data.
 */
#define	HASUB(fp) ((fp)->_fops->_ub._base != NULL)
#define FREEUB(fp) __sfreeub(fp)

/*
 * test for an fgetln() buffer.
 */
#define	HASLB(fp) ((fp)->_lb._base != NULL)
#define	FREELB(fp) { \
	free((char *)(fp)->_lb._base); \
	(fp)->_lb._base = NULL; \
}

/*
 * These are used when calling the cleverly broken out floating point routines.
 */
#define FLADJ	000001	/* left adjustment */
#define FPLUS	000002	/* add '+' for positive numbers */
#define FSPC	000004	/* add ' ' for positive numbers */
#define FALT	000010	/* alternate format */
#define FZERO	000020	/* pad fld width with leading '0' */

#define MLONG	000040	/* long format */
#define MLLONG	000100	/* long long format */
#define MCHAR	000200	/* short format */
#define MCCHAR	000400	/* char format */

#define CDEC	001000	/* decimal output */
#define CHEX	002000	/* hex output */
#define CSGN	004000	/* output is signed */
#define CUC	010000	/* uppercase digits in output */

#define ISNEG	020000	/* value is negative */
#define NDFND	040000	/* precision digits found */

struct prinfo {
	FILE *iob;
	va_list *app;
	int width;	/* field width, 0 if no width */
	int ndigit;	/* precision (# of digits) */
	int flags;
	int nwrtn;	/* # of written characters in output */

	char *bend;	/* begin/end of output buffer */
	int nallo;	/* size of current buffer */
};

/* floating point conversion routines specific to pdp11 floating point */
char *__pfcom(int, struct prinfo *);
char *__acvt(double d, int nd, char *buf, int *sign);
/* XXX the functions below should be declared in stdlib.h */
char *ecvt(double value, int ndigit, int *decpt, int *sign);
char *fcvt(double value, int ndigit, int *decpt, int *sign);
char *gcvt(double value, int ndigit, char *buf);
