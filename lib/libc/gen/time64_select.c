#define _TIME_BITS 64
#include "time64.h"

int
select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout)
{
    struct __timeval32 narrow;

    if (timeout && __time64_narrow_interval(&narrow, timeout) != 0)
        return -1;
    return __sys_select(nfds, readfds, writefds, exceptfds,
        timeout ? &narrow : 0);
}
