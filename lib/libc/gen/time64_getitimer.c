#undef _TIME_BITS
#define _TIME_BITS 64
#include "time64.h"

int
getitimer(int which, struct itimerval *value)
{
    struct __itimerval32 narrow;
    int result = __sys_getitimer(which, value ? &narrow : 0);

    if (result == 0 && value) {
        __time64_widen_interval(&value->it_interval, &narrow.it_interval);
        __time64_widen_interval(&value->it_value, &narrow.it_value);
    }
    return result;
}
