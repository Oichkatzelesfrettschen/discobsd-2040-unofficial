#undef _TIME_BITS
#define _TIME_BITS 64
#include "time64.h"

int
getrusage(int who, struct rusage *usage)
{
    struct __rusage32 narrow;
    int result = __sys_getrusage(who, usage ? &narrow : 0);

    if (result == 0 && usage)
        __time64_widen_rusage(usage, &narrow);
    return result;
}
