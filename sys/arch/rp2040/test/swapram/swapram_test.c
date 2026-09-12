/*
 * Host test for the compressed swap tier: the heatshrink round trip over a
 * real process image and the pool allocator's invariants.
 *
 * The image comes from an a.out built for the board. kern/exec_subr.c gives
 * a process one contiguous data area of a_text + a_data + a_bss at
 * USER_DATA_START and a stack at the top of the user window; kern/vm_swap.c
 * skips the clean text and swaps the rest, so the bytes measured here are
 * a_data from the file, a_bss zeros, the stack, and the USIZE u area.
 *
 * Usage: swapram_test <a.out> [stack bytes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "heatshrink_encoder.h"
#include "heatshrink_decoder.h"
#include <machine/swapram.h>

#define USIZE           3072            /* machparam.h */
#define SSIZE           2048            /* machparam.h, initial stack */

static heatshrink_encoder hse;
static heatshrink_decoder hsd;

/*
 * Compress src into dst, refusing to write past cap. Returns the compressed
 * length, or -1 when the output does not fit. This is the same loop
 * swapram.c runs in the kernel, kept in step by review rather than by
 * sharing a translation unit, because the kernel copy takes kernel types.
 */
static long
compress(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    size_t in = 0, out = 0, moved = 0, n;
    HSE_poll_res pres;
    HSE_finish_res fres;

    heatshrink_encoder_reset(&hse);
    while (in < len) {
        if (heatshrink_encoder_sink(&hse, (uint8_t *)src + in, len - in, &n)
            < 0)
            return -1;
        if (n == 0 && out == moved)
            return -1;          /* neither side advanced: no termination */
        in += n;
        moved = out;
        do {
            if (out == cap)
                return -1;
            pres = heatshrink_encoder_poll(&hse, dst + out, cap - out, &n);
            if (pres < 0)
                return -1;
            out += n;
        } while (pres == HSER_POLL_MORE);
    }
    for (;;) {
        fres = heatshrink_encoder_finish(&hse);
        if (fres < 0)
            return -1;
        if (fres == HSER_FINISH_DONE)
            break;
        do {
            if (out == cap)
                return -1;
            pres = heatshrink_encoder_poll(&hse, dst + out, cap - out, &n);
            if (pres < 0)
                return -1;
            out += n;
        } while (pres == HSER_POLL_MORE);
    }
    return (long)out;
}

static long
decompress(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    size_t in = 0, out = 0, moved = 0, n;
    HSD_poll_res pres;
    HSD_finish_res fres;

    heatshrink_decoder_reset(&hsd);
    while (in < len && out < cap) {
        if (heatshrink_decoder_sink(&hsd, (uint8_t *)src + in, len - in, &n)
            < 0)
            return -1;
        if (n == 0 && out == moved)
            return -1;          /* neither side advanced: no termination */
        in += n;
        moved = out;
        do {
            if (out == cap)
                return -1;
            pres = heatshrink_decoder_poll(&hsd, dst + out, cap - out, &n);
            if (pres < 0)
                return -1;
            out += n;
        } while (pres == HSDR_POLL_MORE);
    }
    for (moved = out + 1; out < cap; moved = out) {
        if (out == moved)
            return -1;          /* finish says more and poll yields none */
        fres = heatshrink_decoder_finish(&hsd);
        if (fres < 0)
            return -1;
        if (fres == HSDR_FINISH_DONE)
            break;
        do {
            if (out == cap)
                return -1;
            pres = heatshrink_decoder_poll(&hsd, dst + out, cap - out, &n);
            if (pres < 0)
                return -1;
            out += n;
        } while (pres == HSDR_POLL_MORE);
    }
    return (long)out;
}

static double
now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double
zerofrac(const uint8_t *p, size_t n)
{
    size_t i, z = 0;

    for (i = 0; i < n; i++)
        if (p[i] == 0)
            z++;
    return n ? (double)z / n : 0.0;
}

/*
 * Compress a segment, time it over enough repeats to beat the clock's
 * resolution, verify the round trip byte for byte, and print the row.
 */
static int
measure(const char *name, const uint8_t *src, size_t len, long *clenp)
{
    size_t cap = len + len / 8 + 64;
    uint8_t *dst = malloc(cap), *back = malloc(len + 1);
    long clen, blen;
    double t0, dt;
    int reps = 1, i;

    if (dst == NULL || back == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    clen = compress(src, len, dst, cap);
    if (clen < 0) {
        printf("%-6s %7zu  INCOMPRESSIBLE (exceeds %zu)\n", name, len, cap);
        free(dst);
        free(back);
        return 1;
    }
    while (reps < 4096) {
        t0 = now();
        for (i = 0; i < reps; i++)
            (void)compress(src, len, dst, cap);
        dt = now() - t0;
        if (dt > 0.2)
            break;
        reps *= 4;
    }
    blen = decompress(dst, (size_t)clen, back, len + 1);
    if (blen != (long)len || memcmp(back, src, len) != 0) {
        printf("%-6s ROUND TRIP FAILED (%ld back from %zu)\n", name, blen,
            len);
        free(dst);
        free(back);
        return 1;
    }
    printf("%-6s %7zu %7ld  %5.2fx  zeros %4.1f%%  %7.1f KB/s enc\n",
        name, len, clen, (double)len / clen, 100.0 * zerofrac(src, len),
        (len / 1024.0) * reps / dt);
    *clenp = clen;
    free(dst);
    free(back);
    return 0;
}

/* a.out header, sys/sys/exec_aout.h. */
struct aout {
    uint32_t a_midmag, a_text, a_data, a_bss, a_reltext, a_reldata,
        a_syms, a_entry;
};

/*
 * Pool allocator checks. The tier's live pattern is reserve worst case,
 * trim to the compressed length, free on swapin, so the cases below are
 * that sequence plus the fragmentation and misuse it must survive.
 */
static int
pooltest(void)
{
    struct swapram_seg seg[8];
    struct swapram_pool pool;
    unsigned char arena[4096];
    unsigned int a, b, c;
    int fail = 0;

#define CHECK(cond) do { if (!(cond)) { \
        printf("pool: FAIL %s line %d\n", #cond, __LINE__); fail = 1; } \
    } while (0)

    swapram_pool_init(&pool, arena, sizeof arena, seg, 8);
    CHECK(swapram_pool_avail(&pool) == 4096);

    /* Reserve, trim, and the freed tail returns to the pool. */
    CHECK(swapram_pool_alloc(&pool, 1000, &a) == 0);
    CHECK(a == 0);
    CHECK(swapram_pool_avail(&pool) == 4096 - 1000);
    CHECK(swapram_pool_trim(&pool, a, 1000, 200) == 0);
    CHECK(swapram_pool_avail(&pool) == 4096 - 200);

    /* A second image lands in the coalesced remainder. */
    CHECK(swapram_pool_alloc(&pool, 100, &b) == 0);
    CHECK(b == 200);
    CHECK(swapram_pool_alloc(&pool, 100, &c) == 0);
    CHECK(c == 300);

    /* Freeing the middle leaves a hole that the next alloc fills. */
    CHECK(swapram_pool_free(&pool, b, 100) == 0);
    CHECK(swapram_pool_alloc(&pool, 100, &b) == 0);
    CHECK(b == 200);

    /* Freeing both neighbours of a hole merges three segments into one. */
    CHECK(swapram_pool_free(&pool, b, 100) == 0);
    CHECK(swapram_pool_free(&pool, c, 100) == 0);
    CHECK(swapram_pool_free(&pool, a, 200) == 0);
    CHECK(swapram_pool_avail(&pool) == 4096);
    CHECK(swapram_pool_largest(&pool) == 4096);

    /* Misuse is refused rather than silently corrupting the free list. */
    CHECK(swapram_pool_alloc(&pool, 5000, &a) == -1);
    CHECK(swapram_pool_alloc(&pool, 0, &a) == -1);
    CHECK(swapram_pool_free(&pool, 0, 4096) == -1);   /* double free */
    CHECK(swapram_pool_free(&pool, 8192, 16) == -1);  /* outside */
    CHECK(swapram_pool_trim(&pool, 0, 16, 32) == -1); /* grows */

    /* Sizes round to the grain, so a request of 1 costs SWAPRAM_GRAIN. */
    CHECK(swapram_pool_alloc(&pool, 1, &a) == 0);
    CHECK(swapram_pool_avail(&pool) == 4096 - SWAPRAM_GRAIN);
    CHECK(swapram_pool_free(&pool, a, 1) == 0);
    CHECK(swapram_pool_avail(&pool) == 4096);

    /* The free list cannot grow past nseg: the last free is refused. */
    {
        unsigned int off[8];
        int i;

        for (i = 0; i < 8; i++)
            CHECK(swapram_pool_alloc(&pool, 8, &off[i]) == 0);
        CHECK(pool.p_used == 1);
        for (i = 0; i < 8; i += 2)
            CHECK(swapram_pool_free(&pool, off[i], 8) == 0);
        CHECK(pool.p_used == 5);
        for (i = 1; i < 8; i += 2)
            CHECK(swapram_pool_free(&pool, off[i], 8) == 0);
        CHECK(swapram_pool_avail(&pool) == 4096);
    }
#undef CHECK
    if (!fail)
        printf("pool: 26 checks pass\n");
    return fail;
}

/*
 * The kernel's expand path guards against a stream that never terminates,
 * because a truncated or corrupted pool image would otherwise spin the
 * decoder forever with interrupts enabled and no way out. These cases drive
 * that guard: every one must return -1 and return at all.
 */
static int
codectest(void)
{
    uint8_t src[2048], comp[4096], back[4096];
    long clen;
    size_t i;
    int fail = 0;

    for (i = 0; i < sizeof src; i++)
        src[i] = (uint8_t)(i % 17);
    clen = compress(src, sizeof src, comp, sizeof comp);
    if (clen <= 0) {
        printf("codec: FAIL baseline compress\n");
        return 1;
    }

    /* A stream cut short must not expand to the recorded length. */
    if (decompress(comp, (size_t)clen / 2, back, sizeof src) ==
        (long)sizeof src) {
        printf("codec: FAIL truncated stream expanded in full\n");
        fail = 1;
    }

    /* A destination smaller than the stream must stop, not overrun. */
    if (decompress(comp, (size_t)clen, back, 64) != -1 &&
        memcmp(back, src, 64) != 0) {
        printf("codec: FAIL short destination\n");
        fail = 1;
    }

    /* Random bytes are not a stream; the decoder must return, not spin. */
    for (i = 0; i < sizeof comp; i++)
        comp[i] = (uint8_t)(i * 31 + 7);
    (void)decompress(comp, sizeof comp, back, sizeof back);

    /* An output cap of zero leaves the encoder nowhere to write. */
    if (compress(src, sizeof src, comp, 0) != -1) {
        printf("codec: FAIL zero output cap\n");
        fail = 1;
    }

    if (!fail)
        printf("codec: 4 termination checks pass\n");
    return fail;
}

int
main(int argc, char **argv)
{
    struct aout hdr;
    FILE *f;
    uint8_t *data, *stack, *uarea;
    size_t dlen, ssize = SSIZE;
    int fail = 0;
    size_t i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <a.out> [stack bytes]\n", argv[0]);
        return 2;
    }
    if (argc > 2)
        ssize = (size_t)strtoul(argv[2], NULL, 0);

    f = fopen(argv[1], "rb");
    if (f == NULL || fread(&hdr, sizeof hdr, 1, f) != 1) {
        fprintf(stderr, "%s: cannot read a.out header\n", argv[1]);
        return 2;
    }

    /* What swapout writes: a_data from the file, then a_bss of zeros. */
    dlen = hdr.a_data + hdr.a_bss;
    data = calloc(1, dlen ? dlen : 1);
    if (data == NULL ||
        fseek(f, (long)(sizeof hdr + hdr.a_text), SEEK_SET) != 0 ||
        fread(data, 1, hdr.a_data, f) != hdr.a_data) {
        fprintf(stderr, "%s: cannot read the data segment\n", argv[1]);
        return 2;
    }
    fclose(f);

    /*
     * The stack and the u area are synthetic: a real one is not readable
     * off the host. The stack carries argv and environment strings near its
     * top over untouched space, and the u area is a struct with pointers
     * and a mostly idle 2 KB kernel stack, so both are modelled as zeros
     * with a plausible live top. Every figure from these two rows is an
     * estimate, not a measurement of a running process.
     */
    stack = calloc(1, ssize);
    uarea = calloc(1, USIZE);
    if (stack == NULL || uarea == NULL)
        return 2;
    for (i = ssize > 512 ? ssize - 512 : 0; i < ssize; i++)
        stack[i] = (uint8_t)(0x20 + (i % 64));
    for (i = 0; i < 384; i++)
        uarea[i] = (uint8_t)(i * 7 + (i >> 3));

    printf("image from %s: text %u data %u bss %u\n", argv[1],
        hdr.a_text, hdr.a_data, hdr.a_bss);
    printf("heatshrink window %d lookahead %d index %d\n",
        HEATSHRINK_STATIC_WINDOW_BITS, HEATSHRINK_STATIC_LOOKAHEAD_BITS,
        HEATSHRINK_USE_INDEX);
    printf("encoder %zu bytes, decoder %zu bytes of bss\n",
        sizeof hse, sizeof hsd);
    printf("%-6s %7s %7s\n", "seg", "raw", "comp");

    {
        long cd = 0, cs = 0, cu = 0;
        size_t raw = dlen + ssize + USIZE;

        fail |= measure("data", data, dlen, &cd);
        fail |= measure("stack", stack, ssize, &cs);
        fail |= measure("u", uarea, USIZE, &cu);

        /*
         * The tier stores the three segments as independent streams, so the
         * pool cost is their sum: swapin writes each to its own address and
         * a single stream would have to be split at the same boundaries
         * anyway.
         */
        if (cd > 0 && cs > 0 && cu > 0)
            printf("%-6s %7zu %7ld  %5.2fx  pool cost of one image\n",
                "image", raw, cd + cs + cu, (double)raw / (cd + cs + cu));
    }

    fail |= codectest();
    fail |= pooltest();
    free(data);
    free(stack);
    free(uarea);
    return fail;
}
