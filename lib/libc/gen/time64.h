/*
 * Shared by the time64_*.c members: the wrappers that give a program built
 * with the 64-bit view of time (sys/_time_t.h) its system calls.
 *
 * Each wrapper is defined under the ordinary function name, which the
 * public header binds to the symbol __NAME64. It calls the 32-bit entry
 * point, declared here under a __sys_ name bound to the existing symbol,
 * with the layout the kernel reads and writes, and converts each count of
 * seconds:
 *
 *  - a count since 1970 (a clock reading, a file time) widens
 *    zero-extended, since the kernel holds it unsigned (utime32_t), and
 *    narrows only from 0..4294967295;
 *  - an interval (a timeout, a timer value, a clock correction, a CPU
 *    time) widens sign-extended and narrows only from the range of a long.
 *
 * A value outside its range fails with EINVAL before the kernel is
 * entered, so no call acts on a truncated time.
 */
#ifndef _LIBC_TIME64_H_
#define _LIBC_TIME64_H_

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <errno.h>

#ifndef __TIME64_VIEW
#error "a time64 member is compiled with _TIME_BITS defined as 64"
#endif

struct __itimerval32 {
    struct __timeval32 it_interval;
    struct __timeval32 it_value;
};

int __sys_stat(const char *, struct __stat32 *) __asm__("stat");
int __sys_lstat(const char *, struct __stat32 *) __asm__("lstat");
int __sys_fstat(int, struct __stat32 *) __asm__("fstat");
int __sys_gettimeofday(struct __timeval32 *, struct timezone *)
    __asm__("gettimeofday");
int __sys_settimeofday(const struct __timeval32 *, const struct timezone *)
    __asm__("settimeofday");
int __sys_adjtime(const struct __timeval32 *, struct __timeval32 *)
    __asm__("adjtime");
int __sys_select(int, fd_set *, fd_set *, fd_set *, struct __timeval32 *)
    __asm__("select");
int __sys_pselect(int, fd_set *, fd_set *, fd_set *,
    const struct __timespec32 *, const sigset_t *) __asm__("pselect");
int __sys_utimes(const char *, const struct __timeval32 *) __asm__("utimes");
int __sys_futimes(int, const struct __timeval32 *) __asm__("futimes");
int __sys_getitimer(int, struct __itimerval32 *) __asm__("getitimer");
int __sys_setitimer(int, const struct __itimerval32 *,
    struct __itimerval32 *) __asm__("setitimer");
int __sys_getrusage(int, struct __rusage32 *) __asm__("getrusage");
pid_t __sys_wait4(pid_t, int *, int, struct __rusage32 *) __asm__("wait4");

int pselect(int, fd_set *, fd_set *, fd_set *, const struct timespec *,
    const sigset_t *) __TIME64_NAME(pselect);

static inline time_t
__time64_epoch(long seconds)
{
    return (time_t)(utime32_t)seconds;
}

static inline int
__time64_epoch_fits(time_t seconds)
{
    return seconds >= 0 && seconds <= 0xffffffffLL;
}

static inline int
__time64_interval_fits(time_t seconds)
{
    return seconds >= -0x7fffffffLL - 1 && seconds <= 0x7fffffffLL;
}

static inline void
__time64_widen_interval(struct timeval *wide, const struct __timeval32 *narrow)
{
    wide->tv_sec = narrow->tv_sec;
    wide->tv_usec = narrow->tv_usec;
}

/* Returns 0, or -1 with errno set when the interval leaves a long. */
static inline int
__time64_narrow_interval(struct __timeval32 *narrow, const struct timeval *wide)
{
    if (!__time64_interval_fits(wide->tv_sec)) {
        errno = EINVAL;
        return -1;
    }
    narrow->tv_sec = (long)wide->tv_sec;
    narrow->tv_usec = wide->tv_usec;
    return 0;
}

/* Returns 0, or -1 with errno set when the count leaves 0..4294967295. */
static inline int
__time64_narrow_epoch(struct __timeval32 *narrow, const struct timeval *wide)
{
    if (!__time64_epoch_fits(wide->tv_sec)) {
        errno = EINVAL;
        return -1;
    }
    narrow->tv_sec = (long)(utime32_t)wide->tv_sec;
    narrow->tv_usec = wide->tv_usec;
    return 0;
}

void __time64_widen_stat(struct stat *, const struct __stat32 *);
void __time64_widen_rusage(struct rusage *, const struct __rusage32 *);

#endif /* !_LIBC_TIME64_H_ */
