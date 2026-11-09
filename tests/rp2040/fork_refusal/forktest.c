/*
 * forktest: on-device check of fork's swap reservation (sys/kern/kern_fork.c,
 * swap_image_reserve in sys/kern/vm_swap.c). A 100-KiB bss image filled with
 * xorshift output does not compress into the 16-KiB SwapRAM pool, so the
 * child copy needs flash swap; a regular pattern would compress into the
 * pool and never reach the flash reservation.
 * With flash swap held by talloc(8), fork must return -1 with ENOMEM and
 * the parent must keep running; with swap free, the child must see the
 * parent's bytes. Prints "FORKTEST ENOMEM" or "FORKTEST CHILD OK".
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#ifndef BSSBYTES
#define BSSBYTES (100 * 1024)
#endif

static unsigned char big[BSSBYTES];

/* xorshift32 (Marsaglia 2003): incompressible to heatshrink. */
static unsigned long
next(unsigned long *state)
{
    unsigned long x = *state;

    x ^= (x << 13) & 0xffffffffUL;
    x ^= x >> 17;
    x ^= (x << 5) & 0xffffffffUL;
    *state = x;
    return x;
}

static unsigned
mismatches(void)
{
    unsigned long state = 2463534242UL;
    unsigned i, bad = 0;

    for (i = 0; i < BSSBYTES; i++)
        bad += big[i] != (unsigned char)next(&state);
    return bad;
}

int
main(void)
{
    unsigned long state = 2463534242UL;
    unsigned i;
    int pid, status;

    for (i = 0; i < BSSBYTES; i++)
        big[i] = (unsigned char)next(&state);
    pid = fork();
    if (pid == 0)
        _exit(mismatches() != 0 ? 3 : 0);
    if (pid < 0) {
        if (errno == ENOMEM && mismatches() == 0) {
            printf("FORKTEST ENOMEM\n");
            return 0;
        }
        printf("FORKTEST FAIL fork errno %d\n", errno);
        return 1;
    }
    if (wait(&status) != pid || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        printf("FORKTEST FAIL child status %#x\n", status);
        return 1;
    }
    printf("FORKTEST CHILD OK\n");
    return 0;
}
