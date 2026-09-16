/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

/* ========     error handling  ======== */

failure(s1, s2, xno)
char    *s1, *s2;
int     xno;
{
	prp();
	prs_cntl(s1);
	if (s2)
	{
		prs(colon);
		prs(s2);
	}
	newline();
	exitsh(xno);
}

failed(s1, s2)
char    *s1, *s2;
{
	failure(s1, s2, ERROR);
}

/*
 * A command that cannot be run: report it and set the exit status the
 * way POSIX XCU 2.8.1 and 2.9.1 ask (127 not found, 126 not executable),
 * then let the list it sits in continue -- "nosuchcmd; echo after" must
 * print "after". failure() would longjmp back to the prompt, dropping
 * the rest of the line, or end a script. A forked child has nothing
 * to continue with and exits with the status; under set -e the shell
 * exits as it does on any failure.
 */
cmdfail(s1, s2, xno)
char    *s1, *s2;
int     xno;
{
	if (flags & (forked | errflg))
		failure(s1, s2, xno);
	prp();
	prs_cntl(s1);
	if (s2)
	{
		prs(colon);
		prs(s2);
	}
	newline();
	exitval = xno;
}

error(s)
char    *s;
{
	failed(s, NIL);
}

exitsh(xno)
int     xno;
{
	/*
	 * Arrive here from `FATAL' errors
	 *  a) exit command,
	 *  b) default trap,
	 *  c) fault with no trap set.
	 *
	 * Action is to return to command level or exit.
	 */
	exitval = xno;
	flags |= eflag;
	if ((flags & (forked | errflg | ttyflg)) != ttyflg)
		done();
	else
	{
		clearup();
		restore(0);
		clear_buff();
		execbrk = breakcnt = funcnt = 0;
		longjmp(errshell, 1);
	}
}

void
done()
{
	register char   *t;

	if (t = trapcom[0])
	{
		trapcom[0] = NIL;
		execexp(t, 0);
		free(t);
	}
	else
		chktrap();

	rmtemp(NIL);
	rmfunctmp();

#ifdef ACCOUNT
	doacct();
#endif
	exit(exitval);
}

rmtemp(base)
struct ionod    *base;
{
	while (iotemp > base)
	{
		unlink(iotemp->ioname);
		free(iotemp->iolink);
		iotemp = iotemp->iolst;
	}
}

rmfunctmp()
{
	while (fiotemp)
	{
		unlink(fiotemp->ioname);
		fiotemp = fiotemp->iolst;
	}
}
