/*
 * Copyright (c) 1980,1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <utmp.h>
#include <setjmp.h>
#include <sys/reboot.h>
#include <errno.h>
#include <sys/file.h>
#include <ttyent.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <paths.h>
#include <grp.h>

#include "daemons.h"

//#undef KERN_SECURELVL

#define	LINSIZ	sizeof(wtmp.ut_line)
#define	CMDSIZ	200	/* max string length for getty or window command*/
#define SCPYN(a, b)	strncpy(a, b, sizeof(a))
#define SCMPN(a, b)	strncmp(a, b, sizeof(a))

char	shell[]	= _PATH_BSHELL;
char	minus[]	= "-";
char	runc[]	= "/etc/rc";
char	utmpf[]	= _PATH_UTMP;
char	wtmpf[]	= _PATH_WTMP;
char	ctty[]	= _PATH_CONSOLE;

void merge (int signum);

struct utmp wtmp;
struct	tab
{
	char	line[LINSIZ];
	char	comn[CMDSIZ];
	char	xflag;
	int	pid;
	int	wpid;		/* window system pid for SIGHUP	*/
	char	wcmd[CMDSIZ];	/* command to start window system process */
	time_t	gettytime;
	int	gettycnt;
	time_t	windtime;
	int	windcnt;
	struct	tab *next;
} *itab;

int	fi;
int	mergflag;
char	tty[20];
jmp_buf	sjbuf, shutpass;
time_t	time0;

void	reset();
void	dalarm(int sig);
void	setsecuritylevel();
int	getsecuritylevel();
extern	int errno;

struct	sigvec rvec = { reset, sigmask (SIGHUP), 0 };

jmp_buf	idlebuf;

/*
 * Supervised daemons from /etc/daemons (daemons.h). init forks each entry,
 * so its pid is known and its exit reaches multiple() or idle(). dalarm()
 * only records that a restart deadline passed; multiple() then calls
 * dschedule() outside signal context, because the SIGALRM handler can
 * interrupt malloc(), stdio and the utmp update in rmut(). The handler is
 * installed with SV_INTERRUPT so that the alarm ends wait(2) with EINTR
 * instead of restarting it. merge() changes dtab as the SIGHUP handler,
 * as it changes itab, with SIGALRM in its sigvec mask.
 */
struct	dtab *dtab;
int	didle;		/* idle(): reap and schedule, start nothing */
volatile sig_atomic_t dtimer;	/* dalarm(): a restart deadline passed */

void	dschedule(void);
int	dreap(int pid, int status);

/*
 * fork() returns -1 with ENOMEM when swap cannot hold the child and with
 * EAGAIN when the process table is full (fork(2)); both clear as other
 * processes exit. spawn() logs the first refusal, waits between attempts so
 * a refusal cannot spin init, and returns -1 after SPAWN_TRIES attempts.
 */
#define	SPAWN_TRIES	6
#define	SPAWN_DELAY	5	/* seconds between attempts */

int
spawn(what)
	const char *what;
{
	register int pid, tries;

	for (tries = 1; ; tries++) {
		pid = fork();
		if (pid >= 0)
			return (pid);
		if (tries == 1)
			syslog(LOG_ERR, "cannot fork %s: %m", what);
		if (tries >= SPAWN_TRIES)
			return (-1);
		sleep(SPAWN_DELAY);
	}
}

/*
 * Catch a SIGSYS signal.
 *
 * These may arise if a system does not support sysctl.
 * We tolerate up to 25 of these, then throw in the towel.
 */
void
badsys (sig)
	int sig;
{
	static int badcount = 0;

	if (badcount++ < 25)
		return;
	syslog(LOG_EMERG, "fatal signal: %d", sig);
	sleep(30);
	_exit(sig);
}

void
idlehup (sig)
	int sig;
{

	longjmp (idlebuf, 1);
}

/*
 * Remove utmp entry.
 */
void
rmut(p)
	register struct tab *p;
{
	register int f;
	int found = 0;
	static unsigned utmpsize;
	static struct utmp *utmp;
	register struct utmp *u;
	int nutmp;
	struct stat statbf;

	f = open(utmpf, O_RDWR);
	if (f >= 0) {
		fstat(f, &statbf);
		if (utmpsize < statbf.st_size) {
			unsigned nsize = statbf.st_size + 10 * sizeof(struct utmp);
			struct utmp *nutmp;

			if (utmp)
				nutmp = (struct utmp *)realloc(utmp, nsize);
			else
				nutmp = (struct utmp *)malloc(nsize);
			/* utmpsize grows only with the buffer: the read below
			 * is bounded by st_size, which the old buffer may not
			 * hold, and the old buffer is what init keeps. */
			if (nutmp) {
				utmp = nutmp;
				utmpsize = nsize;
			} else
				syslog(LOG_ERR, "utmp malloc failed");
		}
		if (statbf.st_size && utmp) {
			nutmp = read(f, utmp, statbf.st_size < utmpsize ?
			    (int)statbf.st_size : (int)utmpsize);
			nutmp /= sizeof(struct utmp);
			for (u = utmp ; u < &utmp[nutmp] ; u++) {
				if (SCMPN(u->ut_line, p->line) ||
				    u->ut_name[0]==0)
					continue;
				lseek(f, ((long)u)-((long)utmp), L_SET);
				SCPYN(u->ut_name, "");
				SCPYN(u->ut_host, "");
				time(&u->ut_time);
				write(f, (char *)u, sizeof(*u));
				found++;
			}
		}
		close(f);
	}
	if (found) {
		f = open(wtmpf, O_WRONLY|O_APPEND);
		if (f >= 0) {
			SCPYN(wtmp.ut_line, p->line);
			SCPYN(wtmp.ut_name, "");
			SCPYN(wtmp.ut_host, "");
			time(&wtmp.ut_time);
			write(f, (char *)&wtmp, sizeof(wtmp));
			close(f);
		}
		/*
		 * After a proper login force reset
		 * of error detection code in dfork.
		 */
		p->gettytime = 0;
		p->windtime = 0;
	}
}

/* One line straight to the console: no syslogd runs on a small root. */
void
dconsole(msg)
	const char *msg;
{
	register int f;

	/*
	 * ttyopen() in sys/kern/tty.c makes the opened tty the controlling
	 * terminal of a process whose p_pgrp is 0 and moves it into the tty's
	 * process group, and it ignores O_NOCTTY. init runs in group 0, so it
	 * opens the console from its own group 1 and returns to 0 at once;
	 * otherwise init would join the login shell's group, take console
	 * signals and pass the console to every child, and getty could no
	 * longer start a login on it. Gettys inherit group 0 from init, which
	 * lets each one claim its own line.
	 */
	setpgrp(0, 1);
	f = open(ctty, O_WRONLY);
	setpgrp(0, 0);
	if (f >= 0) {
		write(f, msg, strlen(msg));
		close(f);
	}
}

/*
 * The child's path from fork to execve. Every descriptor init holds is
 * closed and stdio reads and writes /dev/null. The command path resolves
 * inside the chroot, since execve(2) looks it up after chroot(2) and the
 * kernel offers no exec by descriptor. Groups go before gid and gid before
 * uid, because each call needs the root privilege the next one removes
 * (setgroups() in sys/kern/kern_prot.c, setgid() and setuid() in
 * kern_prot2.c); a failure, or a uid that can still become root, ends the
 * child before exec.
 */
void
dexec(d)
	register struct dtab *d;
{
	char buf[DCMDSIZ], msg[DCMDSIZ + 48], *argv[DNARGS], *envp[1];
	const char *step;
	register int fd, con, nfd;
	gid_t groups[1];

	signal(SIGTERM, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGALRM, SIG_DFL);
	signal(SIGTSTP, SIG_DFL);
	sigsetmask(0L);

	/*
	 * ttyopen() in sys/kern/tty.c gives a process whose p_pgrp is 0 the
	 * opened tty as its controlling terminal and joins its process group,
	 * so the child leaves init's group 0 before it opens the console;
	 * console interrupts and hangups then never reach the daemon.
	 */
	setpgrp(0, getpid());

	/* The console descriptor reports a failure and closes on exec. */
	con = open(ctty, O_WRONLY);
	if (con >= 0 && con < 3) {
		fd = fcntl(con, F_DUPFD, 3);
		close(con);
		con = fd;
	}
	if (con >= 0)
		fcntl(con, F_SETFD, FD_CLOEXEC);
	nfd = getdtablesize();
	for (fd = 0; fd < nfd; fd++)
		if (fd != con)
			close(fd);
	step = _PATH_DEVNULL;
	if (open(_PATH_DEVNULL, O_RDWR) != 0 || dup2(0, 1) != 1 ||
	    dup2(0, 2) != 2)
		goto fail;
	step = "chroot";
	if (d->root[0] != '\0' && chroot(d->root) < 0)
		goto fail;
	if (chdir("/") < 0)
		goto fail;
	step = "setgroups";
	groups[0] = d->gid;
	if (setgroups(1, groups) < 0)
		goto fail;
	step = "setgid";
	if (setgid(d->gid) < 0)
		goto fail;
	step = "setuid";
	if (setuid(d->uid) < 0)
		goto fail;
	step = "credential check";
	if (getuid() != d->uid || geteuid() != d->uid ||
	    getgid() != d->gid || getegid() != d->gid)
		goto fail;
	if (d->uid != 0 && setuid(0) == 0)
		goto fail;
	strcpy(buf, d->cmd);
	dtab_argv(buf, argv);
	envp[0] = 0;
	step = "exec";
	execve(argv[0], argv, envp);
fail:
	if (con >= 0) {
		snprintf(msg, sizeof(msg), "init: %s: %s: %s\n", d->cmd, step,
		    strerror(errno));
		write(con, msg, strlen(msg));
	}
	_exit(127);
}

/* Records an exit or a refused fork and reports it on the console. */
void
dfailed(d, status)
	register struct dtab *d;
	int status;
{
	char msg[DCMDSIZ + 64];
	int delay;

	delay = dtab_exited(d, time((time_t *)0));
	dtab_describe(msg, sizeof(msg), d, status, delay);
	dconsole(msg);
}

/* A refused fork counts as a failed run, so the entry backs off. */
void
dstart(d)
	register struct dtab *d;
{
	register int pid;

	d->started = time((time_t *)0);
	pid = fork();
	if (pid == 0)
		dexec(d);
	if (pid < 0) {
		dfailed(d, -1);
		return;
	}
	d->pid = pid;
}

/* Starts every entry whose deadline has passed and arms the next alarm. */
void
dschedule()
{
	register struct dtab *d;
	time_t now, soonest;

	if (didle)
		return;
	now = time((time_t *)0);
	soonest = 0;
	for (d = dtab; d; d = d->next) {
		if (d->pid != -1 || d->disabled)
			continue;
		if (d->next_try <= now)
			dstart(d);
		if (d->pid == -1 && (soonest == 0 || d->next_try < soonest))
			soonest = d->next_try;
	}
	alarm(soonest ? (unsigned)(soonest > now ? soonest - now : 1) : 0);
}

/*
 * The handler records the deadline and re-arms a one-second alarm, both
 * safe in signal context. An alarm that lands after multiple() tests dtimer
 * but before wait(2) blocks is then repeated within a second, and the
 * repeat interrupts that wait; dschedule() replaces the one-second alarm
 * with the next real deadline.
 */
void
dalarm(sig)
	int sig;
{
	dtimer = 1;
	alarm(1);
}

/*
 * sigvec() in lib/libc/compat/sigcompat.c flips SV_INTERRUPT in the
 * caller's structure, so each call fills a fresh one.
 */
void
dalarm_install()
{
	struct sigvec sv;

	sv.sv_handler = dalarm;
	sv.sv_mask = sigmask(SIGHUP);
	sv.sv_flags = SV_INTERRUPT;
	sigvec(SIGALRM, &sv, (struct sigvec *)0);
}

/* Runs the restarts a passed deadline recorded, with SIGHUP blocked. */
void
dtimer_run()
{
	long omask;

	if (!dtimer)
		return;
	omask = sigblock(sigmask(SIGHUP) | sigmask(SIGALRM));
	dtimer = 0;
	dschedule();
	sigsetmask(omask);
}

/* Matches a reaped pid against the table; returns nonzero on a match. */
int
dreap(pid, status)
	int pid, status;
{
	register struct dtab *d;

	for (d = dtab; d; d = d->next)
		if (d->pid == pid) {
			dfailed(d, status);
			dschedule();
			return (1);
		}
	return (0);
}

int
dpending()
{
	register struct dtab *d;

	for (d = dtab; d; d = d->next)
		if (d->pid == -1 && !d->disabled)
			return (1);
	return (0);
}

void
dfree()
{
	register struct dtab *d;

	alarm(0);
	dtimer = 0;
	/* reset() leaves idle() by longjmp, past its own clearing of didle. */
	didle = 0;
	while ((d = dtab) != 0) {
		dtab = d->next;
		free(d);
	}
}

/*
 * Rereads /etc/daemons. An entry whose line is unchanged keeps running and
 * a disabled one is enabled again; a removed or changed entry gets SIGTERM
 * and leaves the table, and a changed line enters as a new entry. A
 * malformed or overlong line is reported and skipped, so it never runs.
 */
void
dmerge()
{
	register struct dtab *d, *n, **link;
	struct dtab parsed;
	char line[DCMDSIZ + DROOTSIZ + 32], msg[64];
	FILE *f;
	int lineno, r;

	for (d = dtab; d; d = d->next)
		d->xflag = 0;
	f = fopen(DAEMONS_FILE, "r");
	for (lineno = 1; f && fgets(line, sizeof(line), f); lineno++) {
		if (strchr(line, '\n') == 0 && !feof(f)) {
			while (strchr(line, '\n') == 0 &&
			    fgets(line, sizeof(line), f))
				;
			r = -1;
		} else
			r = dtab_parse(line, &parsed);
		if (r == 0)
			continue;
		if (r < 0) {
			snprintf(msg, sizeof(msg), "init: %s:%d: bad entry\n",
			    DAEMONS_FILE, lineno);
			dconsole(msg);
			continue;
		}
		for (d = dtab; d; d = d->next)
			if (!d->xflag && dtab_same(d, &parsed))
				break;
		if (d) {
			d->xflag = 1;
			if (d->disabled) {
				d->disabled = 0;
				d->fails = 0;
				d->pid = -1;
				d->next_try = 0;
			}
			continue;
		}
		n = (struct dtab *)malloc(sizeof(*n));
		if (n == 0) {
			dconsole("init: no space for a daemon entry\n");
			continue;
		}
		*n = parsed;
		n->xflag = 1;
		n->pid = -1;
		for (link = &dtab; *link; link = &(*link)->next)
			;
		*link = n;
	}
	if (f)
		fclose(f);
	for (link = &dtab; (d = *link) != 0; ) {
		if (d->xflag) {
			link = &d->next;
			continue;
		}
		if (d->pid > 0)
			kill(d->pid, SIGTERM);
		*link = d->next;
		free(d);
	}
	dschedule();
}

void
idle (sig)
	int sig;
{
	register struct tab *p;
	register int pid;

	int status;
	long omask;

	signal (SIGHUP, idlehup);
	didle = 1;
	for (;;) {
		if (setjmp(idlebuf)) {
			/* The alarm interrupts the wait(2) multiple() resumes. */
			didle = 0;
			alarm(1);
			return;
		}
		pid = wait(&status);
		if (pid == -1) {
			/* An interrupted wait still has children to reap. */
			if (errno != EINTR)
				sigpause(0L);
			continue;
		}
		omask = sigblock(sigmask(SIGALRM));
		dreap(pid, status);
		sigsetmask(omask);
		for (p=itab; p; p=p->next) {
			/* if window system dies, mark it for restart */
			if (p->wpid == pid)
				p->wpid = -1;
			if (p->pid == pid) {
				rmut(p);
				p->pid = -1;
			}
		}
	}
}

void
term(p)
	register struct tab *p;
{

	/* A pid of -1 marks an entry to restart, not a process. */
	if (p->pid > 0) {
		rmut(p);
		kill(p->pid, SIGKILL);
	}
	p->pid = 0;
	/* send SIGHUP to get rid of connections */
	if (p->wpid > 0)
		kill(p->wpid, SIGHUP);
}

int
shutend()
{
	register int i, f;

	signal(SIGALRM, SIG_DFL);
	for (i = 0; i < 10; i++)
		close(i);
	f = open(wtmpf, O_WRONLY|O_APPEND);
	if (f >= 0) {
		SCPYN(wtmp.ut_line, "~");
		SCPYN(wtmp.ut_name, "shutdown");
		SCPYN(wtmp.ut_host, "");
		time(&wtmp.ut_time);
		write(f, (char *)&wtmp, sizeof(wtmp));
		close(f);
	}
	return (1);
}

void
shutreset (sig)
	int sig;
{
        const char shutfailm[] = "WARNING: Something is hung (wont die); ps axl advised\n";

	if (fork() == 0) {
		int ct = open(ctty, 1);
		write(ct, shutfailm, sizeof (shutfailm));
		sleep(1);
		exit(1);
	}
	sleep(1);
	shutend();
	longjmp(shutpass, 1);
}

static void
shutdown(void)
{
	register int i;
	register struct tab *p, *p1;

	close(creat(utmpf, 0644));
	signal(SIGHUP, SIG_IGN);
	for (p = itab; p ; ) {
		term(p);
		p1 = p->next;
		free(p);
		p = p1;
	}
	itab = (struct tab *)0;
	dfree();
	signal(SIGALRM, shutreset);
	(void) kill(-1, SIGTERM);	/* one chance to catch it */
	sleep(1);
	alarm(30);
	for (i = 0; i < 5; i++)
		kill(-1, SIGKILL);
	while (wait((int *)0) != -1)
		;
	alarm(0);
	shutend();
}

void
single()
{
	register int pid, xpid;
	int	fd;

	/*
	 * If the kernel is in secure mode, downgrade it to insecure mode.
	 */
	if (getsecuritylevel() > 0)
		setsecuritylevel(0);

	do {
		pid = spawn("single-user shell");
		if (pid < 0) {
			xpid = -1;
			continue;
		}
		if (pid == 0) {
			signal(SIGTERM, SIG_DFL);
			signal(SIGHUP, SIG_DFL);
			signal(SIGALRM, SIG_DFL);
			signal(SIGTSTP, SIG_IGN);
			fd = open(ctty, O_RDWR, 0);
			if (fd)
				dup2(fd, 0);
			dup2(0, 1);
			dup2(0, 2);
			execl(shell, minus, (char *)0);
			perror(shell);
			exit(0);
		}
		while ((xpid = wait((int *)0)) != pid)
			if (xpid == -1 && errno == ECHILD)
				break;
	} while (xpid == -1);
}

int
runcom(oldhowto)
	int oldhowto;
{
	register int pid, f, xpid;
	int status;

	pid = spawn(runc);
	if (pid < 0)
		return (0);
	if (pid == 0) {
		f = open("/", O_RDONLY, 0);
		if (f > 0)
			dup2(f, 0);
		dup2(0, 1);
		dup2(0, 2);
		if (oldhowto & RB_SINGLE)
			execl(shell, shell, runc, (char *)0);
		else
			execl(shell, shell, runc, "autoboot", (char *)0);
		exit(1);
	}
	status = 0;
	while ((xpid = wait(&status)) != pid)
		if (xpid == -1 && errno == ECHILD) {
			status = -1;
			break;
		}
	if (status) {
syslog(LOG_ERR, "%s failed: status = %#x", runc, status);
closelog();
sleep(1);
		return (0);
        }
	f = open(wtmpf, O_WRONLY|O_APPEND);
	if (f >= 0) {
		SCPYN(wtmp.ut_line, "~");
		SCPYN(wtmp.ut_name, "reboot");
		SCPYN(wtmp.ut_host, "");
		if (time0) {
			wtmp.ut_time = time0;
			time0 = 0;
		} else
			time(&wtmp.ut_time);
		write(f, (char *)&wtmp, sizeof(wtmp));
		close(f);
	}
	return (1);
}

#define NARGS	20	/* must be at least 4 */
#define ARGLEN	512	/* total size for all the argument strings */

void
execit(s, arg)
	char *s;
	char *arg;	/* last argument on line */
{
	char *argv[NARGS], args[ARGLEN], *envp[1];
	register char *sp = s;
	register char *ap = args;
	register char c;
	register int i;

	/*
	 * First we have to set up the argument vector.
	 * "prog arg1 arg2" maps to exec("prog", "-", "arg1", "arg2").
	 */
	for (i = 1; i < NARGS - 2; i++) {
		argv[i] = ap;
		for (;;) {
			if ((c = *sp++) == '\0' || ap >= &args[ARGLEN-1]) {
				*ap = '\0';
				goto done;
			}
			if (c == ' ') {
				*ap++ = '\0';
				while (*sp == ' ')
					sp++;
				if (*sp == '\0')
					goto done;
				break;
			}
			*ap++ = c;
		}
	}
done:
	argv[0] = argv[1];
	argv[1] = "-";
	argv[i+1] = arg;
	argv[i+2] = 0;
	envp[0] = 0;
	execve(argv[0], &argv[1], envp);
	/* report failure of exec */
	syslog(LOG_ERR, "%s: %m", argv[0]);
	closelog();
	sleep(10);	/* prevent failures from eating machine */
}

void
wstart(p)
	register struct tab *p;
{
	register int pid;
	time_t t;
	int dowait = 0;

	time(&t);
	p->windcnt++;
	if ((t - p->windtime) >= 60) {
		p->windtime = t;
		p->windcnt = 1;
	} else if (p->windcnt >= 5) {
		dowait = 1;
		p->windtime = t;
		p->windcnt = 1;
	}

	pid = spawn(p->wcmd);

	if (pid == 0) {
		signal(SIGTERM, SIG_DFL);
		signal(SIGHUP,  SIG_IGN);
		sigsetmask(0L);	/* since can be called from masked code */
		if (dowait) {
			syslog(LOG_ERR, "'%s %s' failing, sleeping", p->wcmd, p->line);
			closelog();
			sleep(30);
		}
		execit(p->wcmd, p->line);
		exit(0);
	}
	/* A refusal leaves -1, which multiple() restarts on its next pass. */
	p->wpid = pid;
}

void
dfork(p)
	struct tab *p;
{
	register int pid;
	time_t t;
	int dowait = 0;

	time(&t);
	p->gettycnt++;
	if ((t - p->gettytime) >= 60) {
		p->gettytime = t;
		p->gettycnt = 1;
	} else if (p->gettycnt >= 5) {
		dowait = 1;
		p->gettytime = t;
		p->gettycnt = 1;
	}
	pid = spawn(p->comn);
	if (pid == 0) {
		signal(SIGTERM, SIG_DFL);
		signal(SIGHUP, SIG_IGN);
		sigsetmask(0L);	/* since can be called from masked code */
		if (dowait) {
			syslog(LOG_ERR, "'%s %s' failing, sleeping", p->comn, p->line);
			closelog();
			sleep(30);
		}
		execit(p->comn, p->line);
		exit(0);
	}
	/* A refusal leaves -1, which multiple() restarts on its next pass. */
	p->pid = pid;
}

/*
 * Multi-user.  Listen for users leaving, SIGHUP's
 * which indicate ttys has changed, and SIGTERM's which
 * are used to shutdown the system.
 */
void
multiple()
{
	register struct tab *p;
	register int pid;
	long omask;
	int status;
	static struct	sigvec	mvec = { merge,
	    sigmask(SIGTERM) | sigmask(SIGALRM), 0 };

	/*
	 * If the administrator has not set the security level to -1
	 * to indicate that the kernel should not run multiuser in secure
	 * mode, and the run script has not set a higher level of security
	 * than level 1, then put the kernel into secure mode.
	 */
	if (getsecuritylevel() == 0)
		setsecuritylevel(1);

	sigvec(SIGHUP, &mvec, (struct sigvec *)0);
	for (;;) {
		dtimer_run();
		pid = wait(&status);
		if (pid == -1) {
			if (errno == EINTR)
				continue;
			/*
			 * With no child left, a pending restart waits for its
			 * alarm. merge() frees entries, so the table is read
			 * with SIGHUP and SIGALRM blocked, and sigpause()
			 * restores the mask and waits in one step, so an alarm
			 * after the test still ends the pause.
			 */
			omask = sigblock(sigmask(SIGHUP) | sigmask(SIGALRM));
			if (dtimer || dpending()) {
				if (!dtimer)
					sigpause(omask);
				sigsetmask(omask);
				continue;
			}
			sigsetmask(omask);
			return;
		}
		omask = sigblock(sigmask(SIGHUP) | sigmask(SIGALRM));
		if (dreap(pid, status)) {
			sigsetmask(omask);
			continue;
		}
		for (p=itab; p; p=p->next) {
			/* must restart window system BEFORE emulator */
			if (p->wpid == pid || p->wpid == -1)
				wstart(p);
			if (p->pid == pid || p->pid == -1) {
				/* disown the window system; -1 names no process */
				if (p->wpid > 0)
					kill(p->wpid, SIGHUP);
				rmut(p);
				dfork(p);
			}
		}
		sigsetmask(omask);
	}
}

int
main(argc, argv)
	int argc;
	char **argv;
{
#if 0
        /* Trivial init: just start shell. */
        int fd = open(ctty, O_RDWR, 0);
        if (fd < 0)
                return 0;
        write(fd, "init: starting /bin/sh\n", 23);

        if (fd > 0)
                dup2(fd, 0);
        dup2(0, 1);
        dup2(0, 2);
        execl(shell, minus, (char *)0);
        perror(shell);
        return 0;
#else
	int howto, oldhowto;

	time0 = time(0);
	if (argc > 1 && argv[1][0] == '-') {
		char *cp;

		howto = 0;
		cp = &argv[1][1];
		while (*cp) switch (*cp++) {
		case 'a':
			howto |= RB_ASKNAME;
			break;
		case 's':
			howto |= RB_SINGLE;
			break;
		}
	} else {
		howto = RB_SINGLE;
	}
	if (getuid() != 0)
		exit(1);
	if (getpid() != 1)
		exit(1);

	openlog("init", LOG_CONS|LOG_ODELAY, LOG_AUTH);

	signal(SIGSYS, badsys);
	sigvec(SIGTERM, &rvec, (struct sigvec *)0);
	signal(SIGTSTP, idle);
	signal(SIGSTOP, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	(void) setjmp(sjbuf);
	for (;;) {
		oldhowto = howto;
		howto = RB_SINGLE;
		if (setjmp(shutpass) == 0)
			shutdown();
		if (oldhowto & RB_SINGLE)
			single();
		if (runcom(oldhowto) == 0)
			continue;
		dalarm_install();
		merge(0);
		multiple();
	}
#endif
}

/*
 * Get the security level of the kernel.
 */
int
getsecuritylevel()
{
#ifdef KERN_SECURELVL
	int name[2], curlevel;
	size_t len;

	name[0] = CTL_KERN;
	name[1] = KERN_SECURELVL;
	len = sizeof curlevel;
	if (sysctl(name, 2, &curlevel, &len, NULL, 0) == -1) {
		syslog(LOG_EMERG, "cannot get kernel security level: %s",
		    strerror(errno));
		return (-1);
	}
	return (curlevel);
#else
	return (-1);
#endif
}

/*
 * Set the security level of the kernel.
 */
void
setsecuritylevel(newlevel)
	int newlevel;
{
#ifdef KERN_SECURELVL
	int name[2], curlevel;

	curlevel = getsecuritylevel();
	if (newlevel == curlevel)
		return;
	name[0] = CTL_KERN;
	name[1] = KERN_SECURELVL;
	if (sysctl(name, 2, NULL, NULL, &newlevel, sizeof newlevel) == -1) {
		syslog(LOG_EMERG,
		    "cannot change kernel security level from %d to %d: %s",
		    curlevel, newlevel, strerror(errno));
		return;
	}
	syslog(LOG_ALERT, "kernel security level changed from %d to %d",
	    curlevel, newlevel);
#endif
}

void
wterm(p)
	register struct tab *p;
{
	/* A wpid of -1 marks a window system to restart, not a process. */
	if (p->wpid > 0)
		kill(p->wpid, SIGKILL);
	p->wpid = 0;
}

/*
 * Merge current contents of ttys file
 * into in-core table of configured tty lines.
 * Entered as signal handler for SIGHUP.
 */
#define	FOUND	1
#define	CHANGE	2
#define WCHANGE 4

void merge(int signum)
{
	register struct tab *p;
	register struct ttyent *t;
	register struct tab *p1;

	for (p=itab; p; p=p->next)
		p->xflag = 0;
	setttyent();
	while ((t = getttyent()) != 0) {
		if ((t->ty_status & TTY_ON) == 0)
			continue;
		for (p=itab; p; p=p->next) {
			if (SCMPN(p->line, t->ty_name))
				continue;
			p->xflag |= FOUND;
			if (SCMPN(p->comn, t->ty_getty)) {
				p->xflag |= CHANGE;
				SCPYN(p->comn, t->ty_getty);
			}
			if (SCMPN(p->wcmd, t->ty_window ? t->ty_window : "")) {
				p->xflag |= WCHANGE | CHANGE;
				SCPYN(p->wcmd, t->ty_window);
			}
			goto contin1;
		}

		/*
		 * Make space for a new one
		 */
		p1 = (struct tab *)calloc(1, sizeof(*p1));
		if (!p1) {
			syslog(LOG_ERR, "no space for '%s' !?!", t->ty_name);
			goto contin1;
		}
		/*
		 * Put new terminal at the end of the linked list.
		 */
		if (itab) {
			for (p = itab; p->next ; p = p->next)
				;
			p->next = p1;
		} else
			itab = p1;

		p = p1;
		SCPYN(p->line, t->ty_name);
		p->xflag |= FOUND|CHANGE;
		SCPYN(p->comn, t->ty_getty);
		if (t->ty_window && strcmp(t->ty_window, "") != 0) {
			p->xflag |= WCHANGE;
			SCPYN(p->wcmd, t->ty_window);
		}
	contin1:
		;
	}
	endttyent();
	p1 = (struct tab *)0;
	for (p=itab; p; p=p->next) {
		if ((p->xflag & FOUND) == 0) {
			term(p);
			wterm(p);
			if (p1)
				p1->next = p->next;
			else
				itab = p->next;
			free(p);
			p = p1 ? p1 : itab;
		} else {
			/* window system should be started first */
			if (p->xflag & WCHANGE) {
				wterm(p);
				wstart(p);
			}
			if (p->xflag & CHANGE) {
				term(p);
				dfork(p);
			}
		}
		p1 = p;
	}
	dmerge();
}

void
reset()
{
	longjmp(sjbuf, 1);
}
