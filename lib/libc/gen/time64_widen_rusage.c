#define _TIME_BITS 64
#include "time64.h"

#include <stddef.h>
#include <string.h>

/*
 * The two CPU times are intervals; the counters after them, ru_maxrss to
 * ru_nivcsw, have one layout in both structures and are copied whole.
 */
_Static_assert(sizeof(struct rusage) - offsetof(struct rusage, ru_maxrss) >=
    sizeof(struct __rusage32) - offsetof(struct __rusage32, ru_maxrss),
    "the wide rusage holds every counter of the narrow one");

void
__time64_widen_rusage(struct rusage *wide, const struct __rusage32 *narrow)
{
    __time64_widen_interval(&wide->ru_utime, &narrow->ru_utime);
    __time64_widen_interval(&wide->ru_stime, &narrow->ru_stime);
    memcpy(&wide->ru_maxrss, &narrow->ru_maxrss,
        sizeof(struct __rusage32) - offsetof(struct __rusage32, ru_maxrss));
}
