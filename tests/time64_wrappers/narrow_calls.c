/*
 * Stand-ins for the 32-bit system call entry points, compiled with the
 * default view of time so that each takes the layout the kernel reads and
 * writes. Each records the times it was handed and returns the canned
 * ones; the wrappers under test are the only callers.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <string.h>

#include "narrow_calls.h"

_Static_assert(sizeof(time_t) == 4, "the stand-ins take the 32-bit layouts");
_Static_assert(sizeof(struct timeval) == 8, "struct timeval is two words");
_Static_assert(sizeof(struct stat) == 56, "struct stat is the kernel's");

int errno;
int narrow_calls;
int narrow_null;
int narrow_old_null;
unsigned long narrow_seen_sec[NARROW_SLOTS];
long narrow_seen_sub[NARROW_SLOTS];
unsigned long narrow_give_sec[NARROW_SLOTS];
long narrow_give_sub[NARROW_SLOTS];
int narrow_result;
int narrow_stopped;

int pselect(int, fd_set *, fd_set *, fd_set *, const struct timespec *,
    const sigset_t *);
int adjtime(const struct timeval *, struct timeval *);

void
narrow_reset(void)
{
	narrow_calls = 0;
	narrow_null = 0;
	narrow_old_null = 0;
	narrow_result = 0;
	narrow_stopped = 0;
	memset(narrow_seen_sec, 0, sizeof narrow_seen_sec);
	memset(narrow_seen_sub, 0, sizeof narrow_seen_sub);
	errno = 0;
}

static void
see(int slot, const struct timeval *tv)
{
	narrow_seen_sec[slot] = (unsigned long)tv->tv_sec;
	narrow_seen_sub[slot] = tv->tv_usec;
}

static void
give(int slot, struct timeval *tv)
{
	tv->tv_sec = (long)narrow_give_sec[slot];
	tv->tv_usec = narrow_give_sub[slot];
}

static int
fill_stat(struct stat *sb)
{
	narrow_calls++;
	narrow_null = sb == 0;
	if (sb == 0)
		return -1;
	memset(sb, 0, sizeof *sb);
	sb->st_dev = 3;
	sb->st_ino = 77;
	sb->st_mode = 0100644;
	sb->st_nlink = 2;
	sb->st_uid = 5;
	sb->st_gid = 6;
	sb->st_rdev = 9;
	sb->st_size = 123456;
	sb->st_atime = (time_t)narrow_give_sec[0];
	sb->st_mtime = (time_t)narrow_give_sec[1];
	sb->st_ctime = (time_t)narrow_give_sec[2];
	sb->st_blksize = 1024;
	sb->st_blocks = 121;
	sb->st_flags = 0x10;
	return narrow_result;
}

int stat(const char *path, struct stat *sb) { (void)path; return fill_stat(sb); }
int lstat(const char *path, struct stat *sb) { (void)path; return fill_stat(sb); }
int fstat(int fd, struct stat *sb) { (void)fd; return fill_stat(sb); }

int
gettimeofday(struct timeval *tv, struct timezone *tz)
{
	(void)tz;
	narrow_calls++;
	narrow_null = tv == 0;
	if (tv)
		give(0, tv);
	return narrow_result;
}

int
settimeofday(const struct timeval *tv, const struct timezone *tz)
{
	(void)tz;
	narrow_calls++;
	narrow_null = tv == 0;
	if (tv)
		see(0, tv);
	return narrow_result;
}

int
adjtime(const struct timeval *delta, struct timeval *olddelta)
{
	narrow_calls++;
	narrow_null = delta == 0;
	if (delta == 0)
		return -1;
	see(0, delta);
	narrow_old_null = olddelta == 0;
	if (olddelta)
		give(0, olddelta);
	return narrow_result;
}

int
select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *timeout)
{
	(void)nfds; (void)r; (void)w; (void)e;
	narrow_calls++;
	narrow_null = timeout == 0;
	if (timeout)
		see(0, timeout);
	return narrow_result;
}

int
pselect(int nfds, fd_set *r, fd_set *w, fd_set *e,
    const struct timespec *timeout, const sigset_t *mask)
{
	(void)nfds; (void)r; (void)w; (void)e; (void)mask;
	narrow_calls++;
	narrow_null = timeout == 0;
	if (timeout) {
		narrow_seen_sec[0] = (unsigned long)timeout->tv_sec;
		narrow_seen_sub[0] = timeout->tv_nsec;
	}
	return narrow_result;
}

static int
see_pair(const struct timeval times[2])
{
	narrow_calls++;
	narrow_null = times == 0;
	if (times) {
		see(0, &times[0]);
		see(1, &times[1]);
	}
	return narrow_result;
}

int utimes(const char *path, const struct timeval times[2]) { (void)path; return see_pair(times); }
int futimes(int fd, const struct timeval times[2]) { (void)fd; return see_pair(times); }

int
getitimer(int which, struct itimerval *value)
{
	(void)which;
	narrow_calls++;
	narrow_null = value == 0;
	if (value == 0)
		return -1;
	give(0, &value->it_interval);
	give(1, &value->it_value);
	return narrow_result;
}

int
setitimer(int which, const struct itimerval *value, struct itimerval *old)
{
	(void)which;
	narrow_calls++;
	narrow_null = value == 0;
	narrow_old_null = old == 0;
	if (value) {
		see(0, &value->it_interval);
		see(1, &value->it_value);
	}
	if (old) {
		give(2, &old->it_interval);
		give(3, &old->it_value);
	}
	return narrow_result;
}

static void
fill_rusage(struct rusage *usage)
{
	memset(usage, 0, sizeof *usage);
	give(0, &usage->ru_utime);
	give(1, &usage->ru_stime);
	usage->ru_maxrss = 11;
	usage->ru_minflt = 12;
	usage->ru_nivcsw = 13;
}

int
getrusage(int who, struct rusage *usage)
{
	(void)who;
	narrow_calls++;
	narrow_null = usage == 0;
	if (usage == 0)
		return -1;
	fill_rusage(usage);
	return narrow_result;
}

pid_t
wait4(pid_t pid, int *status, int options, struct rusage *usage)
{
	(void)pid; (void)options;
	narrow_calls++;
	narrow_null = usage == 0;
	/* As the kernel does: a stopped child reports its status alone. */
	if (status)
		*status = narrow_stopped ? 0177 : 0x300;
	if (usage && !narrow_stopped)
		fill_rusage(usage);
	return narrow_result;
}
