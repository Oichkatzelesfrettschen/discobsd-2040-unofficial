#define _TIME_BITS 64
#include "time64.h"

/* Both file times are counts since 1970; a null pair means the present. */
int
utimes(const char *path, const struct timeval times[2])
{
    struct __timeval32 narrow[2];

    if (times && (__time64_narrow_epoch(&narrow[0], &times[0]) != 0 ||
        __time64_narrow_epoch(&narrow[1], &times[1]) != 0))
        return -1;
    return __sys_utimes(path, times ? narrow : 0);
}
