/*
 * One packed executable stream expanded through a caller-owned decoder.
 * The loop admits a stream only when it consumes every packed byte,
 * produces exactly the promised length, and finishes clean: a stream that
 * stalls, that runs past its length, or that ends short is refused with
 * the same result whether it is a forged header or a flipped bit. The
 * kernel and the host packer compile this file unchanged, so the packer's
 * self-check runs the loader's loop.
 */
#include <machine/hsx_decoder.h>
#include <sys/exec_hsaout.h>

int
hsx_expand (struct hsx_work *w,
    int (*fill) (void *ctx, unsigned off, void *buf, unsigned len),
    void *ctx, unsigned off, unsigned clen, unsigned char *dst,
    unsigned rlen, unsigned *crc)
{
    unsigned char probe[64];
    unsigned in = 0, out = 0, avail = 0, pos = 0, c = CRC32_INIT;
    unsigned char *buf;
    size_t n, room;
    int progress;
    HSD_poll_res pres;
    HSD_finish_res fres;

/*
 * Drain the decoder. Output lands in dst while room remains there, and
 * in the probe otherwise, so a byte past rlen is seen and refused rather
 * than written anywhere.
 */
#define HSX_DRAIN() do { \
        do { \
            if (dst != 0 && out < rlen) { \
                buf = dst + out; \
                room = rlen - out; \
            } else { \
                buf = probe; \
                room = sizeof probe; \
            } \
            pres = hsx_decoder_poll (&w->dec, buf, room, &n); \
            if (pres < 0) \
                return -1; \
            if (n != 0) { \
                if (n > rlen - out) \
                    return -1; \
                c = crc32_update (c, buf, (unsigned) n); \
                out += (unsigned) n; \
                progress = 1; \
            } \
        } while (pres == HSDR_POLL_MORE); \
    } while (0)

    hsx_decoder_reset (&w->dec);
    while (in < clen) {
        if (pos == avail) {
            avail = clen - in;
            if (avail > HSX_STAGE)
                avail = HSX_STAGE;
            if (fill (ctx, off + in, w->stage, avail) != 0)
                return -1;
            pos = 0;
        }
        if (hsx_decoder_sink (&w->dec, w->stage + pos, avail - pos, &n) < 0)
            return -1;
        pos += (unsigned) n;
        in += (unsigned) n;
        progress = n != 0;
        HSX_DRAIN ();
        if (! progress)
            return -1;      /* neither side advanced: the stream stalls */
    }
    for (;;) {
        fres = hsx_decoder_finish (&w->dec);
        if (fres < 0)
            return -1;
        if (fres == HSDR_FINISH_DONE)
            break;
        progress = 0;
        HSX_DRAIN ();
        if (! progress)
            return -1;      /* finish wants more and poll yields none */
    }
#undef HSX_DRAIN
    if (out != rlen)
        return -1;
    *crc = CRC32_FIN (c);
    return 0;
}
