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

#ifndef _MACHINE_HSX_DECODER_H_
#define _MACHINE_HSX_DECODER_H_

/*
 * The port's decoder for heatshrink streams with a 9-bit window. The
 * packed executable container uses lookahead 3: Thumb text matches are
 * short, and lookahead 3 packs the shipped executables into 49 fewer root
 * blocks than 8 does (doc/research/exec-hsaout.md). The swap tier uses
 * lookahead 8, where long zero runs dominate. The decoder takes the
 * lookahead as a parameter (1 to 8 bits) and accepts the same streams as
 * the vendored heatshrink_decoder.c, byte for byte; the vendored source
 * stays in the tree as the reference the host differential test compares
 * against (tests/hsaout/hsx_decoder_diff.c).
 *
 * Stream layout, most significant bit first: a tag bit 1 and eight
 * literal bits; or a tag bit 0, a 9-bit index (distance - 1) and a
 * lookahead-bit count (length - 1). A back-reference may reach before the
 * first output byte, where the window holds zeros.
 */
#define HSXD_WINDOW_BITS    9
#define HSXD_WINDOW         (1u << HSXD_WINDOW_BITS)
#define HSXD_LOOKAHEAD_MAX  8

struct hsx_dec {
    const unsigned char *ip;    /* next input byte */
    const unsigned char *ie;    /* end of the current input chunk */
    unsigned acc;               /* bit buffer, next stream bit in bit 31 */
    int sh;                     /* 24 minus the valid bits in acc */
    unsigned pos;               /* bytes produced so far */
    unsigned cnt;               /* bytes left in a pending back-reference */
    unsigned off;               /* distance of the pending back-reference */
    unsigned lb;                /* lookahead bits */
    unsigned char win[HSXD_WINDOW];  /* history when the caller has no buffer */
};

/* Begin a stream with the given lookahead. */
void hsx_dec_init (struct hsx_dec *d, unsigned lookahead);

/* Offer n input bytes at p; they must stay valid until hsx_dec_run stalls. */
static inline void
hsx_dec_input (struct hsx_dec *d, const unsigned char *p, unsigned n)
{
    d->ip = p;
    d->ie = p + n;
}

/*
 * Produce up to room bytes. Stream byte number p lands at base[p & mask],
 * and a back-reference reads base[(p - distance) & mask], so two calling
 * conventions share the routine: a linear destination (base is the
 * destination, mask is ~0u, room is at most the space left), or the
 * decoder's own ring (base is d->win, mask is HSXD_WINDOW - 1, and the
 * caller limits room so the run does not cross the ring end). Returns the
 * bytes produced. A result below room means the current input chunk holds
 * no further complete token; the leftover bits stay in d for the next
 * chunk. A result equal to room means the caller's space ran out first,
 * and a back-reference may be left pending.
 */
unsigned hsx_dec_run (struct hsx_dec *d, unsigned char *base,
    unsigned mask, unsigned room);

/*
 * The decoder and its input staging area share one HSX_WORK_SIZE buffer,
 * which the kernel takes from the buffer cache for the length of one
 * expansion, so two processes in exec at once, or exec and the swapper,
 * never touch the same decoder.
 */
#define HSX_WORK_SIZE   1024
#define HSX_STAGE       (HSX_WORK_SIZE - sizeof (struct hsx_dec))

struct hsx_work {
    struct hsx_dec dec;
    unsigned char stage[HSX_STAGE];
};

/*
 * Expand one stream: clen packed bytes read through fill() from offset off,
 * into dst (or discarded when dst is null), which must yield exactly rlen
 * bytes. Every packed byte must be consumed, no complete token may follow
 * the last byte, and any byte past rlen is an error. On success *crc holds
 * the CRC-32 of the expanded bytes. Returns 0, or -1 on the first
 * deviation.
 */
int hsx_expand (struct hsx_work *w,
    int (*fill) (void *ctx, unsigned off, void *buf, unsigned len),
    void *ctx, unsigned off, unsigned clen, unsigned char *dst,
    unsigned rlen, unsigned *crc);
#endif
