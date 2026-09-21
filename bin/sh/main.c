/*
 * UNIX shell
 *
 * Bell Telephone Laboratories
 */
#include "defs.h"
#include "sym.h"
#include "timeout.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "dup.h"
#include <sys/fcntl.h>
#include <sys/param.h>
#include <fcntl.h>
#include <unistd.h>
#include "edit.h"
#include <string.h>

#include <pwd.h>

/*
 * promptexpand: PS1 with "\w" replaced by the working directory (the
 * home directory shown as "~"), "\u" by the effective user's name, "\h"
 * by the hostname to its first dot and "\$" by "#" for root and "$"
 * otherwise -- bash's letters -- so a prompt inherited across su still
 * tells the truth. The rest of PS1 passes through, escapes
 * included, so a colored prompt is PS1's own business.
 */
static char promptbuf[256];
static char promptuser[32];
static char prompthost[32];

static char *
promptexpand(ps1)
	char *ps1;
{
	char cwd[MAXPATHLEN];
	char *home = homenod.namval;
	char *o = promptbuf, *end = promptbuf + sizeof(promptbuf) - 1;
	char *p, *w;

	if (ps1 == NULL || strchr(ps1, '\\') == NULL)
		return (ps1);
	for (p = ps1; *p && o < end; p++) {
		if (p[0] == '\\' && p[1] == '$') {
			*o++ = geteuid() ? '$' : '#';
			p++;
			continue;
		}
		if (p[0] == '\\' && p[1] == 'h') {
			p++;
			if (prompthost[0] == 0) {
				if (gethostname(prompthost, sizeof(prompthost) - 1) < 0)
					strcpy(prompthost, "?");
				prompthost[sizeof(prompthost) - 1] = 0;
				if ((w = strchr(prompthost, '.')) != NULL)
					*w = 0;
			}
			for (w = prompthost; *w && o < end; )
				*o++ = *w++;
			continue;
		}
		if (p[0] == '\\' && p[1] == 'u') {
			struct passwd *pw;

			p++;
			if (promptuser[0] == 0) {
				pw = getpwuid(geteuid());
				strncpy(promptuser, pw ? pw->pw_name : "?",
				    sizeof(promptuser) - 1);
			}
			for (w = promptuser; *w && o < end; )
				*o++ = *w++;
			continue;
		}
		if (p[0] != '\\' || p[1] != 'w') {
			*o++ = *p;
			continue;
		}
		p++;
		if (getwd(cwd) == NULL)
			strcpy(cwd, "?");
		w = cwd;
		if (home && *home && strcmp(home, "/") != 0) {
			int n = strlen(home);

			if (strncmp(cwd, home, n) == 0 && (cwd[n] == '/' || cwd[n] == 0)) {
				*o++ = '~';
				w = cwd + n;
			}
		}
		while (*w && o < end)
			*o++ = *w++;
	}
	*o = 0;
	return (promptbuf);
}

#ifdef RES
#include <sgtty.h>

#endif

static BOOL     beenhere = FALSE;
char            tmpout[20] = "/tmp/sh-";
struct fileblk  stdfile;
struct fileblk *standin = &stdfile;
int mailchk = 0;

static char     *mailp;
static long     *mod_time = (long *)NIL;


extern char     *simple();

static void      exfile(BOOL);
void             chkpr();
void             settmp();
void             Ldup(int, int);
void             chkmail();
void             setmail(char *);

static void
exfile(prof)
BOOL    prof;
{
	long    mailtime = 0;   /* Must not be a register variable */
	long    curtime = 0;
	register int    userid;

	/*
	 * move input
	 */
	if (input > 0)
	{
		Ldup(input, INIO);
		input = INIO;
	}

	userid = geteuid();
	if( userid ) defpath[ENDPATH] = '\0';   /* no /etc */

	/*
	 * decide whether interactive
	 */
	if ((flags & intflg) ||
	    ((flags&oneflg) == 0 &&
	    isatty(output) &&
	    isatty(input)) )

	{
		dfault(&ps1nod, (userid ? stdprompt : supprompt));
		dfault(&ps2nod, readmsg);
		flags |= ttyflg | prompt;
		ignsig(SIGTERM);
		if (mailpnod.namflg != N_DEFAULT)
			setmail(mailpnod.namval);
		else
			setmail(mailnod.namval);
	}
	else
	{
		flags |= prof;
		flags &= ~prompt;
	}

	if (setjmp(errshell) && prof)
	{
		close(input);
		return;
	}
	/*
	 * error return here
	 */

	loopcnt = peekc = peekn = 0;
	fndef = 0;
	nohash = 0;
	iopend = 0;

	if (input >= 0)
		initf(input);
	/*
	 * command loop
	 */
	for (;;)
	{
		tdystak(NIL);
		stakchk();      /* may reduce sbrk */
		exitset();

		if ((flags & prompt) && standin->fstak == NIL && !eof)
		{

			if (mailp)
			{
				time(&curtime);

				if ((curtime - mailtime) >= mailchk)
				{
					chkmail();
					mailtime = curtime;
				}
			}

			/*
			 * Interactive line editing only ever engages here,
			 * for the top-level command line on a real tty in
			 * both directions; a script, a pipe, or `sh -i`
			 * with redirected input falls straight through to
			 * the original prprompt()+readc() path below,
			 * unchanged.
			 */
			if (isatty(input) && isatty(output))
			{
				char    *path = pathnod.namval ? pathnod.namval : defpath;
				int     n = editline(input, output, promptexpand(ps1nod.namval), path,
					    standin->fbuf, sizeof(standin->fbuf));

				if (n < 0)
					eof++;
				else
				{
					standin->fbuf[n] = NL;
					standin->fnxt = standin->fbuf;
					standin->fend = standin->fbuf + n + 1;
				}
			}
			else
				prprompt(promptexpand(ps1nod.namval));

#ifdef TIME_OUT
			alarm(TIMEOUT);
#endif

			flags |= waiting;
		}

		trapnote = 0;
		peekc = cii(readc());   /* @@@ */
		if (eof)
			return;

#ifdef TIME_OUT
		alarm(0);
#endif

		flags &= ~waiting;

		execute(cmd(NL, MTFLG), 0, eflag);
		eof |= (flags & oneflg);
	}
}

int
main(c, v, e)
int     c;
char    **v;
char    **e;
{
	register int    rflag = ttyflg;
	int             rsflag = 1;     /* local restricted flag */
	struct namnod   *n;

	stdsigs();

	/*
	 * initialise storage allocation
	 */

	stakbot = NIL;
	addblok((unsigned)0);

	/*
	 * set names from userenv
	 */

	setup_env();

	/*
	 * 'rsflag' is non-zero if SHELL variable is
	 *  set in environment and contains an'r' in
	 *  the simple file part of the value.
	 */
	if (n = findnam("SHELL"))
	{
		if (any('r', simple(n->namval)))
			rsflag = 0;
	}

	/*
	 * a shell is also restricted if argv(0) has
	 * an 'r' in its simple name
	 */

#ifndef RES
	if (c > 0 && any('r', simple(*v)))
		rflag = 0;
#endif

	hcreate();
	set_dotpath();

	/*
	 * look for options
	 * dolc is $#
	 */
	dolc = options(c, v);

	if (dolc < 2)
	{
		flags |= stdflg;
		{
			register char *flagc = flagadr;

			while (*flagc)
				flagc++;
			*flagc++ = STDFLG;
			*flagc = 0;
		}
	}
	if ((flags & stdflg) == 0)
		dolc--;
	dolv = v + c - dolc;
	dolc--;

	/*
	 * return here for shell file execution
	 * but not for parenthesis subshells
	 */
	setjmp(subshell);

	/*
	 * number of positional parameters
	 */
	replace(&cmdadr, dolv[0]);      /* cmdadr is $0 */

	/*
	 * set pidname '$$'
	 */
	assnum(&pidadr, getpid());

	/*
	 * set up temp file names
	 */
	settmp();

	/*
	 * default internal field separators - $IFS
	 */
	dfault(&ifsnod, sptbnl);

	dfault(&mchknod, MAILCHECK);
	mailchk = stoi(mchknod.namval);

	if ((beenhere++) == FALSE)      /* ? profile */
	{
		if (*(simple(cmdadr)) == '-')
		{                       /* system profile */

#ifndef RES
			if ((input = pathopen(nullstr, sysprofile)) >= 0)
				exfile(rflag);          /* file exists */
#endif

			if ((input = pathopen(nullstr, profile)) >= 0)
			{
				exfile(rflag);
				flags &= ~ttyflg;
			}
		}
		if (rsflag == 0 || rflag == 0)
			flags |= rshflg;
		/*
		 * open input file if specified
		 */
		if (comdiv)
		{
			estabf(comdiv);
			input = -1;
		}
		else
		{
			input = ((flags & stdflg) ? 0 : chkopen(cmdadr));

#ifdef ACCOUNT
			if (input != 0)
				preacct(cmdadr);
#endif
			comdiv--;
		}
	}
	exfile(0);
	done();
}

void
chkpr()
{
	if ((flags & prompt) && standin->fstak == NIL)
		prs(ps2nod.namval);
}

void
settmp()
{
	itos(getpid());
	serial = 0;
	tmpnam = movstr(numbuf, &tmpout[TMPNAM]);
}

void
Ldup(fa, fb)
register int    fa, fb;
{
#ifdef RES

	dup(fa | DUPFLG, fb);
	close(fa);
	ioctl(fb, FIOCLEX, 0);
#else
	if (fa >= 0)
		{ close(fb);
		  fcntl(fa,F_DUPFD,fb);               /* normal dup */
		  close(fa);
		  fcntl(fb, F_SETFD, EXCLOSE);        /* autoclose for fb */
		}
#endif
}

void
chkmail()
{
	register char   *s = mailp;
	register char   *save;

	long    *ptr = mod_time;
	char    *start;
	BOOL    flg;
	struct stat     statb;

	while (*s)
	{
		start = s;
		save = NIL;
		flg = 0;

		while (*s)
		{
			if (*s != COLON)
			{
				if (*s == '%' && save == NIL)
					save = s;

				s++;
			}
			else
			{
				flg = 1;
				*s = 0;
			}
		}

		if (save)
			*save = 0;

		if (*start && stat(start, &statb) >= 0)
		{
			if(statb.st_size && *ptr
				&& statb.st_mtime != *ptr)
			{
				if (save)
				{
					prs(save+1);
					newline();
				}
				else
					prs(mailmsg);
			}
			*ptr = statb.st_mtime;
		}
		else if (*ptr == 0)
			*ptr = 1;

		if (save)
			*save = '%';

		if (flg)
			*s++ = COLON;

		ptr++;
	}
}

void
setmail(mailpath)
	char *mailpath;
{
	register char   *s = mailpath;
	register int    cnt = 1;

	long    *ptr;

	free(mod_time);
	if (mailp = mailpath)
	{
		while (*s)
		{
			if (*s == COLON)
				cnt += 1;

			s++;
		}

		ptr = mod_time = (long *)alloc(sizeof(long) * cnt);

		while (cnt)
		{
			*ptr = 0;
			ptr++;
			cnt--;
		}
	}
}
