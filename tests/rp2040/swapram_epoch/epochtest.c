/*
 * epochtest: on-device check of the SMALL/LARGE window epoch. Run as
 * root. Children pattern their memory and sleep so the swapper fills
 * the pool; a write of 1 to machdep.swapram_epoch must evacuate them
 * and close the pool, children forked under LARGE must stay out of it,
 * a write of 0 must reopen it, and every child must find its pattern
 * intact. Expects "EPOCHTEST OK".
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

#define NCHILD  3
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
child(int k, int secs)
{
    static unsigned char data[DATA];
    unsigned i, bad = 0;

    for (i = 0; i < DATA; i++)
        data[i] = (k * 53 + i / 8) & 0xff;
    sleep(secs);
    for (i = 0; i < DATA; i++)
        bad += data[i] != ((k * 53 + i / 8) & 0xff);
    return bad != 0;
}

static int
spawn(int base, int secs)
{
    int k, pid;

    for (k = 0; k < NCHILD; k++) {
        pid = fork();
        if (pid == 0)
            exit(child(base + k, secs));
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
    }
    return NCHILD;
}

static int
reap(int n)
{
    int st, failed = 0;

    while (n-- > 0)
        if (wait(&st) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
            failed++;
    return failed;
}

/* Poll until the pool holds an image; the swapper moves on its own clock. */
static int
wait_images(int secs)
{
    int v = 0;

    while (secs-- > 0) {
        if (machdep(CPU_SWAPRAM_IMAGES, &v, 0) == 0 && v > 0)
            return v;
        sleep(1);
    }
    return v;
}

static int
wait_epoch(int want)
{
    int tries, v = -1;

    for (tries = 0; tries < 40; tries++) {
        if (machdep(CPU_SWAPRAM_EPOCH, &v, 0) != 0)
            return -1;
        if (v == want)
            return v;
        sleep(1);
    }
    return v;
}

int
main(void)
{
    int v, images, failed = 0, bad = 0;

    spawn(0, 20);
    images = wait_images(15);
    printf("pool images with children asleep: %d\n", images);
    bad += images == 0;
    v = SWAPRAM_LARGE;
    if (machdep(CPU_SWAPRAM_EPOCH, &v, 1) != 0) {
        perror("swapram_epoch");
        return 1;
    }
    v = wait_epoch(SWAPRAM_LARGE);
    machdep(CPU_SWAPRAM_IMAGES, &images, 0);
    printf("after asking LARGE: epoch %d, pool images %d\n", v, images);
    bad += v != SWAPRAM_LARGE || images != 0;
    /* children forked under LARGE must swap to flash, never the pool */
    spawn(10, 8);
    images = wait_images(6);            /* must stay empty the whole time */
    printf("under LARGE with new children asleep: pool images %d\n", images);
    bad += images != 0;
    failed += reap(NCHILD);
    v = SWAPRAM_SMALL;
    machdep(CPU_SWAPRAM_EPOCH, &v, 1);
    v = wait_epoch(SWAPRAM_SMALL);
    printf("after asking SMALL: epoch %d\n", v);
    bad += v != SWAPRAM_SMALL;
    failed += reap(NCHILD);
    spawn(20, 20);
    images = wait_images(15);
    printf("back in SMALL with children asleep: pool images %d\n", images);
    bad += images == 0;
    failed += reap(NCHILD);
    if (failed == 0 && bad == 0)
        printf("EPOCHTEST OK\n");
    else
        printf("EPOCHTEST FAIL: %d children failed, %d checks failed\n",
            failed, bad);
    return failed || bad;
}
