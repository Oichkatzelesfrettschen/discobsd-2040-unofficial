#undef _TIME_BITS
#define _TIME_BITS 64
#include "time64.h"

int
gettimeofday(struct timeval *tv, struct timezone *tz)
{
    struct __timeval32 narrow;
    int result = __sys_gettimeofday(tv ? &narrow : 0, tz);

    if (result == 0 && tv) {
        tv->tv_sec = __time64_epoch(narrow.tv_sec);
        tv->tv_usec = narrow.tv_usec;
    }
    return result;
}
