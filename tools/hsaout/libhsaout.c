/*
 * The packer behind hsaout and fsutil's "pack" manifest command. The
 * encoder is the vendored heatshrink compiled with the container's window
 * and lookahead; the check pass is the kernel's own hsx_stream.c and
 * hsx_decoder.c, compiled from the same files.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/exec_hsaout.h>
#include <machine/hsx_decoder.h>
#include "heatshrink_encoder.h"
#include "libhsaout.h"

_Static_assert (HEATSHRINK_STATIC_WINDOW_BITS == HSX_WINDOW &&
    HEATSHRINK_STATIC_LOOKAHEAD_BITS == HSX_LOOKAHEAD,
    "the encoder must be built with the container's parameters");

/* Encode len bytes into out, whose capacity cap bounds the result. */
static int
encode (const unsigned char *src, size_t len, unsigned char *out, size_t cap,
    size_t *outlen)
{
    static heatshrink_encoder enc;
    size_t in = 0, done = 0, n;
    HSE_poll_res pres;
    HSE_finish_res fres;

    heatshrink_encoder_reset (&enc);
    while (in < len) {
        if (heatshrink_encoder_sink (&enc, (uint8_t *) src + in, len - in,
            &n) < 0)
            return -1;
        in += n;
        do {
            if (done == cap)
                return -1;
            pres = heatshrink_encoder_poll (&enc, out + done, cap - done, &n);
            if (pres < 0)
                return -1;
            done += n;
        } while (pres == HSER_POLL_MORE);
    }
    for (;;) {
        fres = heatshrink_encoder_finish (&enc);
        if (fres < 0)
            return -1;
        if (fres == HSER_FINISH_DONE)
            break;
        do {
            if (done == cap)
                return -1;
            pres = heatshrink_encoder_poll (&enc, out + done, cap - done, &n);
            if (pres < 0)
                return -1;
            done += n;
        } while (pres == HSER_POLL_MORE);
    }
    *outlen = done;
    return 0;
}

struct mem {
    const unsigned char *base;
    size_t len;
};

static int
memfill (void *ctx, unsigned off, void *buf, unsigned len)
{
    struct mem *m = ctx;

    if (off > m->len || len > m->len - off)
        return -1;
    memcpy (buf, m->base + off, len);
    return 0;
}

/* The kernel's expansion of one stream of a container held in memory. */
static int
expand (const unsigned char *packed, size_t len, unsigned off, unsigned clen,
    unsigned char *dst, unsigned rlen, unsigned *crc)
{
    struct hsx_work *w = malloc (HSX_WORK_SIZE);
    struct mem m = { packed, len };
    int r;

    if (w == NULL)
        return -1;
    r = hsx_expand (w, memfill, &m, off, clen, dst, rlen, crc);
    free (w);
    return r;
}

unsigned char *
hsaout_pack (const unsigned char *raw, size_t rawlen, size_t *outlen,
    const char **err)
{
    struct exec e;
    struct hsx x;
    unsigned char *out = NULL, *check = NULL;
    size_t cap, ctext, cdata;
    unsigned crc;

    *err = NULL;
    if (rawlen < sizeof e) {
        *err = "shorter than an a.out header";
        return NULL;
    }
    memcpy (&e, raw, sizeof e);
    if (N_GETMAGIC (e) != OMAGIC || N_GETMID (e) != MID_ZERO ||
        N_GETFLAG (e) != 0) {
        *err = "not a raw OMAGIC a.out";
        return NULL;
    }
    if (e.a_text > rawlen - sizeof e || e.a_data > rawlen - sizeof e - e.a_text) {
        *err = "text and data run past the end of the file";
        return NULL;
    }
    /*
     * Nine bits per literal is heatshrink's worst case, so this capacity
     * holds any stream; a stream that does not fit is a packer defect.
     */
    cap = HSX_HDRSIZE + (size_t) e.a_text + e.a_text / 8 +
        (size_t) e.a_data + e.a_data / 8 + 64;
    out = malloc (cap);
    if (out == NULL) {
        *err = "out of memory";
        return NULL;
    }
    if (encode (raw + sizeof e, e.a_text, out + HSX_HDRSIZE,
        cap - HSX_HDRSIZE, &ctext) != 0 ||
        encode (raw + sizeof e + e.a_text, e.a_data, out + HSX_HDRSIZE + ctext,
        cap - HSX_HDRSIZE - ctext, &cdata) != 0) {
        *err = "encoder failed";
        goto fail;
    }

    N_SETMAGIC (e, OMAGIC, MID_ZERO, EX_HSPACK);
    e.a_reltext = e.a_reldata = e.a_syms = 0;
    memset (&x, 0, sizeof x);
    x.x_sig = HSX_SIG;
    x.x_version = HSX_VERSION;
    x.x_hdrlen = sizeof x;
    x.x_codec = HSX_CODEC_HEATSHRINK;
    x.x_window = HSX_WINDOW;
    x.x_lookahead = HSX_LOOKAHEAD;
    x.x_ctext = (unsigned) ctext;
    x.x_cdata = (unsigned) cdata;
    x.x_textcrc = CRC32_FIN (crc32_update (CRC32_INIT, raw + sizeof e,
        e.a_text));
    x.x_datacrc = CRC32_FIN (crc32_update (CRC32_INIT,
        raw + sizeof e + e.a_text, e.a_data));
    crc = crc32_update (CRC32_INIT, &e, sizeof e);
    x.x_hdrcrc = CRC32_FIN (crc32_update (crc, &x, sizeof x));
    memcpy (out, &e, sizeof e);
    memcpy (out + sizeof e, &x, sizeof x);
    *outlen = HSX_HDRSIZE + ctext + cdata;

    /* The loader's view of what was just written. */
    check = hsaout_unpack (out, *outlen, &cap, err);
    if (check == NULL)
        goto fail;
    if (cap != sizeof e + e.a_text + e.a_data ||
        memcmp (check + sizeof e, raw + sizeof e, e.a_text + e.a_data) != 0) {
        *err = "self-check: expansion differs from the input";
        goto fail;
    }
    free (check);
    return out;
fail:
    free (out);
    free (check);
    return NULL;
}

unsigned char *
hsaout_unpack (const unsigned char *packed, size_t len, size_t *outlen,
    const char **err)
{
    struct exec e;
    struct hsx x;
    unsigned char *raw;
    unsigned crc;

    *err = NULL;
    if (len < HSX_HDRSIZE) {
        *err = "shorter than the container headers";
        return NULL;
    }
    memcpy (&e, packed, sizeof e);
    memcpy (&x, packed + sizeof e, sizeof x);
    switch (hsx_check (&e, &x, (unsigned long) len)) {
    case HSX_OK:
        break;
    case HSX_BADAOUT:
        *err = "not a packed a.out";
        return NULL;
    case HSX_BADSIG:
        *err = "container header rejected";
        return NULL;
    case HSX_SIZEWRAP:
        *err = "stream lengths wrap";
        return NULL;
    default:
        *err = "file size differs from the headers";
        return NULL;
    }
    if (e.a_text > 0xffffffffU - e.a_data ||
        e.a_text + e.a_data > 0xffffffffU - sizeof e) {
        *err = "segment sizes wrap";
        return NULL;
    }
    raw = malloc (sizeof e + (size_t) e.a_text + e.a_data);
    if (raw == NULL) {
        *err = "out of memory";
        return NULL;
    }
    if (expand (packed, len, (unsigned) HSX_HDRSIZE, x.x_ctext,
        raw + sizeof e, e.a_text, &crc) != 0 || crc != x.x_textcrc) {
        *err = "text stream rejected";
        free (raw);
        return NULL;
    }
    if (expand (packed, len, (unsigned) HSX_HDRSIZE + x.x_ctext, x.x_cdata,
        raw + sizeof e + e.a_text, e.a_data, &crc) != 0 ||
        crc != x.x_datacrc) {
        *err = "data stream rejected";
        free (raw);
        return NULL;
    }
    N_SETMAGIC (e, OMAGIC, MID_ZERO, 0);
    memcpy (raw, &e, sizeof e);
    *outlen = sizeof e + (size_t) e.a_text + e.a_data;
    return raw;
}

/*
 * The 2.11 file system keeps four direct block addresses in the inode,
 * then a single, a double and a triple indirect block of 256 addresses.
 */
unsigned long
hsaout_blocks (unsigned long len)
{
    unsigned long b = (len + 1023) / 1024;

    if (b > 4)
        b++;
    if (b > 4 + 256)
        b++;
    if (b > 4 + 256 + 256UL * 256)
        b++;
    return b;
}
