#define _TIME_BITS 64
#include "time64.h"

/* The correction and the correction still owed are signed intervals. */
int
adjtime(const struct timeval *delta, struct timeval *olddelta)
{
    struct __timeval32 narrow, owed;
    int result;

    if (delta && __time64_narrow_interval(&narrow, delta) != 0)
        return -1;
    result = __sys_adjtime(delta ? &narrow : 0, olddelta ? &owed : 0);
    if (result == 0 && olddelta)
        __time64_widen_interval(olddelta, &owed);
    return result;
}
