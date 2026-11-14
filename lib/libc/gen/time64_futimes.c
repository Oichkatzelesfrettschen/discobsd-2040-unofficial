#define _TIME_BITS 64
#include "time64.h"

/* Both file times are counts since 1970; a null pair means the present. */
int
futimes(int fd, const struct timeval times[2])
{
    struct __timeval32 narrow[2];

    if (times && (__time64_narrow_file_time(&narrow[0], &times[0]) != 0 ||
        __time64_narrow_file_time(&narrow[1], &times[1]) != 0))
        return -1;
    return __sys_futimes(fd, times ? narrow : 0);
}
