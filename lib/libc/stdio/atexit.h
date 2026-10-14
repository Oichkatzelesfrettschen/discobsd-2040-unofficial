#ifndef LIBC_STDIO_ATEXIT_H
#define LIBC_STDIO_ATEXIT_H

/*
 * One atexit(3) registration.  atexit.c pushes entries onto the LIFO list
 * headed by __atexit and exit.c walks it.  The two live in separate archive
 * members so a program that calls exit() alone links no malloc.
 */
struct atexit {
    struct atexit *next;        /* next entry in the list */
    void (*func)(void);         /* callback function */
};

extern struct atexit *__atexit;

#endif
