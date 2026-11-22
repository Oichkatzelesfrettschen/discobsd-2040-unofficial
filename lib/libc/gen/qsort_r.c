#if defined(DISCOBSD_SHARED_LIBC) && DISCOBSD_SHARED_LIBC == 1
/* The shared qsort member owns both public entries and their sorting core. */
#include <stdlib.h>
#else
#define _DISCOBSD_QSORT_R
#include "qsort.c"
#endif
