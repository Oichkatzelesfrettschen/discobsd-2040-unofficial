/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/vm.h>

/*
 * Resource map handling routines.
 *
 * A resource map is an array of structures each of which describes a
 * segment of the address space of an available resource.  The segments
 * are described by their base address and length, and sorted in address
 * order.  Each resource map has a fixed maximum number of segments
 * allowed.  Resources are allocated by taking part or all of one of the
 * segments of the map.
 *
 * Returning of resources will require another segment if the returned
 * resources are not adjacent in the address space to an existing segment.
 * If the return of a segment would require a slot which is not available,
 * then one of the resource map segments is discarded after a warning is
 * printed.
 *
 * Returning of resources may also cause the map to collapse by coalescing
 * two existing segments and the returned space into a single segment.  In
 * this case the resource map is made smaller by copying together to fill
 * the resultant gap.
 *
 * N.B.: the current implementation uses a dense array and does not admit
 * the value ``0'' as a legal address or size, since that is used as a
 * delimiter.
 */

/*
 * Allocate 'size' units from the given map.  Return the base of the
 * allocated space.  In a map, the addresses are increasing and the
 * list is terminated by a 0 size.
 *
 * Algorithm is first-fit.
 */
size_t
malloc (struct map *mp, register size_t size)
{
    register struct mapent *bp, *ep;
    size_t addr;

    if (! size)
        panic ("malloc: size = 0");
    /*
     * Search for a piece of the resource map which has enough
     * free space to accomodate the request.
     */
    for (bp = mp->m_map; bp->m_size; ++bp)
        if (bp->m_size >= size) {
            /*
             * Allocate from the map.  If we allocated the entire
             * piece, move the rest of the map to the left.
             */
            addr = bp->m_addr;
            bp->m_size -= size;
            if (bp->m_size)
                bp->m_addr += size;
            else for (ep = bp;; ++ep) {
                *ep = *++bp;
                if (!bp->m_size)
                    break;
            }
            return(addr);
        }
    /* no entries big enough */
    return 0;
}

/*
 * Free the previously allocated size units at addr into the specified
 * map.  Sort addr into map and combine on one or both ends if possible.
 */
void
mfree (struct map *mp, size_t size, register size_t addr)
{
    register struct mapent *bp, *ep;
    struct mapent *start;

    if (! size)
        return;
    /* the address must not be 0, or the protocol has broken down. */
    if (! addr)
        panic ("mfree: addr = 0");

    /* A ring extent above m_ring_end continues at m_ring_first. */
    if (mp->m_ring_end != 0) {
        if (addr >= mp->m_ring_end)
            addr -= mp->m_ring_end - mp->m_ring_first;
        if (size > mp->m_ring_end - addr) {
            size_t low = mp->m_ring_end - addr;

            mfree (mp, size - low, mp->m_ring_first);
            size = low;
        }
    }

    /*
     * locate the piece of the map which starts after the
     * returned space (or the end of the map).
     */
    bp = mp->m_map;
    /* printf ("mfree (size=%u, addr=%u) m_map = %08x\n", size, addr, bp); */

    while (bp->m_size && bp->m_addr <= addr) {
        /*printf ("skip m_map[%d]: m_addr %u <= addr %u\n", bp - mp->m_map, bp->m_addr, addr);*/
        ++bp;
    }

    /* if there is a piece on the left abutting us, combine with it. */
    ep = bp - 1;
    if (bp != mp->m_map && ep->m_addr + ep->m_size >= addr) {
#ifdef DIAGNOSTIC
        /* any overlap is an internal error */
        if (ep->m_addr + ep->m_size > addr)
            panic("mfree overlap #1");
#endif
        /* add into piece on the left by increasing its size. */
        ep->m_size += size;

        /*
         * if the combined piece abuts the piece on the right now,
         * compress it in also, by shifting the remaining pieces
         * of the map over.
         */
        if (bp->m_size && addr + size >= bp->m_addr) {
#ifdef DIAGNOSTIC
            if (addr + size > bp->m_addr)
                panic("mfree overlap #2");
#endif
            ep->m_size += bp->m_size;
            do {
                *++ep = *++bp;
            } while (bp->m_size);
        }
        return;
    }

    /* if doesn't abut on the left, check for abutting on the right. */
    if (bp->m_size && addr + size >= bp->m_addr) {
#ifdef DIAGNOSTIC
        if (addr + size > bp->m_addr)
            panic("mfree overlap #3");
#endif
        bp->m_addr = addr;
        bp->m_size += size;
        return;
    }

    /* doesn't abut.  Make a new entry and check for map overflow. */
    for (start = bp; bp->m_size; ++bp);
    if (++bp > mp->m_limit)
        /*
         * too many segments; if this happens, the correct fix
         * is to make the map bigger; you can't afford to lose
         * chunks of the map.  If you need to implement recovery,
         * use the above "for" loop to find the smallest entry
         * and toss it.
         */
        printf("%s: overflow, lost %u clicks at 0%o\n",
            mp->m_name, size, addr);
    else {
        for (ep = bp - 1; ep >= start; *bp-- = *ep--);
        start->m_addr = addr;
        start->m_size = size;
    }
}

/*
 * Allocate resources for the three segments of a process (data, stack
 * and u.), attempting to minimize the cost of failure part-way through.
 * Since the segments are located successively, it is best for the sizes
 * to be in decreasing order; generally, data, stack, then u. will be
 * best.  Returns NULL on failure, address of u. on success.
 */
size_t
malloc3 (struct map *mp, size_t d_size, size_t s_size, size_t u_size,
    size_t a[3])
{
    register struct mapent *bp, *remap;
    register int next;
    struct mapent *madd[3];
    size_t sizes[3];
    int found;

    sizes[0] = d_size;
    sizes[1] = s_size;
    sizes[2] = u_size;
    /*
     * note, this has to work for d_size and s_size of zero,
     * since init() comes in that way.
     */
    madd[0] = madd[1] = madd[2] = remap = NULL;
    for (found = 0, bp = mp->m_map; bp->m_size; ++bp)
        for (next = 0; next < 3; ++next)
            if (!madd[next] && sizes[next] <= bp->m_size) {
                madd[next] = bp;
                bp->m_size -= sizes[next];
                if (!bp->m_size && !remap)
                    remap = bp;
                if (++found == 3)
                    goto resolve;
            }

    /* couldn't get it all; restore the old sizes, try again */
    for (next = 0; next < 3; ++next)
        if (madd[next])
            madd[next]->m_size += sizes[next];
    return 0;

resolve:
    /* got it all, update the addresses. */
    for (next = 0; next < 3; ++next) {
        bp = madd[next];
        a[next] = bp->m_addr;
        bp->m_addr += sizes[next];
    }

    /* remove any entries of size 0; addr of 0 terminates */
    if (remap) {
        for (bp = remap + 1;; ++bp) {
            if (bp->m_size || !bp->m_addr) {
                *remap++ = *bp;
                if (!bp->m_addr)
                    break;
            }
        }
    }
    return(a[2]);
}

/*
 * Allocate data, stack and u area as one run, rounded to align units.
 *
 * The map must start aligned and callers must return the complete run. Those
 * two conditions preserve alignment without splitting a free-map entry, which
 * also keeps the maximum map-entry count to one per live allocation. The
 * three addresses retain the layout the swap code expects, including zero
 * length data or stack segments.
 */
size_t
malloc3_contiguous (struct map *mp, size_t d_size, size_t s_size, size_t u_size,
    size_t align, size_t a[3])
{
    size_t allocation_size, span, base;

    if (align == 0 || (align & (align - 1)) != 0)
        panic ("malloc3_contiguous: bad alignment");
    if (d_size > (size_t)-1 - s_size ||
        d_size + s_size > (size_t)-1 - u_size)
        return 0;
    allocation_size = d_size + s_size + u_size;
    if (allocation_size == 0 || allocation_size > (size_t)-1 - (align - 1))
        return 0;
    span = (allocation_size + align - 1) & ~(align - 1);
    base = malloc (mp, span);
    if (base == 0)
        return 0;
    if ((base & (align - 1)) != 0)
        panic ("malloc3_contiguous: unaligned map");

    a[0] = base;
    a[1] = base + d_size;
    a[2] = a[1] + s_size;
    return span;
}

/* Free units from addr upward, passing m_ring_end into m_ring_first. */
static size_t
ring_free_from (struct map *mp, size_t addr)
{
    struct mapent *bp, *first = mp->m_map;
    size_t run;

    for (bp = first; bp->m_size; bp++)
        if (addr >= bp->m_addr && addr < (size_t) bp->m_addr + bp->m_size)
            break;
    if (bp->m_size == 0)
        return 0;
    run = (size_t) bp->m_addr + bp->m_size - addr;
    if ((size_t) bp->m_addr + bp->m_size == mp->m_ring_end &&
        first->m_addr == mp->m_ring_first)
        run += first == bp ? addr - first->m_addr : first->m_size;
    return run;
}

/* Longest free run of a ring map. */
static size_t
ring_largest (struct map *mp)
{
    struct mapent *bp, *first = mp->m_map, *last = NULL;
    size_t best = 0;

    for (bp = first; bp->m_size; bp++) {
        if (bp->m_size > best)
            best = bp->m_size;
        last = bp;
    }
    if (last != NULL && last != first &&
        first->m_addr == mp->m_ring_first &&
        (size_t) last->m_addr + last->m_size == mp->m_ring_end &&
        (size_t) first->m_size + last->m_size > best)
        best = (size_t) first->m_size + last->m_size;
    return best;
}

/*
 * Remove [addr, addr + size) from the free run that holds it.  Returns 0
 * with the map unchanged when the piece splits a run and no descriptor is
 * spare.
 */
static int
ring_take (struct map *mp, size_t addr, size_t size)
{
    struct mapent *bp, *endp, *move;
    size_t base, end;

    if (size == 0)
        return 1;
    for (endp = mp->m_map; endp->m_size; endp++)
        continue;
    for (bp = mp->m_map; bp->m_size; bp++)
        if (addr >= bp->m_addr &&
            addr + size <= (size_t) bp->m_addr + bp->m_size)
            break;
    if (bp->m_size == 0)
        panic ("ring_take: not free");
    base = bp->m_addr;
    end = base + bp->m_size;
    if (addr == base) {
        bp->m_addr += size;
        bp->m_size -= size;
        if (bp->m_size == 0)
            do {
                *bp = bp[1];
            } while ((bp++)->m_size != 0);
    } else if (addr + size == end) {
        bp->m_size -= size;
    } else {
        if (endp >= mp->m_limit)
            return 0;
        for (move = endp; move > bp; move--)
            move[1] = move[0];
        bp[1].m_addr = addr + size;
        bp[1].m_size = end - bp[1].m_addr;
        bp->m_size = addr - base;
    }
    return 1;
}

/*
 * Next-fit on a ring map.  Aligned positions are tried from the cursor
 * round the ring.  The first one that leaves a free run of
 * min(m_guard, longest run) units is taken; when none does, the one that
 * leaves the longest run is.  A position inside a run that would need a
 * descriptor the map does not have is passed over; the run's own start is
 * a later position, so a fitting request is still served.
 *
 * The rule has no preferred address: a rotated cursor gives the rotated
 * placements, so a cursor that starts uniformly spreads erases evenly over
 * the ring.  docs/research/swap-exhaustion/placement-wear.md has the model.
 */
static size_t
ring_next (struct map *mp, size_t span, size_t align, size_t *nextp)
{
    size_t length = mp->m_ring_end - mp->m_ring_first;
    size_t start, step, base, low, target, after;
    size_t best = 0, best_after = 0;
    int have_best = 0;

    if (span > length)
        return 0;
    target = ring_largest (mp);
    if (target > mp->m_guard)
        target = mp->m_guard;
    start = *nextp;
    if (start < mp->m_ring_first || start >= mp->m_ring_end)
        start = mp->m_ring_first;
    start = mp->m_ring_first +
        ((start - mp->m_ring_first + align - 1) & ~(align - 1)) % length;
    for (step = 0; step < length; step += align) {
        base = mp->m_ring_first +
            (start - mp->m_ring_first + step) % length;
        if (ring_free_from (mp, base) < span)
            continue;
        low = span < mp->m_ring_end - base ? span : mp->m_ring_end - base;
        if (! ring_take (mp, base, low))
            continue;
        if (! ring_take (mp, mp->m_ring_first, span - low)) {
            mfree (mp, low, base);
            continue;
        }
        after = ring_largest (mp);
        if (after >= target) {
            *nextp = mp->m_ring_first +
                (base - mp->m_ring_first + span) % length;
            return base;
        }
        mfree (mp, span, base);
        if (! have_best || after > best_after) {
            have_best = 1;
            best_after = after;
            best = base;
        }
    }
    if (! have_best)
        return 0;
    low = span < mp->m_ring_end - best ? span : mp->m_ring_end - best;
    if (! ring_take (mp, best, low) ||
        ! ring_take (mp, mp->m_ring_first, span - low))
        panic ("ring_next: lost placement");
    *nextp = mp->m_ring_first + (best - mp->m_ring_first + span) % length;
    return best;
}

/*
 * Allocate one aligned run using a cyclic next-fit search.  Raw NOR swap
 * callers use the cursor to move short-lived images across erase sectors
 * instead of returning to the lowest free sector after every release.
 *
 * An allocation in the middle of a free run needs one additional map
 * descriptor.  When the fixed map has no spare descriptor, the search uses
 * a fitting run boundary instead.  Descriptor pressure can therefore weaken
 * rotation, but it cannot turn a fitting allocation into a false shortage.
 */
size_t
malloc3_contiguous_next (struct map *mp, size_t d_size, size_t s_size,
    size_t u_size, size_t align, size_t *nextp, size_t a[3])
{
    struct mapent *bp, *endp, *selected;
    size_t base, candidate, end, selected_base, span, allocation_size;
    int pass, spare;

    if (align == 0 || (align & (align - 1)) != 0)
        panic ("malloc3_contiguous_next: bad alignment");
    if (nextp == NULL)
        panic ("malloc3_contiguous_next: no cursor");
    if (d_size > (size_t)-1 - s_size ||
        d_size + s_size > (size_t)-1 - u_size)
        return 0;
    allocation_size = d_size + s_size + u_size;
    if (allocation_size == 0 || allocation_size > (size_t)-1 - (align - 1))
        return 0;
    span = (allocation_size + align - 1) & ~(align - 1);

    if (mp->m_ring_end != 0) {
        base = ring_next (mp, span, align, nextp);
        if (base == 0)
            return 0;
        a[0] = base;
        a[1] = base + d_size;
        a[2] = a[1] + s_size;
        return span;
    }

    for (endp = mp->m_map; endp->m_size; endp++)
        continue;
    spare = endp < mp->m_limit;
    selected = NULL;
    selected_base = 0;

    /* Pass zero searches at and above the cursor; pass one wraps below it. */
    for (pass = 0; pass < 2 && selected == NULL; pass++) {
        for (bp = mp->m_map; bp->m_size; bp++) {
            if ((bp->m_addr & (align - 1)) != 0 ||
                (bp->m_size & (align - 1)) != 0)
                panic ("malloc3_contiguous_next: unaligned map");
            end = (size_t)bp->m_addr + (size_t)bp->m_size;
            if (end < (size_t)bp->m_addr)
                panic ("malloc3_contiguous_next: corrupt map");
            if (bp->m_size < span)
                continue;

            if (pass == 0) {
                candidate = *nextp > bp->m_addr ? *nextp : bp->m_addr;
                if (candidate > (size_t)-1 - (align - 1))
                    continue;
                candidate = (candidate + align - 1) & ~(align - 1);
                if (candidate > end - span)
                    continue;
            } else {
                candidate = bp->m_addr;
                if (candidate >= *nextp)
                    continue;
            }

            /* Prefer the cursor, but avoid an interior split when full. */
            if (!spare && candidate != bp->m_addr &&
                candidate != end - span) {
                candidate = end - span;
                if ((pass == 0 && candidate < *nextp) ||
                    (pass == 1 && candidate >= *nextp))
                    continue;
            }
            selected = bp;
            selected_base = candidate;
            break;
        }
    }
    if (selected == NULL)
        return 0;

    bp = selected;
    base = bp->m_addr;
    end = base + bp->m_size;
    if (selected_base == base) {
        bp->m_addr += span;
        bp->m_size -= span;
        if (bp->m_size == 0)
            do {
                *bp = bp[1];
            } while ((bp++)->m_size != 0);
    } else if (selected_base == end - span) {
        bp->m_size -= span;
    } else {
        struct mapent *move;

        for (move = endp; move > bp; move--)
            move[1] = move[0];
        bp[1].m_addr = selected_base + span;
        bp[1].m_size = end - bp[1].m_addr;
        bp->m_size = selected_base - base;
    }

    a[0] = selected_base;
    a[1] = selected_base + d_size;
    a[2] = a[1] + s_size;
    *nextp = selected_base + span;
    return span;
}
