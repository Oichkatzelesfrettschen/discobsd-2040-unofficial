/* Host stand-in for <sys/sysctl.h>: the call and node name dev_picocalc.c uses. */
#include <stddef.h>

int sysctl(int *, unsigned, void *, size_t *, void *, size_t);
#define CTL_MACHDEP 7
