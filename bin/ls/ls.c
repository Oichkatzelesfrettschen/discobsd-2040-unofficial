/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * ls
 *
 * 4.2bsd version for symbolic links, variable length
 * directory entries, block size in the inode, etc.
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <stdio.h>
#include <stdlib.h>
#include <sgtty.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "stat_flags.h"

#define	kbytes(size)	(((size) + 1023) / 1024)

struct afile {
	char	ftype;		/* file type, e.g. 'd', 'c', 'f' */
	ino_t	fnum;		/* inode number of file */
	short	fmode;		/* mode&~S_IFMT, perhaps ISARG */
	u_short	fflags;		/* st_flags (uappnd, uchg, schg, ...) */
	short	fnl;		/* number of links */
	uid_t	fuid;		/* owner id */
	gid_t	fgid;		/* group id */
	off_t	fsize;		/* file size */
	long	fblks;		/* number of blocks used */
	time_t	fmtime;		/* time (modify or access or create) */
	char	*fname;		/* file name */
	char	*flinkto;	/* symbolic link value */
};

#define ISARG	0x8000		/* extra ``mode'' */

struct subdirs {
	char	*sd_name;
	struct	subdirs *sd_next;
} *subdirs;

char	aflg, dflg, gflg, lflg, sflg, tflg, uflg, iflg, fflg, cflg;
signed char rflg = 1;
char	oflg, qflg, Aflg, Cflg, Fflg, Lflg, Rflg, usetabs, colorflag;

time_t	now, sixmonthsago;

char	*dotp = ".";

struct	winsize win;
int	twidth;
int	errs;		/* an argument that stat cannot reach sets the exit status */

struct	afile *gstat(struct afile *, const char *, int, long *);
int	fcmp(const void *, const void *);
char	*cat(const char *, const char *);
char	*savestr(const char *);
char	*fmtentry(struct afile *, int);
static const char *getname(uid_t);
static const char *getgroup(gid_t);

long	getdir(const char *, struct afile **, struct afile **, int *);
void	formatf(struct afile *, struct afile *);
void	formatd(char *, int);

int
main(int argc, char *argv[])
{
	struct afile *fp0, *fplast;
	register struct afile *fp;
	struct sgttyb sgbuf;
	int ch, i;

	Aflg = !getuid();
	(void) time(&now); sixmonthsago = now - 6L*30L*24L*60L*60L; now += 60;
	twidth = 80;
	if (isatty(1)) {
		qflg = Cflg = colorflag = 1;
		ioctl(1, TIOCGETP, &sgbuf);
		if (ioctl(1, TIOCGWINSZ, &win) != -1)
			twidth = (win.ws_col == 0 ? 80 : win.ws_col);
		if ((sgbuf.sg_flags & XTABS) != XTABS)
			usetabs = 1;
	} else
		usetabs = 1;
	while ((ch = getopt(argc, argv, "1ACLFRacdfgiloqrstu")) != EOF)
		switch((char)ch) {
/*
 * The -1, -C, and -l options override each other so shell aliasing
 * works right.
*/
		case '1':
			lflg = 0;
			Cflg = 0; break;
		case 'C':
			lflg = 0;
			Cflg = 1; break;
		case 'l':
			Cflg = 0;
			lflg++; break;
		case 'A':
			Aflg++; break;
		case 'L':
			Lflg++; break;
		case 'F':
			Fflg++; break;
		case 'R':
			Rflg++; break;
		case 'a':
			aflg++; break;
		case 'c':
			uflg = 0;	/* -c overrides -u */
			cflg++; break;
		case 'd':
			Rflg = 0;	/* -d overrides -R */
			dflg++; break;
		case 'f':
			fflg++; break;
		case 'g':
			gflg++; break;
		case 'i':
			iflg++; break;
		case 'o':
			oflg++; break;
		case 'q':
			qflg = 1; break;
		case 'r':
			rflg = -1; break;
		case 's':
			sflg++; break;
		case 't':
			tflg++; break;
		case 'u':
			cflg = 0;	/* -u overrides -c */
			uflg++; break;
		case '?':
		default:
			fputs("usage: ls [ -1ACLFRacdfgiloqrstu ] [ file ]\n", stderr);
			exit(1);
	}
	if (!lflg)
		oflg = 0;
	if (fflg) {
		Aflg++;
		aflg++; lflg = 0; sflg = 0; tflg = 0;
	}
	if (lflg)
		Cflg = 0;
	argc -= optind;
	argv += optind;
	if (argc == 0) {
		argc++;
		argv = &dotp;
	}
	fp = (struct afile *)calloc((u_int)argc, sizeof (struct afile));
	if (fp == 0) {
		fputs("ls: out of memory\n", stderr);
		exit(1);
	}
	fp0 = fp;
	for (i = 0; i < argc; i++) {
		if (gstat(fp, *argv, 1, NULL)) {
			fp->fname = *argv;
			fp->fmode |= ISARG;
			fp++;
		}
		argv++;
	}
	fplast = fp;
	if (fflg == 0)
		qsort(fp0, fplast - fp0, sizeof (struct afile), fcmp);
	if (dflg) {
		formatf(fp0, fplast);
		exit(0);
	}

	if (fflg)
		fp = fp0;
	else {
		for (fp = fp0; fp < fplast && fp->ftype != 'd'; fp++)
			continue;
		formatf(fp0, fp);
	}

	if (fp < fplast) {
		if (fp > fp0)
			putchar('\n');
		for (;;) {
			formatd(fp->fname, argc > 1);
			while (subdirs) {
				struct subdirs *t;

				t = subdirs; subdirs = t->sd_next;
				putchar('\n');
				formatd(t->sd_name, 1);
				free(t->sd_name);
				free(t);
			}
			if (++fp == fplast)
				break;
			putchar('\n');
		}
	}
	exit(errs);
}

void
formatd(char *name, int dotitle)
{
	register struct afile *fp;
	register struct subdirs *dp;
	struct afile *dfp0, *dfplast;
	int isadir;
	long nkb;

	nkb = getdir(name, &dfp0, &dfplast, &isadir);
	if (dfp0 == 0)
		return;
	if (fflg == 0)
		qsort(dfp0, dfplast - dfp0, sizeof (struct afile), fcmp);
	if (dotitle)
		printf("%s%s\n", name, isadir ? ":" : "");
	if (lflg || sflg)
		printf("total %ld\n", nkb);
	formatf(dfp0, dfplast);
	if (Rflg)
		for (fp = dfplast - 1; fp >= dfp0; fp--) {
			if (fp->ftype != 'd' ||
			    !strcmp(fp->fname, ".") ||
			    !strcmp(fp->fname, ".."))
				continue;
			dp = (struct subdirs *)malloc(sizeof (struct subdirs));
			dp->sd_name = savestr(cat(name, fp->fname));
			dp->sd_next = subdirs; subdirs = dp;
		}
	for (fp = dfp0; fp < dfplast; fp++) {
		if ((fp->fmode&ISARG) == 0 && fp->fname)
			free(fp->fname);
		if (fp->flinkto)
			free(fp->flinkto);
	}
	free(dfp0);
}

long
getdir(const char *dir, struct afile **pfp0, struct afile **pfplast, int *isadir)
{
	register struct afile *fp;
	DIR *dirp;
	register struct direct *dp;
	struct	stat st;
	long nb;
	int nent = 20;

	dirp = opendir(dir);
	if (dirp == NULL) {
		*pfp0 = *pfplast = NULL;
		printf("%s unreadable\n", dir);		/* not stderr! */
		return (0);
	}
	fstat(dirfd(dirp), &st);
	if (S_ISDIR(st.st_mode))
		*isadir = 1;
	else
		*isadir = 0;
	fp = *pfp0 = (struct afile *)calloc(nent, sizeof (struct afile));
	*pfplast = *pfp0 + nent;
	nb = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_ino == 0)
			continue;
		if (aflg == 0 && dp->d_name[0]=='.' &&
		    (Aflg == 0 || dp->d_name[1]==0 ||
		     (dp->d_name[1]=='.' && dp->d_name[2]==0)))
			continue;
		if (gstat(fp, cat(dir, dp->d_name), Fflg+Rflg+colorflag, &nb) == 0)
			continue;
		fp->fnum = dp->d_ino;
		fp->fname = savestr(dp->d_name);
		fp++;
		if (fp == *pfplast) {
			*pfp0 = (struct afile *)realloc((char *)*pfp0,
			    2 * nent * sizeof (struct afile));
			if (*pfp0 == 0) {
				fputs("ls: out of memory\n", stderr);
				exit(1);
			}
			fp = *pfp0 + nent;
			*pfplast = fp + nent;
			nent *= 2;
		}
	}
	closedir(dirp);
	*pfplast = fp;
	return (kbytes (nb * DEV_BSIZE));
}

struct afile *
gstat(struct afile *fp, const char *file, int statarg, long *pnb)
{
	int (*statf)(const char *, struct stat *) = Lflg ? stat : lstat;
	char buf[BUFSIZ]; int cc;
	static struct afile azerofile;

	*fp = azerofile;
	fp->fmode = 0;
	fp->fnum = 0;
	fp->ftype = '-';
	if (statarg || sflg || lflg || tflg) {
		struct stat stb, stb1;

		if ((*statf)(file, &stb) < 0) {
			if (statf == lstat || lstat(file, &stb) < 0) {
				fprintf(stderr, "%s not found\n", file);
				errs = 1;
				return (0);
			}
		}
		fp->fblks = stb.st_blocks;
		fp->fsize = stb.st_size;
		switch (stb.st_mode & S_IFMT) {

		case S_IFDIR:
			fp->ftype = 'd'; break;
		case S_IFBLK:
			fp->ftype = 'b'; fp->fsize = stb.st_rdev; break;
		case S_IFCHR:
			fp->ftype = 'c'; fp->fsize = stb.st_rdev; break;
		case S_IFSOCK:
			fp->ftype = 's'; fp->fsize = 0; break;
		case S_IFLNK:
			fp->ftype = 'l';
			if (lflg) {
				cc = readlink(file, buf, BUFSIZ);
				if (cc >= 0) {
					buf[cc] = 0;
					fp->flinkto = savestr(buf);
				}
				break;
			}
			if (stat(file, &stb1) < 0)
				break;
			if ((stb1.st_mode & S_IFMT) == S_IFDIR) {
				stb = stb1;
				fp->ftype = 'd';
				fp->fsize = stb.st_size;
				fp->fblks = stb.st_blocks;
			}
			break;
		}
		fp->fnum = stb.st_ino;
		fp->fmode = stb.st_mode & ~S_IFMT;
		fp->fflags = stb.st_flags;
		fp->fnl = stb.st_nlink;
		fp->fuid = stb.st_uid;
		fp->fgid = stb.st_gid;
		if (uflg)
			fp->fmtime = stb.st_atime;
		else if (cflg)
			fp->fmtime = stb.st_ctime;
		else
			fp->fmtime = stb.st_mtime;
		if (pnb)
			*pnb += stb.st_blocks;
	}
	return (fp);
}

/*
 * An SGR color for a file's type, or 0 for none. Emitted around the
 * formatted entry at print time, never folded into the width-measured
 * string, so column alignment counts only the visible characters.
 */
char *
ls_color(struct afile *fp)
{
	if (!colorflag)
		return (0);
	switch (fp->ftype) {
	case 'd': return ("\033[1;34m");	/* directory: bold blue */
	case 'l': return ("\033[36m");	/* symlink: cyan */
	case 'b':
	case 'c': return ("\033[33m");	/* device: yellow */
	case 's': return ("\033[35m");	/* socket: magenta */
	}
	if (fp->fmode & 0111)
		return ("\033[1;32m");		/* executable: bold green */
	return (0);
}

void
formatf(struct afile *fp0, struct afile *fplast)
{
	register struct afile *fp;
	register int i, j, w;
	int width = 0, nentry = fplast - fp0;
	int columns, lines, maxflags;
	char *cp;

	if (fp0 == fplast)
		return;
	maxflags = 0;
	if (oflg) {
		for (fp = fp0; fp < fplast; fp++)
		    {
		    i = strlen (flags_to_string (fp->fflags, "-"));
		    if (i > maxflags)
			maxflags = i;
		    }
	}
	if (lflg || Cflg == 0)
		columns = 1;
	else {
		for (fp = fp0; fp < fplast; fp++) {
			int len = strlen(fmtentry(fp, maxflags));

			if (len > width)
				width = len;
		}
		if (usetabs)
			width = (width + 8) &~ 7;
		else
			width += 2;
		columns = twidth / width;
		if (columns == 0)
			columns = 1;
	}
	lines = (nentry + columns - 1) / columns;
	for (i = 0; i < lines; i++) {
		for (j = 0; j < columns; j++) {
			fp = fp0 + j * lines + i;
			char *col;

			cp = fmtentry(fp, maxflags);
			col = ls_color(fp);
			if (col)
				fputs(col, stdout);
			fputs(cp, stdout);
			if (col)
				fputs("\033[0m", stdout);
			if (fp + lines >= fplast) {
				putchar('\n');
				break;
			}
			w = strlen(cp);
			while (w < width)
				if (usetabs) {
					w = (w + 8) &~ 7;
					putchar('\t');
				} else {
					w++;
					putchar(' ');
				}
		}
	}
}

int
fcmp(const void *left, const void *right)
{
	const struct afile *f1 = left, *f2 = right;

	if (dflg == 0 && fflg == 0) {
		if ((f1->fmode&ISARG) && f1->ftype == 'd') {
			if ((f2->fmode&ISARG) == 0 || f2->ftype != 'd')
				return (1);
		} else {
			if ((f2->fmode&ISARG) && f2->ftype == 'd')
				return (-1);
		}
	}
	if (tflg) {
		if (f2->fmtime == f1->fmtime)
			return (0);
		if (f2->fmtime > f1->fmtime)
			return (rflg);
		return (-rflg);
	}
	return (rflg * strcmp(f1->fname, f2->fname));
}

char *
cat(const char *dir, const char *file)
{
	static char dfile[BUFSIZ];
	register int dlen;

	if ((dlen = strlen(dir))+1+strlen(file)+1 > BUFSIZ) {
		fputs("ls: filename too long\n", stderr);
		exit(1);
	}
	if (!dir[0] || (dir[0] == '.' && !dir[1]))
		return(strcpy(dfile, file));
	(void) strcpy(dfile, dir);
	if (dir[dlen - 1] != '/' && *file != '/')
		dfile[dlen++] = '/';
	(void) strcpy(dfile + dlen, file);
	return (dfile);
}

char *
savestr(const char *str)
{
	register char *cp = strdup(str);

	if (cp == NULL) {
		fputs("ls: out of memory\n", stderr);
		exit(1);
	}
	return(cp);
}

static char *fmtlstuff(char *, struct afile *, int);

static char *
fmtmode(char *out, unsigned mode)
{
	unsigned read = S_IRUSR;
	unsigned special = S_ISUID;

	for (; read != 0; read >>= 3, special >>= 1) {
		int executable = (mode & (read >> 2)) != 0;
		*out++ = mode & read ? 'r' : '-';
		*out++ = mode & (read >> 1) ? 'w' : '-';
		if (mode & special)
			*out++ = special == S_ISVTX ? (executable ? 't' : 'T') :
			    (executable ? 's' : 'S');
		else
			*out++ = executable ? 'x' : '-';
	}
	return out;
}

char *
fmtentry(struct afile *fp, int maxflags)
{
	static char fmtres[BUFSIZ];
	register char *cp, *dp;

	dp = fmtres;
	if (iflg)
		dp += sprintf(dp, "%6u ", fp->fnum);
	if (sflg)
		dp += sprintf(dp, "%4ld ", kbytes(fp->fblks * DEV_BSIZE));
	if (lflg)
		dp = fmtlstuff(dp, fp, maxflags);
	for (cp = fp->fname; *cp; cp++)
		if (qflg && (*cp < ' ' || *cp >= 0177))
			*dp++ = '?';
		else
			*dp++ = *cp;
	if (Fflg) {
		if (fp->ftype == 'd')
			*dp++ = '/';
		else if (fp->ftype == 'l')
			*dp++ = '@';
		else if (fp->ftype == 's')
			*dp++ = '=';
		else if (fp->fmode & 0111)
			*dp++ = '*';
	}
	if (lflg && fp->flinkto) {
		(void) strcpy(dp, " -> "); dp += 4;
		for (cp = fp->flinkto; *cp; cp++)
			if (qflg && (*cp < ' ' || *cp >= 0177))
				*dp++ = '?';
			else
				*dp++ = *cp;
	}
	*dp++ = 0;
	return (fmtres);
}

static char *
fmtlstuff(char *lp, struct afile *p, int maxflags)
{
	char gname[32], uname[32], fsize[32], ftime[32], fflags[64];

	/* type mode uname gname fsize ftime */
/* get uname */
	{ const char *cp = getname(p->fuid);
	  if (cp)
		(void) sprintf(uname, "%-9.9s", cp);
	  else
		(void) sprintf(uname, "%-9u", p->fuid);
	}
/* get gname */
	if (gflg) {
	  const char *cp = getgroup(p->fgid);
	  if (cp)
		(void) sprintf(gname, "%-9.9s", cp);
	  else
		(void) sprintf(gname, "%-9u", p->fgid);
	}
/* get flags */
	if (oflg)
		(void) sprintf(fflags, "%-*s ", maxflags,
				flags_to_string(p->fflags, "-"));
/* get fsize */
	if (p->ftype == 'b' || p->ftype == 'c')
		(void) sprintf(fsize, "%3d,%4d",
		    major(p->fsize), minor(p->fsize));
	else if (p->ftype == 's')
		(void) sprintf(fsize, "%8ld", 0L);
	else
		(void) sprintf(fsize, "%8ld", p->fsize);
/* get ftime */
	{ char *cp = ctime(&p->fmtime);
	  if ((p->fmtime < sixmonthsago) || (p->fmtime > now))
		(void) sprintf(ftime, " %-7.7s %-4.4s ", cp+4, cp+20);
	  else
		(void) sprintf(ftime, " %-12.12s ", cp+4);
	}
/* splat */
	*lp++ = p->ftype;
	lp = fmtmode(lp, p->fmode);
	return lp + sprintf(lp, "%3d %s%s%s%s%s",
	    p->fnl, uname, gflg ? gname : "", oflg ? fflags : "", fsize, ftime);
}

/* rest should be done with nameserver or database */

#include <pwd.h>
#include <grp.h>

/* fmtlstuff displays at most nine bytes of each account name. */
#define ACCOUNT_NAME_BYTES 9

#define NCACHE	64		/* power of 2 */
#define CAMASK	(NCACHE - 1)

static const char *
getname(uid_t uid)
{
	static struct ncache {
		uid_t	uid;
		char	name[ACCOUNT_NAME_BYTES + 1];
	} c_uid[NCACHE];
	register struct passwd *pw;
	register struct ncache *cp;

	setpassent(1);
	cp = c_uid + (uid & CAMASK);
	if (cp->uid == uid && *cp->name)
		return(cp->name);
	if (!(pw = getpwuid(uid)))
		return((char *)0);
	cp->uid = uid;
	strncpy(cp->name, pw->pw_name, ACCOUNT_NAME_BYTES);
	return(cp->name);
}

static const char *
getgroup(gid_t gid)
{
	static struct ncache {
		gid_t	gid;
		char	name[ACCOUNT_NAME_BYTES + 1];
	} c_gid[NCACHE];
	register struct group *gr;
	register struct ncache *cp;

	cp = c_gid + (gid & CAMASK);
	if (cp->gid == gid && *cp->name)
		return(cp->name);
	if (!(gr = getgrgid(gid)))
		return((char *)0);
	cp->gid = gid;
	strncpy(cp->name, gr->gr_name, ACCOUNT_NAME_BYTES);
	return(cp->name);
}
