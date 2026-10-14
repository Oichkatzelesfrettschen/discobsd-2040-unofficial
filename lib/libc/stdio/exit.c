#include <unistd.h>

#include "atexit.h"

extern void _cleanup(void);	/* stdio's flush-and-close, or clnup.c's empty stub */

int errno;
struct atexit *__atexit;        /* points to head of LIFO stack */


void
exit (int code)
{
    struct atexit *p;

    for (p = __atexit; p; p = p->next)
        (*p->func)();
    _cleanup();
    _exit (code);
}
