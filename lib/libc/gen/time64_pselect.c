#undef _TIME_BITS
#define _TIME_BITS 64
#include "time64.h"

int
pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    const struct timespec *timeout, const sigset_t *sigmask)
{
    struct __timespec32 narrow;

    if (timeout) {
        if (!__time64_interval_fits(timeout->tv_sec) || timeout->tv_sec < 0) {
            errno = EINVAL;
            return -1;
        }
        narrow.tv_sec = (utime32_t)timeout->tv_sec;
        narrow.tv_nsec = timeout->tv_nsec;
    }
    return __sys_pselect(nfds, readfds, writefds, exceptfds,
        timeout ? &narrow : 0, sigmask);
}
