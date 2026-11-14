#define _TIME_BITS 64
#include "time64.h"

int
stat(const char *path, struct stat *sb)
{
    struct __stat32 narrow;
    int result = __sys_stat(path, sb ? &narrow : 0);

    if (result == 0 && sb)
        __time64_widen_stat(sb, &narrow);
    return result;
}
