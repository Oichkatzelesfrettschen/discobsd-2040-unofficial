#define _TIME_BITS 64
#include "time64.h"

int
settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    struct __timeval32 narrow;

    if (tv && __time64_narrow_epoch(&narrow, tv) != 0)
        return -1;
    return __sys_settimeofday(tv ? &narrow : 0, tz);
}
