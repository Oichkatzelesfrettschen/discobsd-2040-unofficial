/*
 * Daemon table for init: one record per line of /etc/daemons,
 *
 *	command [args ...]  uid:gid  chroot  policy
 *
 * The last three fields are read from the right, so the command keeps its
 * own spaces. uid and gid are numeric, which keeps the passwd and group
 * parsers out of init. chroot is an absolute directory, or "-" for none.
 * policy is "backoff" or "capped". A line whose first nonblank character
 * is '#' is a comment.
 *
 * init forks each entry itself and keeps its pid, so a supervised command
 * must stay in the foreground (update -f). The parser and the restart
 * policy here make no system call; init.c owns fork, confinement and exec,
 * and tests/init_daemons exercises this file on the host.
 */
#ifndef _INIT_DAEMONS_H_
#define _INIT_DAEMONS_H_

#include <sys/types.h>

#define	DAEMONS_FILE	"/etc/daemons"
#define	DCMDSIZ		128	/* command and arguments, NUL included */
#define	DROOTSIZ	48	/* chroot path, NUL included */
#define	DNARGS		12	/* argv slots, terminating null included */

/*
 * backoff waits 1, 2, 4 ... seconds between restarts, up to DBACKOFF_MAX.
 * capped waits DCAPPED_DELAY and disables the entry, until the next
 * SIGHUP, once it has been restarted DCAPPED_TRIES times without a healthy
 * run. A run of DHEALTHY_RUN seconds or more clears the failure count
 * under both policies. The ceilings bound how often a crashing daemon
 * execs from flash and how much console output its failures produce.
 */
#define	DPOLICY_BACKOFF	0
#define	DPOLICY_CAPPED	1
#define	DBACKOFF_MAX	60
#define	DCAPPED_TRIES	6
#define	DCAPPED_DELAY	5
#define	DHEALTHY_RUN	60

struct dtab {
	char	cmd[DCMDSIZ];	/* path and arguments, single-space separated */
	char	root[DROOTSIZ];	/* chroot directory, "" for none */
	uid_t	uid;
	gid_t	gid;
	char	policy;
	char	xflag;		/* merge(): found in the reread file */
	char	disabled;	/* capped entry out of tries until SIGHUP */
	int	pid;		/* > 0 running, -1 restart pending, 0 idle */
	int	fails;		/* exits since the last healthy run */
	time_t	started;
	time_t	next_try;	/* restart deadline while pid is -1 */
	struct	dtab *next;
};

/* 1 parsed an entry, 0 blank or comment, -1 malformed (d unspecified). */
int	dtab_parse(const char *line, struct dtab *d);
/* Nonzero when a and b name the same command with the same confinement. */
int	dtab_same(const struct dtab *a, const struct dtab *b);
/*
 * Splits buf (a copy of d->cmd) into argv in place; returns the argument
 * count, the slot after the last holding a null pointer.
 */
int	dtab_argv(char *buf, char **argv);
/*
 * Records an exit at now and returns the seconds until the restart, or -1
 * when the entry is disabled. The same call records a refused fork.
 */
int	dtab_exited(struct dtab *d, time_t now);
/*
 * Formats the console line for an exit: status is the wait(2) status, or
 * -1 for a refused fork; delay is dtab_exited()'s result.
 */
void	dtab_describe(char *buf, size_t len, const struct dtab *d,
	    int status, int delay);

#endif /* _INIT_DAEMONS_H_ */
