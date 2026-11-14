#define _TIME_BITS 64
#include "time64.h"

int
setitimer(int which, const struct itimerval *value, struct itimerval *old)
{
    struct __itimerval32 narrow, before;
    int result;

    if (value && (__time64_narrow_interval(&narrow.it_interval,
        &value->it_interval) != 0 || __time64_narrow_interval(&narrow.it_value,
        &value->it_value) != 0))
        return -1;
    result = __sys_setitimer(which, value ? &narrow : 0, old ? &before : 0);
    if (result == 0 && old) {
        __time64_widen_interval(&old->it_interval, &before.it_interval);
        __time64_widen_interval(&old->it_value, &before.it_value);
    }
    return result;
}
