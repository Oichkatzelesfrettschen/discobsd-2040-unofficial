#include <stdlib.h>

#include "atexit.h"

/*
 * Register a function to be performed at exit.
 */
int
atexit(void (*fn)(void))
{
    static struct atexit __atexit0; /* one guaranteed table */
    struct atexit *p;

    p = __atexit;
    if (! p) {
        p = &__atexit0;
    } else {
        p = malloc(sizeof(struct atexit));
        if (! p)
            return -1;
        p->next = __atexit;
    }
    p->func = fn;
    __atexit = p;
    return 0;
}
