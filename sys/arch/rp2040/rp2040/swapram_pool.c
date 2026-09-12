/*
 * First-fit byte allocator for the compressed swap tier.
 *
 * The free list is an address-ordered array of segments held outside the
 * pool, so an allocation never writes a header into the bytes it hands out
 * and a corrupt image cannot corrupt the allocator. Every routine reports an
 * inconsistency by return value rather than calling panic, which is what
 * lets the host test in arch/rp2040/test/swapram link this file with no
 * kernel headers and run it under valgrind.
 *
 * Offsets, not pointers, name allocations: offset 0 is a legal allocation
 * here and the caller keeps its own notion of "absent".
 */

#include <machine/swapram.h>

/* Round up to SWAPRAM_GRAIN, which every offset and size is a multiple of. */
static unsigned int
roundgrain(unsigned int n)
{
    return (n + (SWAPRAM_GRAIN - 1)) & ~(unsigned int)(SWAPRAM_GRAIN - 1);
}

/*
 * Start with the whole pool free. The caller owns both arrays; nseg bounds
 * how many disjoint free segments the pool can describe, and an mfree that
 * would need one more discards the returned space rather than overrun.
 */
void
swapram_pool_init(struct swapram_pool *mp, unsigned char *base,
    unsigned int size, struct swapram_seg *seg, unsigned int nseg)
{
    mp->p_base = base;
    mp->p_size = size & ~(unsigned int)(SWAPRAM_GRAIN - 1);
    mp->p_seg = seg;
    mp->p_nseg = nseg;
    if (nseg == 0 || mp->p_size == 0) {
        mp->p_used = 0;
        return;
    }
    seg[0].s_off = 0;
    seg[0].s_size = mp->p_size;
    mp->p_used = 1;
}

/*
 * First fit. Returns 0 and sets *offp on success, -1 when no single free
 * segment holds size bytes, which is the caller's signal to use flash.
 */
int
swapram_pool_alloc(struct swapram_pool *mp, unsigned int size,
    unsigned int *offp)
{
    unsigned int i, want = roundgrain(size);

    if (want == 0 || want > mp->p_size)
        return -1;
    for (i = 0; i < mp->p_used; i++) {
        if (mp->p_seg[i].s_size < want)
            continue;
        *offp = mp->p_seg[i].s_off;
        mp->p_seg[i].s_off += want;
        mp->p_seg[i].s_size -= want;
        if (mp->p_seg[i].s_size == 0) {
            for (; i + 1 < mp->p_used; i++)
                mp->p_seg[i] = mp->p_seg[i + 1];
            mp->p_used--;
        }
        return 0;
    }
    return -1;
}

/*
 * Return size bytes at off, coalescing with the neighbours the address
 * order makes adjacent. Returns -1 when the range overlaps a segment
 * already free or leaves the pool, which is a double free or a stray
 * offset in the caller.
 */
int
swapram_pool_free(struct swapram_pool *mp, unsigned int off,
    unsigned int size)
{
    unsigned int i, j, want = roundgrain(size);

    if (want == 0 || off % SWAPRAM_GRAIN != 0 || off + want > mp->p_size)
        return -1;

    /* Locate the first free segment that starts at or after off. */
    for (i = 0; i < mp->p_used && mp->p_seg[i].s_off < off; i++)
        continue;
    if (i > 0 && mp->p_seg[i - 1].s_off + mp->p_seg[i - 1].s_size > off)
        return -1;
    if (i < mp->p_used && off + want > mp->p_seg[i].s_off)
        return -1;

    /* Grow the segment below when it ends exactly at off. */
    if (i > 0 && mp->p_seg[i - 1].s_off + mp->p_seg[i - 1].s_size == off) {
        mp->p_seg[i - 1].s_size += want;
        /* The two neighbours now touch: merge them. */
        if (i < mp->p_used && mp->p_seg[i - 1].s_off +
            mp->p_seg[i - 1].s_size == mp->p_seg[i].s_off) {
            mp->p_seg[i - 1].s_size += mp->p_seg[i].s_size;
            for (j = i; j + 1 < mp->p_used; j++)
                mp->p_seg[j] = mp->p_seg[j + 1];
            mp->p_used--;
        }
        return 0;
    }

    /* Extend the segment above when it starts exactly at the end. */
    if (i < mp->p_used && off + want == mp->p_seg[i].s_off) {
        mp->p_seg[i].s_off = off;
        mp->p_seg[i].s_size += want;
        return 0;
    }

    /* Neither touches: the range needs a slot of its own. */
    if (mp->p_used == mp->p_nseg)
        return -1;
    for (j = mp->p_used; j > i; j--)
        mp->p_seg[j] = mp->p_seg[j - 1];
    mp->p_seg[i].s_off = off;
    mp->p_seg[i].s_size = want;
    mp->p_used++;
    return 0;
}

/*
 * Release the tail of an allocation once the encoder has reported how much
 * of the reservation it used. Both sizes round to the grain, so a trim that
 * frees nothing is a no-op rather than an error.
 */
int
swapram_pool_trim(struct swapram_pool *mp, unsigned int off,
    unsigned int oldsize, unsigned int newsize)
{
    unsigned int keep = roundgrain(newsize), had = roundgrain(oldsize);

    if (keep > had)
        return -1;
    if (keep == had)
        return 0;
    return swapram_pool_free(mp, off + keep, had - keep);
}

/* Bytes free anywhere in the pool; the swapout print reports it. */
unsigned int
swapram_pool_avail(struct swapram_pool *mp)
{
    unsigned int i, n = 0;

    for (i = 0; i < mp->p_used; i++)
        n += mp->p_seg[i].s_size;
    return n;
}

/* The largest single allocation the pool can still satisfy. */
unsigned int
swapram_pool_largest(struct swapram_pool *mp)
{
    unsigned int i, n = 0;

    for (i = 0; i < mp->p_used; i++)
        if (mp->p_seg[i].s_size > n)
            n = mp->p_seg[i].s_size;
    return n;
}
