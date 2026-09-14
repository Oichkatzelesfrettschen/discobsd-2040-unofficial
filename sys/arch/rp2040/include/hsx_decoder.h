#ifndef _MACHINE_HSX_DECODER_H_
#define _MACHINE_HSX_DECODER_H_

/*
 * The heatshrink decoder compiled for the packed executable streams.
 * Lookahead is a compile-time constant of the vendored codec: the swap
 * tier's 8 suits swap images, where long zero runs dominate, while Thumb
 * text matches are short and lookahead 3 packs the shipped executables
 * into 49 fewer root blocks than 8 does (doc/research/exec-hsaout.md).
 * This second instance renames every entry point so both live in one
 * kernel; the window is the same 9 bits, so the state is the same 590
 * bytes as the swap tier's.
 */
#define HEATSHRINK_STATIC_WINDOW_BITS       9
#define HEATSHRINK_STATIC_LOOKAHEAD_BITS    3

#define heatshrink_decoder          hsx_decoder
#define heatshrink_decoder_reset    hsx_decoder_reset
#define heatshrink_decoder_sink     hsx_decoder_sink
#define heatshrink_decoder_poll     hsx_decoder_poll
#define heatshrink_decoder_finish   hsx_decoder_finish

#include "../heatshrink/heatshrink_decoder.h"

/*
 * The decoder and its input staging area share one HSX_WORK_SIZE buffer,
 * which the kernel takes from the buffer cache for the length of one
 * expansion, so two processes in exec at once, or exec and the swapper,
 * never touch the same decoder.
 */
#define HSX_WORK_SIZE   1024
#define HSX_STAGE       (HSX_WORK_SIZE - ((sizeof (hsx_decoder) + 3) & ~3u))

struct hsx_work {
    hsx_decoder dec;
    unsigned char stage[HSX_STAGE];
};

/*
 * Expand one stream: clen packed bytes read through fill() from offset off,
 * into dst (or discarded when dst is null), which must yield exactly rlen
 * bytes. Every packed byte must be consumed, the decoder must finish
 * clean, and any byte past rlen is an error. On success *crc holds the
 * CRC-32 of the expanded bytes. Returns 0, or -1 on the first deviation.
 */
int hsx_expand (struct hsx_work *w,
    int (*fill) (void *ctx, unsigned off, void *buf, unsigned len),
    void *ctx, unsigned off, unsigned clen, unsigned char *dst,
    unsigned rlen, unsigned *crc);
#endif
