#define _TIME_BITS 64
#include "time64.h"

int
fstat(int fd, struct stat *sb)
{
    struct __stat32 narrow;
    int result = __sys_fstat(fd, &narrow);

    if (result == 0)
        __time64_widen_stat(sb, &narrow);
    return result;
}
