/*
 *  1999/8/11 - Remove reference to SDETACH.  It was removed from the kernel
 *          (finally) because it was not needed.
 *
 *  1997/12/16 - Fix coredump when processing -U.
 *
 *  1996/11/16 - Move 'psdatabase' in /var/run.
 *
 *  12/20/94 - Missing casts caused errors in reporting on swapped
 *         processes - sms
 *  1/7/93 - Heavily revised when the symbol table format changed - sms
 *
 *  ps - process status
 *  Usage:  ps [ acgklnrtuwxU# ] [ corefile [ swapfile ] ]
 */

/*
 * ps -- report process status.
 *
 * Every row comes from the kernel's process records, KERN_PROC with
 * KERN_PROC_ALL (sys/sysctl.h). The kernel fills a record wherever the
 * process image lives -- in core, on the swap device or in the SwapRAM
 * pool -- so ps opens no memory device and needs no privilege. A record
 * whose user-area groups the kernel could not fill keeps its row, and each
 * field from a missing group prints as "-".
 *
 * Two grammars choose the rows and columns. An argument list that begins
 * with "-" is POSIX ps (SUSv4 and SUSv5, XSI): -aAdefl, -g, -G, -n, -o,
 * -p, -t, -u, -U and -w. Otherwise the first argument is a 2.11BSD option
 * string: a, c, g, l, n, u, w and x, t followed by a terminal, and a
 * process id. ps(1) lists the columns and the departures from POSIX.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <machine/cpu.h>

enum column {
	C_F, C_S, C_UID, C_USER, C_RUSER, C_GROUP, C_RGROUP, C_PID, C_PPID,
	C_PGID, C_C, C_PRI, C_NICE, C_ADDR, C_SZ, C_VSZ, C_WCHAN, C_STIME,
	C_TTY, C_TT, C_TIME, C_CPUTIME, C_ETIME, C_PCPU, C_COMM, C_ARGS,
	C_COUNT
};

/*
 * The format names, default headers and minimum widths. The POSIX names
 * come first in the -o table of SUSv5; the rest are this system's, and
 * ps(1) documents them. A text column is left-justified.
 */
static const struct coldef {
	char	name[8];
	char	header[8];
	unsigned char width;
	unsigned char text;
} coldefs[C_COUNT] = {
	[C_F] =		{ "f",		"F",		3,	0 },
	[C_S] =		{ "s",		"S",		1,	1 },
	[C_UID] =	{ "uid",	"UID",		5,	0 },
	[C_USER] =	{ "user",	"USER",		8,	1 },
	[C_RUSER] =	{ "ruser",	"RUSER",	8,	1 },
	[C_GROUP] =	{ "group",	"GROUP",	8,	1 },
	[C_RGROUP] =	{ "rgroup",	"RGROUP",	8,	1 },
	[C_PID] =	{ "pid",	"PID",		5,	0 },
	[C_PPID] =	{ "ppid",	"PPID",		5,	0 },
	[C_PGID] =	{ "pgid",	"PGID",		5,	0 },
	[C_C] =		{ "c",		"C",		3,	0 },
	[C_PRI] =	{ "pri",	"PRI",		3,	0 },
	[C_NICE] =	{ "nice",	"NI",		3,	0 },
	[C_ADDR] =	{ "addr",	"ADDR",		4,	0 },
	[C_SZ] =	{ "sz",		"SZ",		4,	0 },
	[C_VSZ] =	{ "vsz",	"VSZ",		5,	0 },
	[C_WCHAN] =	{ "wchan",	"WCHAN",	6,	1 },
	[C_STIME] =	{ "stime",	"STIME",	5,	1 },
	[C_TTY] =	{ "tty",	"TT",		7,	1 },
	[C_TT] =	{ "tt",		"TT",		3,	1 },
	[C_TIME] =	{ "time",	"TIME",		8,	0 },
	[C_CPUTIME] =	{ "cputime",	"TIME",		6,	0 },
	[C_ETIME] =	{ "etime",	"ELAPSED",	11,	0 },
	[C_PCPU] =	{ "pcpu",	"%CPU",		4,	0 },
	[C_COMM] =	{ "comm",	"COMMAND",	0,	1 },
	[C_ARGS] =	{ "args",	"COMMAND",	0,	1 },
};

#define NITEMS(a)	(sizeof(a) / sizeof((a)[0]))
#define MAXFMT		32	/* columns in one listing */
#define MAXLIST		16	/* entries in one selection list */
#define CELLSZ		(KI_COMMSZ + 2)

static struct fmtcol {
	unsigned char	col;
	const char	*header;
} fmt[MAXFMT];
static int nfmt;

/* A selection list: process ids, user or group ids, or terminal devices. */
struct idlist {
	int	n;
	long	id[MAXLIST];
};

static struct {
	char	posix;		/* the POSIX grammar is in use */
	char	any;		/* a POSIX selection option was given */
	char	all;		/* -A, -e, or BSD a */
	char	ttyonly;	/* -a */
	char	noleaders;	/* -d */
	char	noterm;		/* BSD x: also processes without a terminal */
	char	groups;		/* BSD g */
	char	comm;		/* BSD c: the command name, not the arguments */
	struct idlist pids, pgids, rgids, euids, ruids, ttys;
} sel;

static dev_t	my_tty = NODEV;
static uid_t	my_uid, my_euid;
static long	hz = 60;
static time_t	now;
static unsigned	width;		/* output line width, 0 for no limit */
static dev_t	consdev = NODEV;

static void
usage(void)
{
	fputs("usage: ps [-aAdefl] [-g grouplist] [-G grouplist] "
	    "[-n namelist] [-o format]... [-p proclist] [-t termlist] "
	    "[-u userlist] [-U userlist] [-w]\n"
	    "       ps [acglnuwx] [t tty] [pid]\n", stderr);
	exit(1);
}

static void
add_col(enum column col, const char *header)
{
	if (nfmt == MAXFMT) {
		fputs("ps: too many columns\n", stderr);
		exit(1);
	}
	fmt[nfmt].col = (unsigned char)col;
	fmt[nfmt].header = header != NULL ? header : coldefs[col].header;
	nfmt++;
}

static int
add_id(struct idlist *list, long id)
{
	if (list->n == MAXLIST) {
		fputs("ps: selection list too long\n", stderr);
		exit(1);
	}
	list->id[list->n++] = id;
	return (1);
}

static int
in_list(const struct idlist *list, long id)
{
	for (int i = 0; i < list->n; i++)
		if (list->id[i] == id)
			return (1);
	return (0);
}

/*
 * A terminal named by its file name, its name after "tty", or "co" for
 * the console, as the XSI -t list and the BSD t option allow.
 */
static dev_t
tty_dev(const char *name)
{
	char path[MAXNAMLEN + 10];
	struct stat st;

	if (strcmp(name, "co") == 0)
		name = "console";
	if (strcmp(name, "console") == 0 && consdev != NODEV)
		return (consdev);
	snprintf(path, sizeof(path), "/dev/%s", name);
	if (stat(path, &st) == 0 && S_ISCHR(st.st_mode))
		return (st.st_rdev);
	snprintf(path, sizeof(path), "/dev/tty%s", name);
	if (stat(path, &st) == 0 && S_ISCHR(st.st_mode))
		return (st.st_rdev);
	fprintf(stderr, "ps: %s: not a terminal\n", name);
	exit(1);
}

/* A decimal id, or -1 when text holds anything but digits. */
static long
digits(const char *text)
{
	long value = 0;

	if (*text == '\0')
		return (-1);
	for (; *text != '\0'; text++) {
		if (!isdigit((unsigned char)*text) || value > 99999999)
			return (-1);
		value = value * 10 + (*text - '0');
	}
	return (value);
}

static long
user_id(const char *name)
{
	struct passwd *pw;
	long id = digits(name);

	if (id >= 0)
		return (id);
	if ((pw = getpwnam(name)) == NULL) {
		fprintf(stderr, "ps: %s: no such user\n", name);
		exit(1);
	}
	return ((long)pw->pw_uid);
}

static long
number(const char *text)
{
	long value = digits(text);

	if (value < 0) {
		fprintf(stderr, "ps: %s: not a number\n", text);
		exit(1);
	}
	return (value);
}

/* Parse a <blank>- or <comma>-separated list into list. */
static void
parse_list(char *arg, struct idlist *list, long (*convert)(const char *))
{
	for (char *item = strtok(arg, " ,\t"); item != NULL;
	    item = strtok(NULL, " ,\t"))
		add_id(list, convert(item));
	sel.any = 1;
}

static long
tty_id(const char *name)
{
	return ((long)tty_dev(name));
}

/*
 * One -o argument: names separated by blanks or commas. A name followed
 * by "=" takes the rest of the argument as its header.
 */
static void
parse_format(char *arg)
{
	while (*arg != '\0') {
		char *name = arg, *header = NULL;
		size_t len = strcspn(arg, " ,\t=");
		int col;

		arg += len;
		if (*arg == '=') {
			header = arg + 1;
			*arg = '\0';
			arg = header + strlen(header);
		} else if (*arg != '\0')
			*arg++ = '\0';
		if (len == 0)
			continue;
		for (col = 0; col < C_COUNT; col++)
			if (strcmp(name, coldefs[col].name) == 0)
				break;
		if (col == C_COUNT) {
			fprintf(stderr, "ps: %s: unknown format name\n", name);
			exit(1);
		}
		add_col((enum column)col, header);
	}
}

static void
posix_options(int argc, char **argv)
{
	int ch, full = 0, lng = 0, user_format = 0, wide = 0;

	while ((ch = getopt(argc, argv, "aAdefg:G:ln:o:p:t:u:U:w")) != -1) {
		switch (ch) {
		case 'a':
			sel.ttyonly = sel.any = 1;
			break;
		case 'A':
		case 'e':
			sel.all = sel.any = 1;
			break;
		case 'd':
			sel.noleaders = sel.any = 1;
			break;
		case 'f':
			full = 1;
			break;
		case 'g':
			parse_list(optarg, &sel.pgids, number);
			break;
		case 'G':
			parse_list(optarg, &sel.rgids, number);
			break;
		case 'l':
			lng = 1;
			break;
		case 'n':
			break;
		case 'o':
			user_format = 1;
			parse_format(optarg);
			break;
		case 'p':
			parse_list(optarg, &sel.pids, number);
			break;
		case 't':
			parse_list(optarg, &sel.ttys, tty_id);
			break;
		case 'u':
			parse_list(optarg, &sel.euids, user_id);
			break;
		case 'U':
			parse_list(optarg, &sel.ruids, user_id);
			break;
		case 'w':
			wide++;
			break;
		default:
			usage();
		}
	}
	if (optind != argc)
		usage();
	if (wide == 1 && width != 0 && width < 132)
		width = 132;
	else if (wide > 1)
		width = 0;
	if (user_format)
		return;
	if (lng) {
		add_col(C_F, NULL);
		add_col(C_S, NULL);
		add_col(full ? C_USER : C_UID, "UID");
	} else if (full)
		add_col(C_USER, "UID");
	add_col(C_PID, NULL);
	if (lng || full) {
		add_col(C_PPID, NULL);
		add_col(C_C, NULL);
	}
	if (lng) {
		add_col(C_PRI, NULL);
		add_col(C_NICE, NULL);
		add_col(C_ADDR, NULL);
		add_col(C_SZ, NULL);
		add_col(C_WCHAN, NULL);
	}
	if (full)
		add_col(C_STIME, NULL);
	add_col(C_TTY, "TTY");
	add_col(C_TIME, NULL);
	add_col(full ? C_ARGS : C_COMM, "CMD");
}

static void
bsd_options(int argc, char **argv)
{
	char *ap = argv[1];
	int lng = 0, user = 0;

	if (argc > 2)
		usage();
	while (ap != NULL && *ap != '\0') {
		switch (*ap++) {
		case 'a':
			sel.all = 1;
			break;
		case 'c':
			sel.comm = 1;
			break;
		case 'g':
			sel.groups = 1;
			break;
		case 'l':
		case 'n':
			lng = 1;
			break;
		case 't':
			if (*ap == '\0' && my_tty == NODEV)
				break;
			add_id(&sel.ttys, *ap != '\0' ? (long)tty_dev(ap) :
			    (long)my_tty);
			sel.all = 1;
			ap += strlen(ap);
			break;
		case 'u':
			user = 1;
			break;
		case 'w':
			if (width != 0 && width < 132)
				width = 132;
			else
				width = 0;
			break;
		case 'x':
			sel.noterm = 1;
			break;
		default:
			if (!isdigit((unsigned char)ap[-1]))
				usage();
			add_id(&sel.pids, number(ap - 1));
			sel.all = sel.noterm = 1;
			ap += strlen(ap);
			break;
		}
	}
	if (lng && user) {
		fputs("ps: specify only one of l and u\n", stderr);
		exit(1);
	}
	if (lng) {
		add_col(C_F, NULL);
		add_col(C_S, NULL);
		add_col(C_UID, NULL);
	} else if (user)
		add_col(C_USER, NULL);
	add_col(C_PID, NULL);
	if (lng) {
		add_col(C_PPID, NULL);
		add_col(C_C, "CPU");
		add_col(C_PRI, NULL);
	}
	if (lng || user)
		add_col(C_NICE, "NICE");
	if (lng)
		add_col(C_ADDR, NULL);
	if (lng || user)
		add_col(C_SZ, NULL);
	if (lng)
		add_col(C_WCHAN, NULL);
	add_col(C_TT, NULL);
	add_col(C_CPUTIME, NULL);
	add_col(sel.comm ? C_COMM : C_ARGS, NULL);
}

static int
has_tty(const struct kinfo_proc *kp)
{
	return ((kp->kp_valid & KP_VALID_TTY) && kp->kp_tdev != NODEV);
}

/* The command name a row shows: pid 0 runs no image and carries none. */
static const char *
comm_of(const struct kinfo_proc *kp)
{
	if (kp->kp_pid == 0 && kp->kp_comm[0] == '\0')
		return ("swapper");
	return (kp->kp_comm);
}

static int
selected(const struct kinfo_proc *kp)
{
	if (sel.posix) {
		if (!sel.any)
			return (kp->kp_uid == my_euid && has_tty(kp) &&
			    kp->kp_tdev == my_tty);
		return (sel.all || (sel.ttyonly && has_tty(kp)) ||
		    (sel.noleaders && kp->kp_pid != kp->kp_pgid) ||
		    in_list(&sel.pids, kp->kp_pid) ||
		    ((kp->kp_valid & KP_VALID_LIVE) &&
		    in_list(&sel.pgids, kp->kp_pgid)) ||
		    ((kp->kp_valid & KP_VALID_CRED) &&
		    in_list(&sel.rgids, (long)kp->kp_rgid)) ||
		    in_list(&sel.euids, (long)kp->kp_uid) ||
		    ((kp->kp_valid & KP_VALID_CRED) &&
		    in_list(&sel.ruids, (long)kp->kp_ruid)) ||
		    (has_tty(kp) && in_list(&sel.ttys, (long)kp->kp_tdev)));
	}
	if (sel.pids.n != 0)
		return (in_list(&sel.pids, kp->kp_pid));
	if (sel.ttys.n != 0)
		return (has_tty(kp) && in_list(&sel.ttys, (long)kp->kp_tdev));
	if (!sel.all && kp->kp_uid != my_uid && kp->kp_uid != my_euid)
		return (0);
	if (!has_tty(kp))
		return (sel.noterm);
	if (!sel.all && kp->kp_tdev != my_tty)
		return (0);
	/* Login shells and the children of init only with g or x. */
	if (!sel.groups && !sel.noterm &&
	    (kp->kp_ppid == 1 || comm_of(kp)[0] == '-'))
		return (0);
	return (1);
}

/* Terminal names from /dev: the console and the tty entries. */
static struct ttyname {
	char	name[16];
	dev_t	dev;
} ttys[24];
static int nttys;

static void
read_ttys(void)
{
	DIR *dir;
	struct direct *dp;
	struct stat st;
	char path[MAXNAMLEN + 6];

	if ((dir = opendir("/dev")) == NULL)
		return;
	while ((dp = readdir(dir)) != NULL && (size_t)nttys < NITEMS(ttys)) {
		if (strncmp(dp->d_name, "tty", 3) != 0 &&
		    strcmp(dp->d_name, "console") != 0)
			continue;
		if (strlen(dp->d_name) >= sizeof(ttys[0].name))
			continue;
		snprintf(path, sizeof(path), "/dev/%s", dp->d_name);
		if (stat(path, &st) != 0 || !S_ISCHR(st.st_mode))
			continue;
		strcpy(ttys[nttys].name, dp->d_name);
		ttys[nttys].dev = st.st_rdev;
		nttys++;
	}
	closedir(dir);
}

/*
 * A session opened through the console redirector carries the device
 * cnopen() substitutes (sys/dev/cons.c), which {CTL_MACHDEP, CPU_CONSDEV}
 * reports, while /dev/console's node keeps the redirector's own number.
 * The console therefore wins over the real device's own /dev entry.
 */
static const char *
tty_name(dev_t dev)
{
	if (consdev != NODEV && dev == consdev)
		return ("console");
	for (int i = 0; i < nttys; i++)
		if (ttys[i].dev == dev)
			return (ttys[i].name);
	return ("?");
}

/* A user or group name, or the decimal id when there is none. */
static const char *
id_name(long id, int group, char *buf, size_t n)
{
	static struct {
		long	id;
		char	group;
		char	name[9];
	} cache[8];
	static unsigned next;
	const char *name = NULL;
	unsigned i;

	for (i = 0; i < NITEMS(cache); i++)
		if (cache[i].name[0] != '\0' && cache[i].id == id &&
		    cache[i].group == group)
			return (cache[i].name);
	if (group) {
		struct group *gr = getgrgid((gid_t)id);

		if (gr != NULL)
			name = gr->gr_name;
	} else {
		struct passwd *pw = getpwuid((uid_t)id);

		if (pw != NULL)
			name = pw->pw_name;
	}
	if (name == NULL || strlen(name) >= sizeof(cache[0].name)) {
		snprintf(buf, n, "%ld", id);
		return (buf);
	}
	i = next++ % NITEMS(cache);
	cache[i].id = id;
	cache[i].group = (char)group;
	strcpy(cache[i].name, name);
	return (cache[i].name);
}

/* [dd-]hh:mm:ss, or [[dd-]hh:]mm:ss when the hours may be omitted. */
static void
put_duration(char *buf, size_t n, unsigned long secs, int short_form)
{
	unsigned long days = secs / 86400, hours = secs / 3600 % 24;
	unsigned long mins = secs / 60 % 60;
	int len = 0;

	secs %= 60;
	if (days != 0)
		len = snprintf(buf, n, "%lu-", days);
	if (days != 0 || hours != 0 || !short_form)
		len += snprintf(buf + len, n - len, "%02lu:", hours);
	snprintf(buf + len, n - len, "%02lu:%02lu", mins, secs);
}

/* Copy src into buf, each byte outside printable ASCII as '?'. */
static const char *
printable(char *buf, size_t n, const char *src)
{
	size_t i;

	for (i = 0; i + 1 < n && src[i] != '\0'; i++)
		buf[i] = src[i] >= ' ' && src[i] <= '~' ? src[i] : '?';
	buf[i] = '\0';
	return (buf);
}

/* The text of one cell; "-" for a field the record does not carry. */
static const char *
cell(const struct kinfo_proc *kp, enum column col, char *buf, size_t n)
{
	static const char states[] = "0SWRIZT";
	unsigned live = kp->kp_valid & KP_VALID_LIVE;
	unsigned cred = kp->kp_valid & KP_VALID_CRED;
	unsigned times = kp->kp_valid & KP_VALID_TIMES;
	unsigned long cpu = (unsigned long)kp->kp_utime + kp->kp_stime;
	long elapsed = now - kp->kp_start;

	switch (col) {
	case C_F:
		snprintf(buf, n, "%o", kp->kp_flag & 0377);
		return (buf);
	case C_S:
		buf[0] = kp->kp_stat < sizeof(states) - 1 ?
		    states[kp->kp_stat] : '?';
		buf[1] = '\0';
		return (buf);
	case C_UID:
		snprintf(buf, n, "%lu", (unsigned long)kp->kp_uid);
		return (buf);
	case C_USER:
		return (id_name((long)kp->kp_uid, 0, buf, n));
	case C_RUSER:
		return (cred ? id_name((long)kp->kp_ruid, 0, buf, n) : "-");
	case C_GROUP:
		return (cred ? id_name((long)kp->kp_gid, 1, buf, n) : "-");
	case C_RGROUP:
		return (cred ? id_name((long)kp->kp_rgid, 1, buf, n) : "-");
	case C_PID:
		snprintf(buf, n, "%d", kp->kp_pid);
		return (buf);
	case C_PPID:
		snprintf(buf, n, "%d", kp->kp_ppid);
		return (buf);
	case C_PGID:
		if (!live)
			return ("-");
		snprintf(buf, n, "%d", kp->kp_pgid);
		return (buf);
	case C_C:
		if (!live)
			return ("-");
		snprintf(buf, n, "%u", kp->kp_cpu);
		return (buf);
	case C_PRI:
		if (!live)
			return ("-");
		snprintf(buf, n, "%d", kp->kp_pri);
		return (buf);
	case C_NICE:
		if (!live)
			return ("-");
		snprintf(buf, n, "%d", kp->kp_nice);
		return (buf);
	case C_SZ:
		if (!live)
			return ("-");
		snprintf(buf, n, "%lu", ((unsigned long)kp->kp_dsize +
		    kp->kp_ssize + USIZE + 1023) / 1024);
		return (buf);
	case C_VSZ:
		if (!live)
			return ("-");
		snprintf(buf, n, "%lu",
		    ((unsigned long)kp->kp_dsize + kp->kp_ssize + 1023) / 1024);
		return (buf);
	case C_STIME:
		if (kp->kp_valid & KP_VALID_START) {
			time_t start = kp->kp_start;
			struct tm tm = *localtime(&start);
			static const char months[] =
			    "JanFebMarAprMayJunJulAugSepOctNovDec";

			if (now - start < 86400)
				snprintf(buf, n, "%02d:%02d", tm.tm_hour,
				    tm.tm_min);
			else
				snprintf(buf, n, "%.3s%02d",
				    months + 3 * tm.tm_mon, tm.tm_mday);
			return (buf);
		}
		return ("-");
	case C_TTY:
		return (has_tty(kp) ? tty_name(kp->kp_tdev) : "-");
	case C_TT: {
		const char *name;

		if (!has_tty(kp))
			return ((kp->kp_valid & KP_VALID_TTY) ? "?" : "-");
		name = tty_name(kp->kp_tdev);
		if (strcmp(name, "console") == 0)
			return ("co");
		return (strncmp(name, "tty", 3) == 0 ? name + 3 : name);
	}
	case C_TIME:
		if (!times)
			return ("-");
		put_duration(buf, n, (cpu + hz / 2) / hz, 0);
		return (buf);
	case C_CPUTIME:
		if (!times)
			return ("-");
		cpu = (cpu + hz / 2) / hz;
		snprintf(buf, n, "%3lu:%02lu", cpu / 60, cpu % 60);
		return (buf);
	case C_ETIME:
		if (!(kp->kp_valid & KP_VALID_START) || elapsed < 0)
			return ("-");
		put_duration(buf, n, (unsigned long)elapsed, 1);
		return (buf);
	case C_PCPU: {
		unsigned long permille;

		if (!times || !(kp->kp_valid & KP_VALID_START) || elapsed < 0)
			return ("-");
		permille = elapsed == 0 ? 0 :
		    cpu * 1000 / ((unsigned long)elapsed * hz);
		snprintf(buf, n, "%lu.%lu", permille / 10, permille % 10);
		return (buf);
	}
	case C_COMM:
		if (kp->kp_stat == SZOMB)
			return ("<defunct>");
		if (!(kp->kp_valid & KP_VALID_COMM))
			return ("-");
		return (printable(buf, n, comm_of(kp)));
	case C_ARGS:
		if (kp->kp_stat == SZOMB)
			return ("<defunct>");
		if ((kp->kp_valid & KP_VALID_ARGS) && kp->kp_args[0] != '\0')
			return (printable(buf, n, kp->kp_args));
		if (kp->kp_valid & KP_VALID_COMM) {
			char name[KI_COMMSZ];

			snprintf(buf, n, "[%s]",
			    printable(name, sizeof(name), comm_of(kp)));
			return (buf);
		}
		return ("-");
	case C_ADDR:
	case C_WCHAN:
	case C_COUNT:
		break;
	}
	return ("-");
}

static unsigned
col_width(const struct fmtcol *fc)
{
	const struct coldef *cd = &coldefs[fc->col];
	size_t w = strlen(fc->header);

	if (w == 0)
		w = strlen(cd->header);
	return (w > cd->width ? (unsigned)w : cd->width);
}

/*
 * Print one line: every column but the last padded to its width, text
 * left and numbers right, and the last cut to the line width.
 */
static void
print_line(const char *const *text)
{
	unsigned used = 0;

	for (int i = 0; i < nfmt; i++) {
		unsigned w = col_width(&fmt[i]);
		int left = coldefs[fmt[i].col].text;

		if (i != 0) {
			putchar(' ');
			used++;
		}
		if (i == nfmt - 1) {
			if (width == 0 || used >= width)
				printf("%s", text[i]);
			else
				printf("%.*s", (int)(width - used), text[i]);
			break;
		}
		printf(left ? "%-*s" : "%*s", (int)w, text[i]);
		used += w > strlen(text[i]) ? w : (unsigned)strlen(text[i]);
	}
	putchar('\n');
}

static int
row_order(const void *a, const void *b)
{
	const struct kinfo_proc *x = a, *y = b;
	long tx = has_tty(x) ? (long)x->kp_tdev : -1;
	long ty = has_tty(y) ? (long)y->kp_tdev : -1;

	if (tx != ty)
		return (tx < ty ? -1 : 1);
	return (x->kp_pid - y->kp_pid);
}

static struct kinfo_proc *
read_records(int *count)
{
	int mib[5] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0,
	    sizeof(struct kinfo_proc) };
	struct kinfo_proc *table;
	size_t size = 0;

	if (sysctl(mib, 5, NULL, &size, NULL, 0) < 0 ||
	    (table = malloc(size)) == NULL ||
	    sysctl(mib, 5, table, &size, NULL, 0) < 0) {
		perror("ps: kern.proc");
		exit(1);
	}
	*count = (int)(size / sizeof(struct kinfo_proc));
	return (table);
}

static void
read_context(void)
{
	int mib[2];
	struct clockinfo clock;
	struct winsize ws;
	struct stat st;
	const char *columns = getenv("COLUMNS");
	size_t len;

	mib[0] = CTL_KERN;
	mib[1] = KERN_CLOCKRATE;
	len = sizeof(clock);
	if (sysctl(mib, 2, &clock, &len, NULL, 0) == 0 && clock.hz > 0)
		hz = clock.hz;
	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_CONSDEV;
	len = sizeof(consdev);
	if (sysctl(mib, 2, &consdev, &len, NULL, 0) < 0)
		consdev = NODEV;

	width = 80;
	if (columns != NULL && digits(columns) > 0)
		width = (unsigned)digits(columns);
	else if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		width = ws.ws_col;
	for (int fd = 0; fd <= 2; fd++)
		if (isatty(fd) && fstat(fd, &st) == 0) {
			my_tty = st.st_rdev;
			break;
		}
	my_uid = getuid();
	my_euid = geteuid();
	now = time(NULL);
	read_ttys();
}

int
main(int argc, char **argv)
{
	struct kinfo_proc *table;
	const char *text[MAXFMT];
	char cells[MAXFMT][CELLSZ];
	int count, shown = 0, header = 0;

	read_context();
	if (argc > 1 && argv[1][0] == '-') {
		sel.posix = 1;
		posix_options(argc, argv);
	} else
		bsd_options(argc, argv);

	table = read_records(&count);
	qsort(table, count, sizeof(table[0]), row_order);
	for (int i = 0; i < nfmt; i++) {
		text[i] = fmt[i].header;
		header |= fmt[i].header[0] != '\0';
	}
	if (header)
		print_line(text);
	for (int r = 0; r < count; r++) {
		if (!selected(&table[r]))
			continue;
		for (int i = 0; i < nfmt; i++)
			text[i] = cell(&table[r], (enum column)fmt[i].col,
			    cells[i], sizeof(cells[i]));
		print_line(text);
		shown++;
	}
	free(table);
	return (shown == 0);
}
