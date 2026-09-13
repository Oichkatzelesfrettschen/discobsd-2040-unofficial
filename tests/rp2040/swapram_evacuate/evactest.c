/*
 * evactest: on-device check of the pool's evacuation to flash. Forks
 * children that each fill their data and stack with a pattern of their
 * own and sleep, so the swapper moves them into the compressed pool;
 * the parent then asks the swapper through machdep.swapram_evacuate to
 * move every image to flash, waits for the answer, and the children
 * wake, verify their patterns and report. Expects "EVACTEST OK".
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#include <machine/swapram.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define NCHILD  4
#define DATA    12000

static int
machdep(int id, int *val, int set)
{
    int mib[2], old, err;
    size_t len = sizeof old;

    mib[0] = CTL_MACHDEP;
    mib[1] = id;
    /* The kernel sets no return value on success; only -1 is failure. */
    err = sysctl(mib, 2, &old, &len, set ? val : NULL, set ? sizeof *val : 0);
    if (err == -1)
        return -1;
    *val = old;
    return 0;
}

static int
child(int k)
{
    static unsigned char data[DATA];
    unsigned char stack[3000];
    unsigned i, bad = 0;

    for (i = 0; i < DATA; i++)
        data[i] = (k * 37 + i / 8) & 0xff;
    for (i = 0; i < sizeof stack; i++)
        stack[i] = (k * 91 + i / 4) & 0xff;
    sleep(8);
    for (i = 0; i < DATA; i++)
        bad += data[i] != ((k * 37 + i / 8) & 0xff);
    for (i = 0; i < sizeof stack; i++)
        bad += stack[i] != ((k * 91 + i / 4) & 0xff);
    printf("child %d: %u bad bytes\n", k, bad);
    return bad != 0;
}

int
main(void)
{
    int k, pid, st, v, before, tries, failed = 0;

    for (k = 0; k < NCHILD; k++) {
        pid = fork();
        if (pid == 0)
            exit(child(k));
        if (pid < 0) {
            perror("fork");
            return 1;
        }
    }
    sleep(3);
    if (machdep(CPU_SWAPRAM_IMAGES, &before, 0) != 0) {
        perror("swapram_images");
        return 1;
    }
    printf("pool images before: %d\n", before);
    v = 1;
    if (machdep(CPU_SWAPRAM_EVACUATE, &v, 1) != 0) {
        perror("swapram_evacuate");
        return 1;
    }
    for (tries = 0; tries < 50; tries++) {
        if (machdep(CPU_SWAPRAM_EVACUATE, &v, 0) != 0 ||
            v != SWAPRAM_EVAC_PENDING)
            break;
        sleep(1);
    }
    printf("evacuation result: %d (2 done, 3 no flash), pool images after: %d\n",
        v, machdep(CPU_SWAPRAM_IMAGES, &k, 0) == 0 ? k : -1);
    for (k = 0; k < NCHILD; k++) {
        if (wait(&st) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
            failed++;
    }
    if (failed == 0 && v == SWAPRAM_EVAC_DONE && before > 0)
        printf("EVACTEST OK\n");
    else
        printf("EVACTEST FAIL: %d children failed, result %d, images before %d\n",
            failed, v, before);
    return failed || v != SWAPRAM_EVAC_DONE || before == 0;
}
