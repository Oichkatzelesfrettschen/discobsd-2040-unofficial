/*
 * bigtest: on-device check of the LARGE window epoch. Built twice: as
 * bigtest with a bss of BSSBYTES bytes, which exceeds the 144 KB window but
 * fits it plus the 16 KB pool and so must run under LARGE; and as
 * hugetest with one that exceeds both and must be refused at exec. Run
 * as root. bigtest patterns its bss, forks a child that verifies the
 * copy, reports the epoch it runs under, and expects "BIGTEST OK".
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#include <machine/swapram.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#ifndef BSSBYTES
#define BSSBYTES 150000
#endif

static unsigned char big[BSSBYTES];

static int
machdep(int id, int *val)
{
    int mib[2], old;
    size_t len = sizeof old;

    mib[0] = CTL_MACHDEP;
    mib[1] = id;
    if (sysctl(mib, 2, &old, &len, NULL, 0) == -1)
        return -1;
    *val = old;
    return 0;
}

static unsigned
check(int k)
{
    unsigned i, bad = 0;

    for (i = 0; i < BSSBYTES; i++)
        bad += big[i] != ((k * 29 + i / 8) & 0xff);
    return bad;
}

int
main(void)
{
    int epoch = -1, images = -1, pid, st;
    unsigned i, bad;

    for (i = 0; i < BSSBYTES; i++)
        big[i] = (7 * 29 + i / 8) & 0xff;
    machdep(CPU_SWAPRAM_EPOCH, &epoch);
    machdep(CPU_SWAPRAM_IMAGES, &images);
    printf("bss %d bytes, epoch %d, pool images %d\n", BSSBYTES, epoch, images);
    pid = fork();
    if (pid == 0) {
        /* the child's copy came through flash swap */
        sleep(2);
        exit(check(7) != 0);
    }
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    sleep(1);
    bad = check(7);
    if (wait(&st) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
        bad += 1000000;
    if (bad == 0 && epoch == SWAPRAM_LARGE && images == 0)
        printf("BIGTEST OK\n");
    else
        printf("BIGTEST FAIL: %u bad, epoch %d, images %d\n", bad, epoch, images);
    return bad != 0 || epoch != SWAPRAM_LARGE;
}
