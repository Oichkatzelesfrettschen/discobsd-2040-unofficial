#ifndef HEATSHRINK_CONFIG_H
#define HEATSHRINK_CONFIG_H

/*
 * Vendored configuration for the RP2040 kernel and for the host test that
 * measures the same codec. Upstream ships this file as the tuning point, so
 * the values below are the port's and the two .c files stay byte-identical
 * to upstream. Every parameter takes an #ifndef guard so the host harness
 * sweeps them from the command line.
 *
 * Window 9 and lookahead 8 come out of that sweep, recorded in
 * doc/research/zswap.md: lookahead bounds the longest match at 2^8 bytes
 * and costs no RAM, and swap images are mostly bss and untouched stack, so
 * dropping it to 4 costs a factor of four on awk (29.9x against 7.8x) while
 * saving nothing. Window 9 beats both 8 and 10 on every image measured and
 * halves the encoder's index against window 10.
 */

/* The kernel has no malloc for this pool, so every buffer is static. */
#ifndef HEATSHRINK_DYNAMIC_ALLOC
#define HEATSHRINK_DYNAMIC_ALLOC 0
#endif

#if HEATSHRINK_DYNAMIC_ALLOC
    /* Optional replacement of malloc/free */
    #define HEATSHRINK_MALLOC(SZ) malloc(SZ)
    #define HEATSHRINK_FREE(P, SZ) free(P)
#else
    /* Required parameters for static configuration */
    #ifndef HEATSHRINK_STATIC_INPUT_BUFFER_SIZE
    #define HEATSHRINK_STATIC_INPUT_BUFFER_SIZE 64
    #endif
    #ifndef HEATSHRINK_STATIC_WINDOW_BITS
    #define HEATSHRINK_STATIC_WINDOW_BITS 9
    #endif
    #ifndef HEATSHRINK_STATIC_LOOKAHEAD_BITS
    #define HEATSHRINK_STATIC_LOOKAHEAD_BITS 8
    #endif
#endif

/* Turn on logging for debugging. */
#ifndef HEATSHRINK_DEBUGGING_LOGS
#define HEATSHRINK_DEBUGGING_LOGS 0
#endif

/*
 * The search index is off. It changes no output byte -- the sweep in
 * doc/research/zswap.md measures the same compressed length either way --
 * and it costs 2048 bytes of bss plus a 512-byte frame in do_indexing,
 * against a kernel stack of 2100 bytes: USIZE is 3072 and struct user takes
 * 972 of it. Dropping it costs 27 percent of encode time on the awk image.
 */
#ifndef HEATSHRINK_USE_INDEX
#define HEATSHRINK_USE_INDEX 0
#endif

#endif
