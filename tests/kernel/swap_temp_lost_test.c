/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Host gate for the /dev/tempN transfers of sys/dev/swap.c (swcread() and
 * swcwrite(), extracted verbatim by swap_ioctl_extract.py --transfers) on a
 * swap device that can leave (SDSWAP_ENABLED). The device model fails
 * transfer number fail_at and every one after it, as swap_with_buf() does
 * once swap_lost is set, and the production uiomove() (kern_subr.c) moves
 * the caller's bytes. For every failure point the bytes a call consumed
 * must be exactly those of the blocks whose transfer completed, on the
 * device for a write and in the caller's buffer for a read, with the offset
 * and the residual count agreeing, so a retry after EIO resumes at the
 * first byte that did not move.
 */
#include "hostkern.h"

/* The extracted file includes the kernel headers it needs. */
#ifndef SWAP_TEMP_FUNCTION
#define SWAP_TEMP_FUNCTION "swap_temp_function.c"
#endif
#include SWAP_TEMP_FUNCTION
#include <sys/user.h>

#define TEMP_START	4
#define TEMP_BLOCKS	8
#define MEDIA_BLOCKS	(TEMP_START + TEMP_BLOCKS)
/* Byte x of the temporary unit on the device. */
#define UNIT_BYTE(x)	(((unsigned char *)media)[TEMP_START * DEV_BSIZE + (x)])

char swap_removable, swap_lost;
struct user u;
static unsigned char media[MEDIA_BLOCKS][DEV_BSIZE];
static unsigned char buffer_data[DEV_BSIZE];
static struct buf buffer;
static int fail_at = -1, transfers;
/* Blocks of the unit whose transfer completed, in order. */
static int moved[TEMP_BLOCKS * 2], nmoved;

struct buf *
getnewbuf(void)
{
    buffer.b_addr = (caddr_t)buffer_data;
    return &buffer;
}

void
brelse(struct buf *bp)
{
    HK_CHECK(bp == &buffer);
}

void
swap(size_t blkno, size_t coreaddr, int count, int rdflg)
{
    HK_CHECK(count == DEV_BSIZE && coreaddr == (size_t)(uintptr_t)buffer_data);
    HK_CHECK(blkno >= TEMP_START && blkno < MEDIA_BLOCKS);
    if (swap_lost)
        return;
    if (transfers++ == fail_at) {
        swap_lost = SWAP_LOST_NEW;
        return;
    }
    if (rdflg & B_READ)
        bcopy(media[blkno], buffer_data, DEV_BSIZE);
    else {
        bcopy(buffer_data, media[blkno], DEV_BSIZE);
        moved[nmoved++] = (int)(blkno - TEMP_START);
    }
}

/* The bytes of [offset, offset + length) inside block b of the unit. */
static unsigned
overlap(unsigned offset, unsigned length, int b)
{
    unsigned lo = (unsigned)b * DEV_BSIZE, hi = lo + DEV_BSIZE;

    if (lo < offset)
        lo = offset;
    if (hi > offset + length)
        hi = offset + length;
    return hi > lo ? hi - lo : 0;
}

static void
reset(int fail)
{
    unsigned b, i;

    for (b = 0; b < MEDIA_BLOCKS; b++)
        for (i = 0; i < DEV_BSIZE; i++)
            media[b][i] = (unsigned char)(b * 7 + i);
    td[0].t_start = TEMP_START;
    td[0].t_size = TEMP_BLOCKS;
    td[0].t_next = TD_APPEND_DISABLED;
    swap_lost = 0;
    fail_at = fail;
    transfers = 0;
    nmoved = 0;
}

static void
write_case(unsigned offset, unsigned length, int fail)
{
    static unsigned char source[3 * DEV_BSIZE];
    struct iovec iov;
    struct uio uio;
    unsigned consumed, expect = 0, i;
    int error, k;

    reset(fail);
    for (i = 0; i < length; i++)
        source[i] = (unsigned char)(0xa5 ^ i);
    iov.iov_base = (char *)source;
    iov.iov_len = length;
    uio.uio_iov = &iov;
    uio.uio_iovcnt = 1;
    uio.uio_offset = offset;
    uio.uio_resid = length;
    uio.uio_rw = UIO_WRITE;
    error = swcwrite(0, &uio, 0);
    for (k = 0; k < nmoved; k++)
        expect += overlap(offset, length, moved[k]);
    consumed = length - (unsigned)uio.uio_resid;
    HK_CHECK(error == (swap_lost ? EIO : 0));
    HK_CHECK(consumed == expect);
    HK_CHECK(uio.uio_offset == (off_t)(offset + consumed));
    HK_CHECK(iov.iov_base == (char *)source + consumed &&
        iov.iov_len == length - consumed);
    for (i = 0; i < consumed; i++)
        HK_CHECK(UNIT_BYTE(offset + i) == source[i]);
}

static void
read_case(unsigned offset, unsigned length, int fail)
{
    static unsigned char target[3 * DEV_BSIZE];
    struct iovec iov;
    struct uio uio;
    unsigned consumed, i;
    int error;

    reset(fail);
    bzero(target, sizeof(target));
    iov.iov_base = (char *)target;
    iov.iov_len = length;
    uio.uio_iov = &iov;
    uio.uio_iovcnt = 1;
    uio.uio_offset = offset;
    uio.uio_resid = length;
    uio.uio_rw = UIO_READ;
    error = swcread(0, &uio, 0);
    consumed = length - (unsigned)uio.uio_resid;
    HK_CHECK(error == (swap_lost ? EIO : 0));
    HK_CHECK(uio.uio_offset == (off_t)(offset + consumed));
    /* Reads complete whole blocks: every block before the failure. */
    HK_CHECK(consumed == (fail < 0 ? length :
        overlap(offset, length, 0) * (fail > 0) +
        (fail > 1 ? overlap(offset, length, 1) : 0) +
        (fail > 2 ? overlap(offset, length, 2) : 0)));
    for (i = 0; i < consumed; i++)
        HK_CHECK(target[i] == UNIT_BYTE(offset + i));
}

int
main(void)
{
    static const unsigned shapes[][2] = {
        { 0, 3 * DEV_BSIZE }, { 512, 2 * DEV_BSIZE }, { 100, 300 },
        { DEV_BSIZE, DEV_BSIZE + 1 },
    };
    unsigned s;
    int fail;

    for (s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++) {
        for (fail = -1; fail < 7; fail++) {
            write_case(shapes[s][0], shapes[s][1], fail);
            if (shapes[s][0] == 0)
                read_case(shapes[s][0], shapes[s][1], fail);
        }
    }
    /* A device that left before the call moves and consumes nothing. */
    reset(-1);
    swap_lost = SWAP_LOST_SWEPT;
    {
        unsigned char source[DEV_BSIZE] = { 1 };
        struct iovec iov = { (char *)source, sizeof(source) };
        struct uio uio = { &iov, 1, 0, sizeof(source), UIO_WRITE };

        HK_CHECK(swcwrite(0, &uio, 0) == EIO);
        HK_CHECK(uio.uio_resid == sizeof(source) && uio.uio_offset == 0);
        HK_CHECK(transfers == 0 && nmoved == 0);
    }
    return hk_verdict("swap temp lost-device transfers");
}
