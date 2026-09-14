/*
 * The packed a.out container against the checks the loader makes: every
 * header bit, every stream bit of a small image, every truncation and
 * extension, forged lengths, wrong parameters, a fill that fails at every
 * chunk, and the exact-length rule in both directions. Each rejection
 * goes through hsaout_unpack, which is hsx_check plus the kernel's
 * hsx_expand, so a pass here is a pass of the loader's own code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/exec_hsaout.h>
#include <machine/hsx_decoder.h>
#include "libhsaout.h"

static int fails, checks;

#define CHECK(cond, ...) do { \
        checks++; \
        if (!(cond)) { \
            fails++; \
            printf ("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf (__VA_ARGS__); \
            printf ("\n"); \
        } \
    } while (0)

static unsigned seed = 12345;
static unsigned
rnd (void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

/* A raw a.out: header, text, data, with content of the named class. */
static unsigned char *
image (unsigned text, unsigned data, int class, size_t *len)
{
    struct exec e;
    unsigned char *raw = malloc (sizeof e + text + data);
    unsigned i;

    memset (&e, 0, sizeof e);
    N_SETMAGIC (e, OMAGIC, MID_ZERO, 0);
    e.a_text = text;
    e.a_data = data;
    e.a_bss = 4096;
    e.a_entry = 0x20000001;
    memcpy (raw, &e, sizeof e);
    for (i = 0; i < text + data; i++) {
        switch (class) {
        case 0: raw[sizeof e + i] = 0; break;
        case 1: raw[sizeof e + i] = rnd (); break;
        default:
            raw[sizeof e + i] = (i / 64) % 3 == 0 ? rnd () : (i * 7) & 0xff;
        }
    }
    *len = sizeof e + text + data;
    return raw;
}

static void
roundtrip (const unsigned char *raw, size_t rawlen, const char *name)
{
    unsigned char *p, *u;
    size_t plen, ulen;
    const char *err;
    struct exec e;
    struct hsx x;

    p = hsaout_pack (raw, rawlen, &plen, &err);
    CHECK (p != NULL, "%s: pack: %s", name, err);
    if (p == NULL)
        return;
    memcpy (&e, p, sizeof e);
    memcpy (&x, p + sizeof e, sizeof x);
    CHECK (N_GETFLAG (e) == EX_HSPACK && N_GETMAGIC (e) == OMAGIC, "%s: magic", name);
    CHECK (e.a_syms == 0 && e.a_reltext == 0 && e.a_reldata == 0, "%s: symbols", name);
    CHECK (plen == HSX_HDRSIZE + x.x_ctext + x.x_cdata, "%s: packed length", name);
    CHECK (hsx_check (&e, &x, plen) == HSX_OK, "%s: hsx_check", name);
    u = hsaout_unpack (p, plen, &ulen, &err);
    CHECK (u != NULL, "%s: unpack: %s", name, err);
    if (u != NULL) {
        memcpy (&e, raw, sizeof e);
        CHECK (ulen == sizeof e + e.a_text + e.a_data, "%s: unpacked length", name);
        CHECK (ulen == sizeof e + e.a_text + e.a_data &&
            memcmp (u + sizeof e, raw + sizeof e, e.a_text + e.a_data) == 0,
            "%s: bytes differ", name);
        CHECK (memcmp (u, raw, sizeof e - 4) == 0 ||
            memcmp (u + 4, raw + 4, 3 * 4) == 0, "%s: header", name);
    }
    free (u);
    free (p);
}

/* Recompute x_hdrcrc after a deliberate header edit. */
static void
reseal (unsigned char *p)
{
    struct exec e;
    struct hsx x;
    unsigned crc;

    memcpy (&e, p, sizeof e);
    memcpy (&x, p + sizeof e, sizeof x);
    x.x_hdrcrc = 0;
    crc = crc32_update (CRC32_INIT, &e, sizeof e);
    x.x_hdrcrc = CRC32_FIN (crc32_update (crc, &x, sizeof x));
    memcpy (p + sizeof e, &x, sizeof x);
}

static int
rejected (const unsigned char *p, size_t len)
{
    size_t ulen;
    const char *err;
    unsigned char *u = hsaout_unpack (p, len, &ulen, &err);

    free (u);
    return u == NULL;
}

struct chunkfail {
    const unsigned char *base;
    size_t len;
    int calls, failat, zeroat;
};

static int
failfill (void *ctx, unsigned off, void *buf, unsigned len)
{
    struct chunkfail *f = ctx;
    int call = f->calls++;

    if (call == f->failat)
        return -1;
    if (off > f->len || len > f->len - off)
        return -1;
    memcpy (buf, f->base + off, len);
    if (call == f->zeroat)
        memset (buf, 0, len);
    return 0;
}

static void
streams (void)
{
    unsigned char *raw, *p, *q;
    size_t rawlen, plen, i, b, cut;
    const char *err;
    struct exec e;
    struct hsx x;
    unsigned crc, rlen, clen, field;
    struct hsx_work *w = malloc (HSX_WORK_SIZE);
    struct chunkfail f;
    int r, k, nchunks;

    raw = image (3000, 300, 2, &rawlen);
    p = hsaout_pack (raw, rawlen, &plen, &err);
    CHECK (p != NULL, "stream image: %s", err);
    if (p == NULL)
        return;
    memcpy (&e, p, sizeof e);
    memcpy (&x, p + sizeof e, sizeof x);
    q = malloc (plen + 8);

    /* Every header bit. */
    for (b = 0; b < HSX_HDRSIZE * 8; b++) {
        memcpy (q, p, plen);
        q[b / 8] ^= 1 << (b % 8);
        CHECK (rejected (q, plen), "header bit %zu accepted", b);
    }
    /*
     * Every stream bit. LZSS is not a bijection: a flipped index bit can
     * name another copy of the same bytes, and the last byte's padding
     * bits mean nothing, so a flip is either refused or reproduces the
     * original bytes exactly. Anything else is a hole.
     */
    {
        size_t same = 0, refused = 0, ulen;
        unsigned char *u;
        for (b = HSX_HDRSIZE * 8; b < plen * 8; b++) {
            memcpy (q, p, plen);
            q[b / 8] ^= 1 << (b % 8);
            u = hsaout_unpack (q, plen, &ulen, &err);
            if (u == NULL)
                refused++;
            else if (ulen == rawlen && memcmp (u + sizeof e, raw + sizeof e,
                rawlen - sizeof e) == 0)
                same++;
            else
                CHECK (0, "stream bit %zu accepted with different bytes", b);
            free (u);
        }
        printf ("stream bits: %zu refused, %zu equivalent encodings\n", refused, same);
    }
    /* Every truncation, and every extension up to eight bytes. */
    for (cut = 0; cut < plen; cut++)
        CHECK (rejected (p, cut), "truncation to %zu accepted", cut);
    memcpy (q, p, plen);
    memset (q + plen, 0, 8);
    for (i = 1; i <= 8; i++)
        CHECK (rejected (q, plen + i), "%zu trailing bytes accepted", i);

    /* Forged lengths with the header CRC recomputed. */
    static const unsigned forged[] = { 0xffffffffU, 0xfffffffeU, 0x80000000U,
        0x7fffffffU, 0xffffffffU - 80, 0xffffffffU - 79, 0 };
    for (field = 0; field < 4; field++) {
        for (i = 0; i < sizeof forged / sizeof forged[0]; i++) {
            memcpy (q, p, plen);
            switch (field) {
            case 0: memcpy (q + 4, &forged[i], 4); break;          /* a_text */
            case 1: memcpy (q + 8, &forged[i], 4); break;          /* a_data */
            case 2: memcpy (q + sizeof e + 24, &forged[i], 4); break; /* x_ctext */
            default: memcpy (q + sizeof e + 28, &forged[i], 4); break; /* x_cdata */
            }
            reseal (q);
            CHECK (rejected (q, plen), "forged field %u = %#x accepted", field, forged[i]);
        }
    }
    /* Off-by-one expanded lengths: the exact-length rule both ways. */
    for (k = -2; k <= 2; k++) {
        if (k == 0)
            continue;
        memcpy (q, p, plen);
        rlen = e.a_text + k;
        memcpy (q + 4, &rlen, 4);
        reseal (q);
        CHECK (rejected (q, plen), "a_text %+d accepted", k);
        memcpy (q, p, plen);
        rlen = e.a_data + k;
        memcpy (q + 8, &rlen, 4);
        reseal (q);
        CHECK (rejected (q, plen), "a_data %+d accepted", k);
    }
    /* Stream boundary moved by one with the size kept: both streams shift. */
    for (k = -1; k <= 1; k += 2) {
        memcpy (q, p, plen);
        clen = x.x_ctext + k;
        memcpy (q + sizeof e + 24, &clen, 4);
        clen = x.x_cdata - k;
        memcpy (q + sizeof e + 28, &clen, 4);
        reseal (q);
        CHECK (rejected (q, plen), "boundary %+d accepted", k);
    }
    /* Wrong parameters and reserved bits, each resealed. */
    for (field = 0; field < 6; field++) {
        memcpy (q, p, plen);
        memcpy (&x, q + sizeof e, sizeof x);
        switch (field) {
        case 0: x.x_sig ^= 1; break;
        case 1: x.x_version = HSX_VERSION + 1; break;
        case 2: x.x_hdrlen = sizeof x + 4; break;
        case 3: x.x_codec = HSX_CODEC_HEATSHRINK + 1; break;
        case 4: x.x_window = HSX_WINDOW + 1; x.x_lookahead = HSX_LOOKAHEAD; break;
        default: x.x_reserved = 1; break;
        }
        memcpy (q + sizeof e, &x, sizeof x);
        reseal (q);
        CHECK (rejected (q, plen), "parameter %u accepted", field);
    }
    memcpy (q, p, plen);
    memcpy (&x, q + sizeof e, sizeof x);
    x.x_lookahead = HSX_LOOKAHEAD + 1;
    memcpy (q + sizeof e, &x, sizeof x);
    reseal (q);
    CHECK (rejected (q, plen), "lookahead accepted");
    /* The raw flag on the container, and the packed flag on a raw image. */
    memcpy (q, p, plen);
    memcpy (&e, q, sizeof e);
    N_SETMAGIC (e, OMAGIC, MID_ZERO, 0);
    memcpy (q, &e, sizeof e);
    reseal (q);
    CHECK (rejected (q, plen), "container without the flag accepted");
    memcpy (&e, p, sizeof e);
    {
        unsigned char *rp = malloc (rawlen);
        struct exec re;
        size_t n;
        memcpy (rp, raw, rawlen);
        memcpy (&re, rp, sizeof re);
        N_SETMAGIC (re, OMAGIC, MID_ZERO, EX_HSPACK);
        memcpy (rp, &re, sizeof re);
        CHECK (hsaout_pack (rp, rawlen, &n, &err) == NULL, "flagged raw packed");
        CHECK (rejected (rp, rawlen), "flagged raw unpacked");
        free (rp);
    }

    /* A fill that fails at every chunk, then one that returns zeros. */
    memcpy (&x, p + sizeof e, sizeof x);
    nchunks = (x.x_ctext + HSX_STAGE - 1) / HSX_STAGE;
    CHECK (nchunks >= 3, "text stream spans %d chunks, want 3 or more", nchunks);
    for (k = 0; k < nchunks; k++) {
        f.base = p; f.len = plen; f.calls = 0; f.failat = k; f.zeroat = -1;
        r = hsx_expand (w, failfill, &f, HSX_HDRSIZE, x.x_ctext, NULL, e.a_text, &crc);
        CHECK (r == -1, "fill failure at chunk %d accepted", k);
        f.calls = 0; f.failat = -1; f.zeroat = k;
        r = hsx_expand (w, failfill, &f, HSX_HDRSIZE, x.x_ctext, NULL, e.a_text, &crc);
        CHECK (r == -1 || crc != x.x_textcrc, "zeroed chunk %d accepted", k);
    }
    f.base = p; f.len = plen; f.calls = 0; f.failat = -1; f.zeroat = -1;
    r = hsx_expand (w, failfill, &f, HSX_HDRSIZE, x.x_ctext, NULL, e.a_text, &crc);
    CHECK (r == 0 && crc == x.x_textcrc && f.calls == nchunks,
        "clean expansion: r %d calls %d", r, f.calls);
    /* Into a destination as exec's second pass does. */
    {
        unsigned char *dst = malloc (e.a_text);
        f.calls = 0;
        r = hsx_expand (w, failfill, &f, HSX_HDRSIZE, x.x_ctext, dst, e.a_text, &crc);
        CHECK (r == 0 && crc == x.x_textcrc &&
            memcmp (dst, raw + sizeof e, e.a_text) == 0, "destination expansion");
        free (dst);
    }
    /* A stalled stream: packed bytes that never complete a token. */
    {
        static const unsigned char stall[] = { 0x00 };
        struct chunkfail s = { stall, sizeof stall, 0, -1, -1 };
        r = hsx_expand (w, failfill, &s, 0, sizeof stall, NULL, 0, &crc);
        printf ("note: a lone zero byte with rlen 0 %s\n",
            r == 0 ? "is accepted as padding" : "is refused");
        r = hsx_expand (w, failfill, &s, 0, sizeof stall, NULL, 1, &crc);
        CHECK (r == -1, "a lone zero byte yielded one byte");
    }
    free (w);
    free (q);
    free (p);
    free (raw);
}

static void
tree (const char *dir)
{
    DIR *d = opendir (dir);
    struct dirent *de;
    char path[4096];
    struct stat st;
    FILE *f;
    unsigned char *raw;
    struct exec e;
    int n = 0;

    if (d == NULL)
        return;
    while ((de = readdir (d)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        snprintf (path, sizeof path, "%s/%s", dir, de->d_name);
        if (stat (path, &st) != 0)
            continue;
        if (S_ISDIR (st.st_mode)) {
            tree (path);
            continue;
        }
        if (!S_ISREG (st.st_mode) || st.st_size < (off_t) sizeof e)
            continue;
        f = fopen (path, "rb");
        if (f == NULL)
            continue;
        raw = malloc (st.st_size);
        if (fread (raw, 1, st.st_size, f) == (size_t) st.st_size) {
            memcpy (&e, raw, sizeof e);
            if (N_GETMAGIC (e) == OMAGIC && N_GETFLAG (e) == 0 &&
                sizeof e + (size_t) e.a_text + e.a_data <= (size_t) st.st_size) {
                roundtrip (raw, st.st_size, path);
                n++;
            }
        }
        free (raw);
        fclose (f);
    }
    closedir (d);
    if (n)
        printf ("%s: %d a.out files round-tripped\n", dir, n);
}

int
main (int argc, char **argv)
{
    static const unsigned sizes[][2] = { {1, 0}, {1, 1}, {100, 0}, {0, 100},
        {4096, 512}, {60000, 3000}, {200000, 0} };
    unsigned char *raw;
    size_t rawlen, n;
    const char *err;
    unsigned i, class;
    char name[64];

    CHECK (CRC32_FIN (crc32_update (CRC32_INIT, "123456789", 9)) == 0xcbf43926U,
        "CRC-32 check value");
    CHECK (sizeof (struct hsx) == 48 && HSX_HDRSIZE == 80, "header sizes");
    CHECK (sizeof (struct hsx_work) <= HSX_WORK_SIZE, "work area");
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        for (class = 0; class < 3; class++) {
            raw = image (sizes[i][0], sizes[i][1], class, &rawlen);
            snprintf (name, sizeof name, "image %u+%u class %u", sizes[i][0],
                sizes[i][1], class);
            roundtrip (raw, rawlen, name);
            free (raw);
        }
    /* What the packer refuses. */
    raw = image (100, 10, 1, &rawlen);
    CHECK (hsaout_pack (raw, 20, &n, &err) == NULL, "short raw packed");
    CHECK (hsaout_pack (raw, rawlen - 1, &n, &err) == NULL, "truncated raw packed");
    raw[0] ^= 1;
    CHECK (hsaout_pack (raw, rawlen, &n, &err) == NULL, "bad magic packed");
    free (raw);
    streams ();
    if (argc > 1)
        tree (argv[1]);
    printf ("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
