/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Heatshrink decoder for a 9-bit window, see machine/hsx_decoder.h.
 *
 * The bit reader keeps up to 32 stream bits in acc, left aligned, and tops
 * it up a byte at a time whenever fewer than 18 are valid, the longest
 * token for lookahead 8, so one refill serves one to three tokens. sh is
 * 24 minus the number of valid bits: it is both the shift that places the
 * next input byte below the valid bits and, after the subtraction in the
 * refill loop, the flag that says acc is full. A token is decoded only
 * when all of its bits are present: a literal needs 9, a back-reference
 * 10 + lookahead. The vendored decoder suspends when a field cannot be
 * completed and treats a stream that ends there as finished; this routine
 * stops at the same point, so the bits after the last whole token never
 * influence the output, whatever they hold.
 *
 * Output goes through a write pointer into room contiguous bytes at
 * base + (pos & mask). A back-reference reads base[(pos - distance) &
 * mask], which stays inside base[] for the mask the caller names, and a
 * distance that reaches before the first output byte yields zeros without
 * touching memory.
 */
#include <machine/hsx_decoder.h>

void
hsx_dec_init (struct hsx_dec *d, unsigned lookahead)
{
    d->ip = d->ie = 0;
    d->acc = 0;
    d->sh = 24;
    d->pos = d->cnt = d->off = 0;
    d->lb = lookahead;
}

unsigned
hsx_dec_run (struct hsx_dec *d, unsigned char *base, unsigned mask,
    unsigned room)
{
    const unsigned char *ip = d->ip;
    const unsigned char *const ie = d->ie;
    unsigned char *op = base + (d->pos & mask);
    unsigned char *const op0 = op;
    unsigned char *const lim = op + room;
    const unsigned lb = d->lb;
    int sh = d->sh;
    unsigned acc = d->acc, cnt = d->cnt, off = d->off, n, pos, src;

    if (cnt != 0)
        goto copy;
    while (op != lim) {
        if (sh > 6) {
            while (sh >= 0 && ip != ie) {
                acc |= (unsigned) *ip++ << sh;
                sh -= 8;
            }
            if (sh > 15)
                break;
        }
        if ((int) acc < 0) {
            *op++ = (unsigned char) (acc >> 23);
            acc <<= 9;
            sh += 9;
            continue;
        }
        if (sh > 14 - (int) lb)
            break;
        off = ((acc >> (31 - HSXD_WINDOW_BITS)) & (HSXD_WINDOW - 1)) + 1;
        cnt = ((acc >> (31 - HSXD_WINDOW_BITS - lb)) & ((1u << lb) - 1)) + 1;
        acc <<= 1 + HSXD_WINDOW_BITS + lb;
        sh += 1 + HSXD_WINDOW_BITS + lb;
copy:
        n = (unsigned) (lim - op);
        if (n > cnt)
            n = cnt;
        cnt -= n;
        pos = d->pos + (unsigned) (op - op0);
        while (n != 0 && pos < off) {
            *op++ = 0;
            pos++;
            n--;
        }
        src = pos - off;
        while (n-- != 0)
            *op++ = base[src++ & mask];
    }
    d->ip = ip;
    d->acc = acc;
    d->sh = sh;
    d->cnt = cnt;
    d->off = off;
    n = (unsigned) (op - op0);
    d->pos += n;
    return n;
}
