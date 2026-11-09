/*
 * windowtest: on-device check that the RP2040 user window admits an image
 * larger than 144 KiB and that every byte of a 170-KiB bss is private,
 * writable memory. Exec refuses the image unless USER_DATA_SIZE is at
 * least its text, data, bss and stack; the pattern pass faults or reads
 * back wrong if the MPU or the window end disagree with machparam.h.
 * Expects "WINDOWTEST OK".
 */
#include <stdio.h>

#ifndef BSSBYTES
#define BSSBYTES (170 * 1024)
#endif

static unsigned char big[BSSBYTES];

int
main(void)
{
    unsigned i, bad = 0;

    for (i = 0; i < BSSBYTES; i++)
        big[i] = (unsigned char)(i * 7 + i / 251);
    for (i = 0; i < BSSBYTES; i++)
        bad += big[i] != (unsigned char)(i * 7 + i / 251);
    printf("bss %u bytes at %p..%p, %u mismatches\n", (unsigned)BSSBYTES,
        (void *)&big[0], (void *)&big[BSSBYTES - 1], bad);
    if (bad != 0) {
        printf("WINDOWTEST FAIL\n");
        return 1;
    }
    printf("WINDOWTEST OK\n");
    return 0;
}
