/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"

char    *trapcom[MAXTRAP];

static void setsig(int n);
static void sigdone(int sig);

BOOL    trapflg[MAXTRAP] =
{
	0,
	0,      /* 1 hangup */
	0,      /* 2 interrupt */
	0,      /* 3 quit */
	0,      /* 4 illegal instr */
	0,      /* 5 trace trap */
	0,      /* 6 IOT */
	0,      /* 7 EMT */
	0,      /* 8 float pt. exp */
	0,      /* 9 kill */
	0,      /* 10 bus error */
	0,      /* 11 memory faults */
	0,      /* 12 bad sys call */
	0,      /* 13 bad pipe call */
	0,      /* 14 alarm */
	0,      /* 15 software termination */
	0,      /* 16 urgent */
	0,      /* 17 stop */
	0,      /* 18 tstp */
	0,      /* 19 cont */
	0,      /* 20 death or stop of child */
	0,      /* 21 ttin */
	0,      /* 22 ttou */
	0,      /* 23 tint */
};

sig_t sigval[] = {
	0,
	sigdone,
	fault,
	fault,
	sigdone,
	sigdone,
	SIG_DFL,
	sigdone,
	sigdone,
	SIG_DFL,
	sigdone,
	sigdone,
	sigdone,
	sigdone,
	fault,
	fault,
	sigdone,
	SIG_DFL,
	SIG_DFL,
	SIG_DFL,
	sigdone,
	SIG_DFL,
	SIG_DFL,
	SIG_DFL,
};

static void
sigdone(int sig)
{
	(void)sig;
	done();
}

/* ========     fault handling routines    ======== */

void
fault(int sig)
{
	register int    flag;

	signal(sig, fault);
	if (sig == SIGSEGV)
	{
		if (setbrk(brkincr) == (char *)-1)
			error(nospace);
	}
	else if (sig == SIGALRM)
	{
		if (flags & waiting)
			done();
	}
	else
	{
		flag = (trapcom[sig] ? TRAPSET : SIGSET);
		trapnote |= flag;
		trapflg[sig] |= flag;
		if (sig == SIGINT)
			wasintr++;
	}
}

void
stdsigs(void)
{
	setsig(SIGHUP);
	setsig(SIGINT);
	ignsig(SIGQUIT);
	setsig(SIGILL);
	setsig(SIGTRAP);
	setsig(SIGIOT);
	setsig(SIGEMT);
	setsig(SIGFPE);
	setsig(SIGBUS);
	signal(SIGSEGV, fault);
	setsig(SIGSYS);
	setsig(SIGPIPE);
	setsig(SIGALRM);
	setsig(SIGTERM);
#ifdef SIGUSR1
	setsig(SIGUSR1);
#endif
#ifdef SIGUSR2
	setsig(SIGUSR2);
#endif
}

int
ignsig(int n)
{
	register int    s = 0, i;

	if ((i = n) == SIGSEGV)
	{
		clrsig(i);
		failed(badtrap, "cannot trap 11");
	}
	else if ((s = (signal(i, SIG_IGN) == SIG_IGN)) == 0)
	{
		trapflg[i] |= SIGMOD;
	}
	return(s);
}

void
getsig(int n)
{
	register int    i;

	if (trapflg[i = n] & SIGMOD || ignsig(i) == 0)
		signal(i, fault);
}


static void
setsig(int n)
{
	register int    i;

	if (ignsig(i = n) == 0)
		signal(i, sigval[i]);
}

void
oldsigs(void)
{
	register int    i;
	register char   *t;

	i = MAXTRAP;
	while (i--)
	{
		t = trapcom[i];
		if (t == NIL || *t)
			clrsig(i);
		trapflg[i] = 0;
	}
	trapnote = 0;
}

void
clrsig(int i)
{
	free(trapcom[i]);
	trapcom[i] = NIL;
	if (trapflg[i] & SIGMOD)
	{
		trapflg[i] &= ~SIGMOD;
		signal(i, sigval[i]);
	}
}

/*
 * check for traps
 */
void
chktrap(void)
{
	register int    i = MAXTRAP;
	register char   *t;

	trapnote &= ~TRAPSET;
	while (--i)
	{
		if (trapflg[i] & TRAPSET)
		{
			trapflg[i] &= ~TRAPSET;
			if ((t = trapcom[i]))
			{
				int     savxit = exitval;

				execute_string_input(t, (int)(flags & errflg));
				exitval = savxit;
				exitset();
			}
		}
	}
}
