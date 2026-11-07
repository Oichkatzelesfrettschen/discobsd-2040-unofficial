/*
 * One packed executable stream expanded through a caller-owned decoder.
 * The loop admits a stream only when it consumes every packed byte,
 * produces exactly the promised length, and leaves no complete token
 * behind: a stream that ends short, or that carries a token past its
 * length, is refused with the same result whether it is a forged header
 * or a flipped bit. The kernel and the host packer compile this file
 * unchanged, so the packer's self-check runs the loader's loop.
 *
 * Output lands in dst while room remains there. With no dst the decoder
 * writes its own history ring a contiguous run at a time and only the CRC
 * sees the bytes. Once rlen bytes are out, the loop keeps decoding one
 * byte at a time into the ring: any byte produced then is a token past the
 * promised length and fails the stream, and a clean stall proves the
 * remaining input holds no complete token.
 */
#include <machine/hsx_decoder.h>
#include <sys/exec_hsaout.h>

_Static_assert (HSX_WINDOW == HSXD_WINDOW_BITS &&
    HSX_LOOKAHEAD <= HSXD_LOOKAHEAD_MAX,
    "the container's parameters must be ones the decoder implements");

int
hsx_expand (struct hsx_work *w,
    int (*fill) (void *ctx, unsigned off, void *buf, unsigned len),
    void *ctx, unsigned off, unsigned clen, unsigned char *dst,
    unsigned rlen, unsigned *crc)
{
    struct hsx_dec *d = &w->dec;
    unsigned char *base;
    unsigned in = 0, out = 0, avail, mask, room, n, c = CRC32_INIT;

    hsx_dec_init (d, HSX_LOOKAHEAD);
    while (in < clen) {
        avail = clen - in;
        if (avail > HSX_STAGE)
            avail = HSX_STAGE;
        if (fill (ctx, off + in, w->stage, avail) != 0)
            return -1;
        in += avail;
        hsx_dec_input (d, w->stage, avail);
        do {
            room = rlen - out;
            if (room != 0 && dst != 0) {
                base = dst;
                mask = ~0u;
            } else {
                base = d->win;
                mask = HSXD_WINDOW - 1;
                if (room > HSXD_WINDOW - (out & mask))
                    room = HSXD_WINDOW - (out & mask);
                else if (room == 0)
                    room = 1;
            }
            n = hsx_dec_run (d, base, mask, room);
            if (n != 0) {
                if (out == rlen)
                    return -1;
                c = crc32_update (c, base + (out & mask), n);
                out += n;
            }
        } while (n == room);
    }
    if (out != rlen)
        return -1;
    *crc = CRC32_FIN (c);
    return 0;
}
