#define _TIME_BITS 64
#include "time64.h"

pid_t
wait4(pid_t pid, int *status, int options, struct rusage *usage)
{
    struct __rusage32 narrow;
    pid_t result = __sys_wait4(pid, status, options, usage ? &narrow : 0);

    if (result > 0 && usage)
        __time64_widen_rusage(usage, &narrow);
    return result;
}
