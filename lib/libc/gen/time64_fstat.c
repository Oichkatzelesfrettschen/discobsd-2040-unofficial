#undef _TIME_BITS
#define _TIME_BITS 64
#include "time64.h"

int
fstat(int fd, struct stat *sb)
{
    struct __stat32 narrow;
    int result = __sys_fstat(fd, sb ? &narrow : 0);

    if (result == 0 && sb)
        __time64_widen_stat(sb, &narrow);
    return result;
}
