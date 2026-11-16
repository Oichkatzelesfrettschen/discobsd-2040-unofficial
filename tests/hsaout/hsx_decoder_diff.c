/*
 * Differential test of the port's heatshrink decoder (hsx_decoder.c, with
 * the loop in hsx_stream.c) against the vendored heatshrink decoder, which
 * the test builds with dynamic allocation so one reference instance serves
 * every lookahead. Two interfaces are compared, each with the loop that
 * used the vendored decoder before the port-owned one replaced it:
 *
 *   exec  hsx_expand against ref_expand, lookahead 3, into a buffer and
 *         into the discard path. Success needs every byte consumed, exactly
 *         rlen bytes out and no token past them; the status, the bytes and
 *         the CRC-32 must agree.
 *   swap  hsx_dec_run against ref_swap, any lookahead from 3 to 8: decode
 *         up to rlen bytes and return how many came out. Run in one call,
 *         then again with the input cut at random points and the output
 *         requested in random pieces through the linear and the ring
 *         conventions.
 *
 * The inputs are every OMAGIC a.out under the directory named on the
 * command line (the distribution tree), random data of many sizes and
 * compressibilities packed by the vendored encoder, every truncation point
 * of several streams, and every single-bit flip of several streams plus
 * random multi-byte damage and pure noise. Each case runs with the stream
 * placed against a protected page on one side and the destination against
 * a protected page on the other, so a read past the input or a write
 * outside the destination faults. Options: -q trims the sweeps, -s exits at
 * the first failure. The mutant runner (hsx_decoder_mutants.py) compiles
 * this file against broken copies of the decoder and requires a failure
 * from each.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/exec_hsaout.h>
#include <machine/hsx_decoder.h>
#include "heatshrink_encoder.h"
#include "heatshrink_decoder.h"

static unsigned long checks, fails, cases;
static int quick, stopfirst;

#define CHECK(cond, ...) do { \
        checks++; \
        if (!(cond)) { \
            if (fails++ < 25) { \
                printf ("FAIL %s:%d: ", __FILE__, __LINE__); \
                printf (__VA_ARGS__); \
                printf ("\n"); \
            } \
            if (stopfirst) \
                exit (1); \
        } \
    } while (0)

static uint32_t rstate = 0x9e3779b9u;

/* hsx_stream's ring runs must keep both decoder writes and CRC reads inside
 * the contiguous suffix declared by hsx_decoder.h, even when an overflow into
 * consumed staging bytes would preserve the output and its CRC. */
unsigned
hsx_checked_run (struct hsx_dec *d, unsigned char *base, unsigned mask,
    unsigned room)
{
    if (mask == HSXD_WINDOW - 1) {
        if (base != d->win || room > HSXD_WINDOW - (d->pos & mask)) {
            CHECK (0, "history ring run crosses end: position %u room %u",
                d->pos, room);
            return 0;
        }
    }
    return hsx_dec_run (d, base, mask, room);
}

static unsigned
rnd (void)
{
    rstate ^= rstate << 13;
    rstate ^= rstate >> 17;
    rstate ^= rstate << 5;
    return rstate;
}

/* ---- guarded buffers ------------------------------------------------ */

/*
 * A buffer of len bytes with a PROT_NONE page directly after it (atend) or
 * directly before it, so an access just outside faults. Slots are mapped
 * once per power-of-two size and reused, with a guard page on both sides
 * of the body.
 */
#define NCLASSES    26
#define NSLOTS      8

struct slot {
    unsigned char *map;
    size_t body;
    int busy;
};

static struct slot slots[NCLASSES][NSLOTS];

struct gbuf {
    unsigned char *p;
    struct slot *slot;
};

static void
gnew (struct gbuf *g, size_t len, int atend)
{
    size_t page = (size_t) sysconf (_SC_PAGESIZE), body = page;
    unsigned cls = 0, i;
    struct slot *s;

    while (body < len) {
        body <<= 1;
        cls++;
    }
    for (i = 0; i < NSLOTS && slots[cls][i].busy; i++)
        ;
    if (i == NSLOTS) {
        fprintf (stderr, "out of guarded slots\n");
        exit (2);
    }
    s = &slots[cls][i];
    if (s->map == NULL) {
        s->map = mmap (NULL, body + 2 * page, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (s->map == MAP_FAILED || mprotect (s->map, page, PROT_NONE) != 0 ||
            mprotect (s->map + page + body, page, PROT_NONE) != 0) {
            perror ("guard page");
            exit (2);
        }
        s->body = body;
    }
    s->busy = 1;
    g->slot = s;
    g->p = atend ? s->map + page + body - len : s->map + page;
}

static void
gfree (struct gbuf *g)
{
    g->slot->busy = 0;
}

/* ---- vendored encoder and reference decoders ------------------------ */

/* Pack len bytes with window 9 and the given lookahead; malloc'd result. */
static unsigned char *
encode (const unsigned char *src, size_t len, unsigned la, size_t *outlen)
{
    heatshrink_encoder *enc = heatshrink_encoder_alloc (9, la);
    size_t cap = 2 * len + 64, in = 0, done = 0, n;
    unsigned char *out = malloc (cap);
    HSE_poll_res pres;

    if (enc == NULL || out == NULL)
        exit (2);
    while (in < len) {
        if (heatshrink_encoder_sink (enc, (uint8_t *) src + in, len - in, &n) < 0)
            exit (2);
        in += n;
        do {
            pres = heatshrink_encoder_poll (enc, out + done, cap - done, &n);
            if (pres < 0)
                exit (2);
            done += n;
        } while (pres == HSER_POLL_MORE);
    }
    while (heatshrink_encoder_finish (enc) == HSER_FINISH_MORE) {
        do {
            pres = heatshrink_encoder_poll (enc, out + done, cap - done, &n);
            if (pres < 0)
                exit (2);
            done += n;
        } while (pres == HSER_POLL_MORE);
    }
    heatshrink_encoder_free (enc);
    *outlen = done;
    return out;
}

/*
 * The expansion loop hsx_stream.c ran when the vendored decoder sat under
 * it, with the staging chunk size as a parameter. dst null discards.
 */
static int
ref_expand (const unsigned char *in, unsigned clen, unsigned stagelen,
    unsigned char *dst, unsigned rlen, unsigned *crc)
{
    heatshrink_decoder *hsd = heatshrink_decoder_alloc (64, 9, HSX_LOOKAHEAD);
    unsigned char probe[64];
    unsigned used = 0, out = 0, avail = 0, pos = 0, c = CRC32_INIT;
    const unsigned char *stage = in;
    unsigned char *buf;
    size_t n, room;
    int progress, r = -1;
    HSD_poll_res pres;
    HSD_finish_res fres;

#define DRAIN() do { \
        do { \
            if (dst != 0 && out < rlen) { \
                buf = dst + out; \
                room = rlen - out; \
            } else { \
                buf = probe; \
                room = sizeof probe; \
            } \
            pres = heatshrink_decoder_poll (hsd, buf, room, &n); \
            if (pres < 0) \
                goto done; \
            if (n != 0) { \
                if (n > rlen - out) \
                    goto done; \
                c = crc32_update (c, buf, (unsigned) n); \
                out += (unsigned) n; \
                progress = 1; \
            } \
        } while (pres == HSDR_POLL_MORE); \
    } while (0)

    if (hsd == NULL)
        exit (2);
    while (used < clen) {
        if (pos == avail) {
            avail = clen - used;
            if (avail > stagelen)
                avail = stagelen;
            stage = in + used;
            pos = 0;
        }
        if (heatshrink_decoder_sink (hsd, (uint8_t *) stage + pos,
            avail - pos, &n) < 0)
            goto done;
        pos += (unsigned) n;
        used += (unsigned) n;
        progress = n != 0;
        DRAIN ();
        if (!progress)
            goto done;
    }
    for (;;) {
        fres = heatshrink_decoder_finish (hsd);
        if (fres < 0)
            goto done;
        if (fres == HSDR_FINISH_DONE)
            break;
        progress = 0;
        DRAIN ();
        if (!progress)
            goto done;
    }
    if (out != rlen)
        goto done;
    *crc = CRC32_FIN (c);
    r = 0;
done:
#undef DRAIN
    heatshrink_decoder_free (hsd);
    return r;
}

/*
 * The swap tier's expansion of a pool-resident stream, as swapram.c ran it
 * on the vendored decoder with a destination buffer: decode until rlen
 * bytes are out or the input ends, ignore what follows. Returns the bytes
 * produced, or -1.
 */
static long
ref_swap (const unsigned char *in, unsigned clen, unsigned la,
    unsigned char *dst, unsigned rlen)
{
    heatshrink_decoder *hsd = heatshrink_decoder_alloc (64, 9, la);
    unsigned used = 0, out = 0, moved = 0;
    size_t n, room;
    HSD_poll_res pres;
    HSD_finish_res fres;
    long r = -1;

    if (hsd == NULL)
        exit (2);
    while (used < clen && out < rlen) {
        if (heatshrink_decoder_sink (hsd, (uint8_t *) in + used, clen - used,
            &n) < 0)
            goto done;
        if (n == 0 && out == moved)
            goto done;
        used += (unsigned) n;
        moved = out;
        do {
            if (out == rlen)
                break;
            room = rlen - out;
            pres = heatshrink_decoder_poll (hsd, dst + out, room, &n);
            if (pres < 0)
                goto done;
            out += (unsigned) n;
        } while (pres == HSDR_POLL_MORE);
    }
    for (moved = out + 1; out < rlen; moved = out) {
        if (out == moved)
            goto done;
        fres = heatshrink_decoder_finish (hsd);
        if (fres < 0)
            goto done;
        if (fres == HSDR_FINISH_DONE)
            break;
        do {
            if (out == rlen)
                break;
            room = rlen - out;
            pres = heatshrink_decoder_poll (hsd, dst + out, room, &n);
            if (pres < 0)
                goto done;
            out += (unsigned) n;
        } while (pres == HSDR_POLL_MORE);
    }
    r = out;
done:
    heatshrink_decoder_free (hsd);
    return r;
}

/* ---- the port decoder under test ------------------------------------ */

static int
memfill (void *ctx, unsigned off, void *buf, unsigned len)
{
    memcpy (buf, (const unsigned char *) ctx + off, len);
    return 0;
}

/*
 * A decoder whose history ring holds nonzero bytes, as a kernel ring holds
 * whatever the previous image left in it. hsx_dec_init() sets only the
 * scalar fields, so a decode that reads history before its first byte
 * instead of producing zeros reads these bytes and differs from the
 * reference, whatever memory the allocator returns.
 */
static struct hsx_dec *
new_dec (void)
{
    struct hsx_dec *d = malloc (sizeof *d);

    if (d == NULL)
        abort ();
    memset (d, 0x5a, sizeof *d);
    return d;
}

/* Swap-style, one call: linear destination of rlen bytes. */
static long
new_swap (const unsigned char *in, unsigned clen, unsigned la,
    unsigned char *dst, unsigned rlen)
{
    struct hsx_dec *d = new_dec ();
    long n;

    hsx_dec_init (d, la);
    hsx_dec_input (d, in, clen);
    n = hsx_dec_run (d, dst, ~0u, rlen);
    free (d);
    return n;
}

/*
 * Swap-style, the input cut at random points and the output taken in
 * random pieces. ring selects the decoder's own history ring, copied out
 * after each run, the convention the block-wise swap path uses; otherwise
 * the destination is linear.
 */
static long
new_chunked (const unsigned char *in, unsigned clen, unsigned la,
    unsigned char *dst, unsigned rlen, int ring)
{
    struct hsx_dec *d = new_dec ();
    unsigned used = 0, out = 0, cut, room, n, lim, r;
    unsigned char *base;
    unsigned mask;

    hsx_dec_init (d, la);
    while (used < clen && out < rlen) {
        cut = 1 + rnd () % (rnd () % 4 == 0 ? 700 : 24);
        if (cut > clen - used)
            cut = clen - used;
        hsx_dec_input (d, in + used, cut);
        used += cut;
        do {
            room = rlen - out;
            r = 1 + rnd () % (rnd () % 3 == 0 ? 300 : 20);
            if (room > r)
                room = r;
            if (ring) {
                base = d->win;
                mask = HSXD_WINDOW - 1;
                lim = HSXD_WINDOW - (out & mask);
                if (room > lim)
                    room = lim;
            } else {
                base = dst;
                mask = ~0u;
            }
            n = hsx_dec_run (d, base, mask, room);
            if (ring)
                memcpy (dst + out, base + (out & mask), n);
            out += n;
        } while (n == room && out < rlen);
    }
    free (d);
    return out;
}

/* ---- comparisons ---------------------------------------------------- */

/* A stream and the destination size to try, on guarded memory. */
static unsigned placement;

static void
compare_exec (const char *what, const unsigned char *stream, unsigned clen,
    unsigned rlen)
{
    struct hsx_work *w = malloc (HSX_WORK_SIZE);
    struct gbuf gs, gr, gn;
    unsigned crc_r = 0, crc_n = 0;
    int r1, r2, r3, r4;

    placement++;
    gnew (&gs, clen, placement & 1);
    memcpy (gs.p, stream, clen);
    gnew (&gr, rlen, !(placement & 1));
    gnew (&gn, rlen, !(placement & 1));
    memset (gr.p, 0xa5, rlen);
    memset (gn.p, 0xa5, rlen);

    r1 = ref_expand (gs.p, clen, HSX_STAGE, gr.p, rlen, &crc_r);
    r2 = hsx_expand (w, memfill, gs.p, 0, clen, gn.p, rlen, &crc_n);
    CHECK (r1 == r2, "%s: exec status ref %d new %d (clen %u rlen %u)", what,
        r1, r2, clen, rlen);
    if (r1 == 0 && r2 == 0) {
        CHECK (crc_r == crc_n, "%s: exec crc %08x vs %08x", what, crc_r, crc_n);
        CHECK (memcmp (gr.p, gn.p, rlen) == 0, "%s: exec bytes differ", what);
    }
    r3 = ref_expand (gs.p, clen, HSX_STAGE, NULL, rlen, &crc_r);
    r4 = hsx_expand (w, memfill, gs.p, 0, clen, NULL, rlen, &crc_n);
    CHECK (r3 == r4, "%s: discard status ref %d new %d (clen %u rlen %u)", what,
        r3, r4, clen, rlen);
    CHECK (r3 == r1, "%s: reference discard %d differs from buffer %d", what,
        r3, r1);
    if (r3 == 0 && r4 == 0)
        CHECK (crc_r == crc_n, "%s: discard crc %08x vs %08x", what, crc_r,
            crc_n);
    cases++;
    gfree (&gs);
    gfree (&gr);
    gfree (&gn);
    free (w);
}

static void
compare_swap (const char *what, const unsigned char *stream, unsigned clen,
    unsigned la, unsigned rlen)
{
    struct gbuf gs, gr, g1, g2, g3;
    long r, n1, n2, n3;

    placement++;
    gnew (&gs, clen, placement & 1);
    memcpy (gs.p, stream, clen);
    gnew (&gr, rlen, !(placement & 1));
    gnew (&g1, rlen, !(placement & 1));
    gnew (&g2, rlen, !(placement & 1));
    gnew (&g3, rlen, !(placement & 1));
    memset (gr.p, 0xa5, rlen);
    memset (g1.p, 0xa5, rlen);
    memset (g2.p, 0xa5, rlen);
    memset (g3.p, 0xa5, rlen);

    r = ref_swap (gs.p, clen, la, gr.p, rlen);
    n1 = new_swap (gs.p, clen, la, g1.p, rlen);
    n2 = new_chunked (gs.p, clen, la, g2.p, rlen, 0);
    n3 = new_chunked (gs.p, clen, la, g3.p, rlen, 1);
    CHECK (r == n1, "%s: swap length ref %ld new %ld (clen %u la %u rlen %u)",
        what, r, n1, clen, la, rlen);
    CHECK (r == n2, "%s: chunked linear length ref %ld new %ld (la %u)", what,
        r, n2, la);
    CHECK (r == n3, "%s: chunked ring length ref %ld new %ld (la %u)", what,
        r, n3, la);
    if (r >= 0 && r == n1)
        CHECK (memcmp (gr.p, g1.p, (size_t) r) == 0, "%s: swap bytes differ",
            what);
    if (r >= 0 && r == n2)
        CHECK (memcmp (gr.p, g2.p, (size_t) r) == 0,
            "%s: chunked linear bytes differ", what);
    if (r >= 0 && r == n3)
        CHECK (memcmp (gr.p, g3.p, (size_t) r) == 0,
            "%s: chunked ring bytes differ", what);
    cases++;
    gfree (&gs);
    gfree (&gr);
    gfree (&g1);
    gfree (&g2);
    gfree (&g3);
}

/*
 * The number of bytes the vendored decoder yields from the whole stream
 * when given unlimited room: the one destination size at which a damaged
 * stream can still be accepted.
 */
static unsigned
natural (const unsigned char *stream, unsigned clen, unsigned la)
{
    size_t cap = (size_t) clen * 130 + 64;
    unsigned char *tmp = malloc (cap);
    long n = ref_swap (stream, clen, la, tmp, (unsigned) cap);

    free (tmp);
    if (n < 0)
        exit (2);
    return (unsigned) n;
}

/* One stream against the destination sizes that matter. */
static void
sweep (const char *what, const unsigned char *stream, unsigned clen,
    unsigned la, unsigned want)
{
    unsigned nat = natural (stream, clen, la);
    unsigned sizes[6], i, ns = 0;

    sizes[ns++] = nat;
    sizes[ns++] = want;
    if (nat > 0)
        sizes[ns++] = nat - 1;
    sizes[ns++] = nat + 1;
    if (nat > 2)
        sizes[ns++] = nat / 2;
    for (i = 0; i < ns; i++) {
        if (la == HSX_LOOKAHEAD)
            compare_exec (what, stream, clen, sizes[i]);
        compare_swap (what, stream, clen, la, sizes[i]);
    }
}

/* ---- inputs --------------------------------------------------------- */

static void
fill_class (unsigned char *b, size_t n, unsigned cls)
{
    size_t i, run, p;
    unsigned period, k, alpha;
    static const char *words[] = { "the ", "kernel ", "of ", "and ", "swap ",
        "heatshrink ", "block ", "\n", "return ", "int ", "0x00 " };

    switch (cls) {
    case 0:
        memset (b, 0, n);
        break;
    case 1:
        memset (b, 0xaa, n);
        break;
    case 2:
        for (i = 0; i < n; i++)
            b[i] = (unsigned char) rnd ();
        break;
    case 3: case 4: case 5:
        alpha = cls == 3 ? 2 : cls == 4 ? 4 : 16;
        for (i = 0; i < n; i++)
            b[i] = (unsigned char) (rnd () % alpha);
        break;
    case 6:
        for (i = 0; i < n; ) {
            run = 1 + rnd () % 40;
            k = rnd ();
            while (run-- && i < n)
                b[i++] = (unsigned char) k;
        }
        break;
    case 7:
        period = 1 + rnd () % 700;
        for (i = 0; i < n; i++)
            b[i] = i < period ? (unsigned char) rnd () : b[i - period];
        break;
    case 8:
        for (i = 0; i < n; ) {
            const char *s = words[rnd () % 11];
            for (p = 0; s[p] && i < n; p++)
                b[i++] = (unsigned char) s[p];
        }
        break;
    case 9:
        for (i = 0; i < n; ) {
            run = 1 + rnd () % 300;
            if (run > n - i)
                run = n - i;
            fill_class (b + i, run, rnd () % 9);
            i += run;
        }
        break;
    default:
        for (i = 0; i + 1 < n; i += 2) {
            k = rnd () % 24;
            b[i] = (unsigned char) (k * 11);
            b[i + 1] = (unsigned char) (0x40 + (k & 7));
        }
        if (i < n)
            b[i] = 0;
        break;
    }
}
#define NCLASS 11

/* Round trip one raw buffer at one lookahead and compare the decoders. */
static void
roundtrip (const char *what, const unsigned char *raw, size_t rawlen,
    unsigned la)
{
    size_t clen;
    unsigned char *pk = encode (raw, rawlen, la, &clen);
    struct gbuf gd;
    long n;

    placement++;
    gnew (&gd, rawlen, placement & 1);
    n = new_swap (pk, (unsigned) clen, la, gd.p, (unsigned) rawlen);
    CHECK (n == (long) rawlen && memcmp (gd.p, raw, rawlen) == 0,
        "%s: new decoder does not reproduce the input (la %u, %zu bytes)",
        what, la, rawlen);
    gfree (&gd);
    sweep (what, pk, (unsigned) clen, la, (unsigned) rawlen);
    free (pk);
}

static void
random_data (void)
{
    static const unsigned sizes[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17,
        31, 33, 63, 64, 65, 100, 255, 256, 257, 511, 512, 513, 600, 1000,
        1023, 1024, 1025, 2047, 4096, 5000, 20000, 70000 };
    unsigned i, cls, la, rep;
    unsigned char *raw;
    char name[64];

    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        if (quick && sizes[i] > 5000)
            continue;
        for (cls = 0; cls < NCLASS; cls++) {
            for (rep = 0; rep < (sizes[i] > 5000 ? 1u : 2u); rep++) {
                raw = malloc (sizes[i] + 1);
                fill_class (raw, sizes[i], cls);
                snprintf (name, sizeof name, "random %u class %u", sizes[i],
                    cls);
                for (la = 3; la <= 8; la++)
                    roundtrip (name, raw, sizes[i], la);
                free (raw);
            }
        }
    }
}

static void
aouts (const char *dir, unsigned long *nfiles)
{
    DIR *d = opendir (dir);
    struct dirent *de;
    char path[4096];
    struct stat st;
    FILE *f;
    unsigned char *raw;
    struct exec e;

    if (d == NULL)
        return;
    while ((de = readdir (d)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        snprintf (path, sizeof path, "%s/%s", dir, de->d_name);
        if (lstat (path, &st) != 0)
            continue;
        if (S_ISDIR (st.st_mode)) {
            aouts (path, nfiles);
            continue;
        }
        if (!S_ISREG (st.st_mode) || st.st_size < (off_t) sizeof e)
            continue;
        f = fopen (path, "rb");
        if (f == NULL)
            continue;
        raw = malloc ((size_t) st.st_size);
        if (raw != NULL && fread (raw, 1, (size_t) st.st_size, f) ==
            (size_t) st.st_size) {
            memcpy (&e, raw, sizeof e);
            if (N_GETMAGIC (e) == OMAGIC && N_GETFLAG (e) == 0 &&
                sizeof e + (size_t) e.a_text + e.a_data <=
                (size_t) st.st_size) {
                /* the container's text stream, data stream, and swap-style
                   lookahead over the whole image */
                roundtrip (path, raw + sizeof e, e.a_text, HSX_LOOKAHEAD);
                roundtrip (path, raw + sizeof e + e.a_text, e.a_data,
                    HSX_LOOKAHEAD);
                roundtrip (path, raw + sizeof e, e.a_text + e.a_data, 8);
                (*nfiles)++;
            }
        }
        free (raw);
        fclose (f);
    }
    closedir (d);
}

/* Every prefix of a few streams. */
static void
truncations (void)
{
    static const unsigned spec[][3] = {   /* size, class, lookahead */
        { 700, 9, 3 }, { 1500, 10, 3 }, { 900, 8, 3 }, { 1200, 7, 8 },
        { 600, 6, 5 }, { 400, 0, 8 } };
    unsigned s, t;
    size_t clen;
    unsigned char *raw, *pk;
    char name[64];

    for (s = 0; s < sizeof spec / sizeof spec[0]; s++) {
        raw = malloc (spec[s][0]);
        fill_class (raw, spec[s][0], spec[s][1]);
        pk = encode (raw, spec[s][0], spec[s][2], &clen);
        for (t = 0; t <= clen; t++) {
            snprintf (name, sizeof name, "truncate %u/%zu stream %u", t, clen, s);
            sweep (name, pk, t, spec[s][2], spec[s][0]);
        }
        free (pk);
        free (raw);
    }
}

/* Every single-bit flip of a few streams, then random damage and noise. */
static void
corruptions (void)
{
    static const unsigned spec[][3] = {
        { 300, 9, 3 }, { 500, 10, 3 }, { 400, 8, 3 }, { 300, 7, 8 },
        { 250, 6, 4 } };
    unsigned s, bit, k, nflip, i;
    size_t clen;
    unsigned char *raw, *pk, *bad;
    char name[64];

    for (s = 0; s < sizeof spec / sizeof spec[0]; s++) {
        raw = malloc (spec[s][0]);
        fill_class (raw, spec[s][0], spec[s][1]);
        pk = encode (raw, spec[s][0], spec[s][2], &clen);
        bad = malloc (clen + 8);
        for (bit = 0; bit < clen * 8; bit++) {
            memcpy (bad, pk, clen);
            bad[bit / 8] ^= (unsigned char) (0x80 >> (bit % 8));
            snprintf (name, sizeof name, "flip bit %u stream %u", bit, s);
            sweep (name, bad, (unsigned) clen, spec[s][2], spec[s][0]);
        }
        for (k = 0; k < (quick ? 200u : 2000u); k++) {
            memcpy (bad, pk, clen);
            nflip = 1 + rnd () % 6;
            for (i = 0; i < nflip; i++)
                bad[rnd () % clen] = (unsigned char) rnd ();
            snprintf (name, sizeof name, "damage %u stream %u", k, s);
            sweep (name, bad, (unsigned) clen, spec[s][2], spec[s][0]);
        }
        free (bad);
        free (pk);
        free (raw);
    }
    /* Noise: no encoder produced these. */
    bad = malloc (3000);
    for (k = 0; k < (quick ? 500u : 5000u); k++) {
        clen = rnd () % 2500;
        for (i = 0; i < clen; i++)
            bad[i] = (unsigned char) (rnd () % 5 == 0 ? 0 : rnd ());
        snprintf (name, sizeof name, "noise %u", k);
        sweep (name, bad, (unsigned) clen, 3 + k % 6, rnd () % 4000);
    }
    free (bad);
}

/*
 * Window edges: back-references of every distance 1..512 at positions on
 * both sides of the window and ring wrap points, built bit by bit rather
 * than by the encoder, so distance 512, reaches before the first byte and
 * counts of every size are all present.
 */
struct bits {
    unsigned char buf[8192];
    unsigned n;
};

static void
put (struct bits *b, unsigned val, unsigned width)
{
    while (width--) {
        if (b->n % 8 == 0)
            b->buf[b->n / 8] = 0;
        if ((val >> width) & 1)
            b->buf[b->n / 8] |= (unsigned char) (0x80 >> (b->n % 8));
        b->n++;
    }
}

static void
ring_limits (void)
{
    struct bits *b = malloc (sizeof *b);
    unsigned extra, i;
    char name[64];

    if (b == NULL)
        exit (2);
    for (extra = 1; extra <= 8; extra++) {
        b->n = 0;
        for (i = 0; i < HSXD_WINDOW + extra; i++)
            put (b, 256 | (i & 255), 9);
        snprintf (name, sizeof name, "ring boundary plus %u", extra);
        compare_exec (name, b->buf, (b->n + 7) / 8, HSXD_WINDOW + extra);
    }
    free (b);
}

static void
handmade (void)
{
    struct bits *b = malloc (sizeof *b);
    unsigned la, trial, k, dist, cnt, ntok, lits;
    char name[64];

    for (la = 3; la <= 8; la++) {
        for (trial = 0; trial < (quick ? 150u : 1500u); trial++) {
            b->n = 0;
            lits = rnd () % 700;
            for (k = 0; k < lits; k++) {
                put (b, 1, 1);
                put (b, rnd () & 255, 8);
            }
            ntok = 1 + rnd () % 12;
            for (k = 0; k < ntok; k++) {
                dist = rnd () % 4 == 0 ? 511 - rnd () % 3 : rnd () % 512;
                cnt = rnd () % (1u << la);
                if (rnd () % 3 == 0) {
                    put (b, 1, 1);
                    put (b, rnd () & 255, 8);
                }
                put (b, 0, 1);
                put (b, dist, 9);
                put (b, cnt, la);
            }
            for (k = rnd () % 12; k > 0; k--)
                put (b, rnd () & 1, 1);
            snprintf (name, sizeof name, "handmade la %u trial %u", la, trial);
            sweep (name, b->buf, (b->n + 7) / 8, la, 0);
        }
    }
    free (b);
}

int
main (int argc, char **argv)
{
    unsigned long nfiles = 0;
    int i, dirs = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp (argv[i], "-q") == 0)
            quick = 1;
        else if (strcmp (argv[i], "-s") == 0)
            stopfirst = 1;
        else
            dirs++;
    }
    ring_limits ();
    for (i = 1; i < argc; i++)
        if (strcmp (argv[i], "-q") != 0 && strcmp (argv[i], "-s") != 0)
            aouts (argv[i], &nfiles);
    CHECK (dirs == 0 || nfiles != 0, "no a.out files under the named directories");
    CHECK (sizeof (struct hsx_work) <= HSX_WORK_SIZE, "work area");
    random_data ();
    truncations ();
    corruptions ();
    handmade ();
    printf ("hsx_decoder_diff: %lu a.out files, %lu cases, %lu checks, "
        "%lu failures\n", nfiles, cases, checks, fails);
    return fails != 0;
}
