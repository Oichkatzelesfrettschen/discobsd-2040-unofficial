#undef _TIME_BITS
#define _TIME_BITS 64
#include "time64.h"

/*
 * The kernel writes the usage only for a child that has ended; for a
 * stopped child it reports the status and leaves the buffer alone
 * (wait1(), sys/kern/kern_exit.c). The status therefore decides whether
 * the narrow buffer holds anything to widen, and is read through a local
 * when the caller did not ask for it.
 */
pid_t
wait4(pid_t pid, int *status, int options, struct rusage *usage)
{
    struct __rusage32 narrow;
    int report;
    pid_t result;

    if (status == 0)
        status = &report;
    result = __sys_wait4(pid, status, options, usage ? &narrow : 0);
    if (result > 0 && usage && !WIFSTOPPED(*status))
        __time64_widen_rusage(usage, &narrow);
    return result;
}
