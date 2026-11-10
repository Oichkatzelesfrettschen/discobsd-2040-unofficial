/*
 * Copyright (c) 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/systm.h>
#include <sys/vm.h>
#include <sys/inode.h>
#include <sys/uio.h>
#include <sys/exec.h>
#include <sys/signal.h>
#include <sys/storage_stats.h>
#ifdef SWAPRAM
#include <machine/swapram.h>
#endif

/*
 * The clean text at the start of a process's data area: swapout skips it
 * and swapin reads it from the executable. Zero when there is no
 * executable to read from, as for process 1 before it execs.
 */
static size_t
swaptext (register struct proc *p)
{
    if (p->p_tip == NULL || p->p_tsize > p->p_dsize)
        return 0;
    return p->p_tsize;
}

static struct swap_exchange_state swap_exchange;

static size_t
swap_image_span_geometry (size_t data_size, size_t stack_size,
    size_t clean_text)
{
    size_t blocks;

    if (clean_text > data_size)
        clean_text = 0;
    blocks = btod (data_size - clean_text) + btod (stack_size) + btod (USIZE);
#ifdef SWAP_IMAGE_ALIGN
    return (blocks + SWAP_IMAGE_ALIGN - 1) & ~(SWAP_IMAGE_ALIGN - 1);
#else
    return blocks;
#endif
}

#ifdef SWAP_IMAGE_ALIGN
#define SWAP_IMAGE_WRITE (B_WRITE | B_SWAPIMAGE)
#else
#define SWAP_IMAGE_WRITE B_WRITE
#endif

/* Blocks held by one image, excluding executable-backed clean text. */
size_t
swap_image_span (struct proc *p)
{
    return swap_image_span_geometry (p->p_dsize, p->p_ssize, swaptext (p));
}

static void
swap_image_reservation_init (struct swap_image_reservation *reservation,
    struct proc *owner, struct proc *source, u_int odata, u_int ostack)
{
    bzero (reservation, sizeof (*reservation));
    reservation->owner = owner;
    reservation->text = swaptext (source);
    reservation->span = swap_image_span (source);
    reservation->data_blocks = btod (source->p_dsize - reservation->text);
    reservation->stack_blocks = btod (source->p_ssize);
    if (odata == (u_int) X_OLDSIZE)
        odata = source->p_dsize;
    if (ostack == (u_int) X_OLDSIZE)
        ostack = source->p_ssize;
    reservation->data = odata > reservation->text ?
        odata - reservation->text : 0;
    reservation->stack = ostack;
}

/*
 * Move the resident into the pool with its route when compression admits it,
 * or consume the route as flash backing.  A process returning from SwapRAM
 * transfers its separately owned route before the old resident leaves.
 */
int
swap_exchange_switch (struct swap_image_reservation *reservation,
    struct proc *outgoing, struct proc *incoming, int pool_backed)
{
#ifdef SWAP_IMAGE_ALIGN
    size_t incoming_span = 0;
    size_t incoming_base = 0;
    int spl;

    spl = splhigh ();
    if (outgoing != NULL) {
        swap_image_reservation_init (reservation, outgoing, outgoing,
            X_OLDSIZE, X_OLDSIZE);
        if (swap_exchange.owner != outgoing || swap_exchange.base == 0 ||
            swap_exchange.span < reservation->span)
            panic ("swap exchange: departure owner");
    } else
        bzero (reservation, sizeof (*reservation));

    if (pool_backed) {
#ifdef SWAPRAM
        incoming_span = swap_image_span (incoming);
        if (! swapram_route_take (incoming, &incoming_base,
            &incoming_span)) {
            splx (spl);
            return ENOMEM;
        }
        if (incoming_span < swap_image_span (incoming))
            panic ("swap exchange: short pool route");
#else
        panic ("swap exchange: pool without SwapRAM");
#endif
    }

#ifndef SWAPRAM
    (void) incoming;
#endif

    if (outgoing != NULL) {
#ifdef SWAPRAM
        if (swapram_reserve_route (outgoing,
            (caddr_t) (outgoing->p_daddr + reservation->text),
            reservation->data, (caddr_t) outgoing->p_saddr,
            reservation->stack, USIZE, swap_exchange.base,
            swap_exchange.span))
            reservation->tier = SWAP_IMAGE_RAM;
        else
#endif
        {
            reservation->addr[0] = swap_exchange.base;
            reservation->addr[1] = reservation->addr[0] +
                reservation->data_blocks;
            reservation->addr[2] = reservation->addr[1] +
                reservation->stack_blocks;
            reservation->tier = SWAP_IMAGE_FLASH;
            if (swap_exchange.span > reservation->span)
                mfree (swapmap, swap_exchange.span - reservation->span,
                    swap_exchange.base + reservation->span);
        }
        reservation->state = SWAP_IMAGE_RESERVED;
    }

    swap_exchange.owner = pool_backed ? incoming : NULL;
    swap_exchange.base = pool_backed ? incoming_base : 0;
    swap_exchange.span = incoming_span;
    splx (spl);
    return 0;
#else
    (void) incoming;
    (void) pool_backed;
    if (outgoing == NULL) {
        bzero (reservation, sizeof (*reservation));
        return 0;
    }
    return swap_image_reserve (reservation, outgoing, outgoing,
        X_OLDSIZE, X_OLDSIZE);
#endif
}

#ifdef SWAP_IMAGE_ALIGN
/* Consume free blocks that directly follow the resident's owned extent. */
static int
swap_exchange_extend (size_t required)
{
    struct mapent *entry;
    size_t end, extra;

    end = swap_exchange.base + swap_exchange.span;
    extra = required - swap_exchange.span;
    for (entry = swapmap->m_map;
        entry->m_size != 0 && entry->m_addr < end; entry++)
        continue;
    if (entry->m_size < extra || entry->m_addr != end)
        return 0;
    entry->m_addr += extra;
    entry->m_size -= extra;
    if (entry->m_size == 0)
        do {
            *entry = entry[1];
        } while ((entry++)->m_size != 0);
    swap_exchange.span = required;
    return 1;
}
#endif

void
swap_exchange_restore (const struct swap_exchange_state *previous)
{
    int spl;

    spl = splhigh ();
    swap_exchange = *previous;
    splx (spl);
}

/* Geometry changes become visible only after their departure route exists. */
int
swap_exchange_resize (struct proc *owner, size_t data_size,
    size_t stack_size, size_t clean_text)
{
#ifdef SWAP_IMAGE_ALIGN
    size_t addresses[3], next, span;
    size_t required;
    int spl;

    required = swap_image_span_geometry (data_size, stack_size, clean_text);
    spl = splhigh ();
    if (swap_exchange.owner != owner) {
        splx (spl);
        return ENOMEM;
    }
    if (swap_exchange.span >= required) {
        splx (spl);
        return 0;
    }
    if (swap_exchange_extend (required)) {
        splx (spl);
        return 0;
    }
    next = swapnext;
    span = malloc3_contiguous_next (swapmap, required, 0, 0,
        SWAP_IMAGE_ALIGN, &next, addresses);
    if (span == 0) {
        splx (spl);
        return ENOMEM;
    }
    mfree (swapmap, swap_exchange.span, swap_exchange.base);
    swap_exchange.base = addresses[0];
    swap_exchange.span = span;
    swapnext = next;
    swap_cursor_publish (swapnext);
    splx (spl);
    return 0;
#else
    (void) owner;
    (void) data_size;
    (void) stack_size;
    (void) clean_text;
    return 0;
#endif
}

void
swap_exchange_release (struct proc *owner)
{
#ifdef SWAP_IMAGE_ALIGN
    int spl;

    spl = splhigh ();
    if (swap_exchange.owner != owner)
        panic ("swap exchange: release owner");
    mfree (swapmap, swap_exchange.span, swap_exchange.base);
    swap_exchange.owner = NULL;
    swap_exchange.base = 0;
    swap_exchange.span = 0;
    splx (spl);
#else
    (void) owner;
#endif
    wakeup ((caddr_t) &runin);
}

/* Refusal changes neither the owner nor the map, cursor or image bytes. */
int
swap_image_reserve (struct swap_image_reservation *reservation,
    struct proc *owner, struct proc *source, u_int odata, u_int ostack)
{
#ifdef SWAP_IMAGE_ALIGN
    size_t next = swapnext;
#endif

    swap_image_reservation_init (reservation, owner, source, odata, ostack);
#ifdef SWAPRAM
    if (swapram_reserve (owner,
        (caddr_t) (source->p_daddr + reservation->text), reservation->data,
        (caddr_t) source->p_saddr, reservation->stack, USIZE)) {
        reservation->tier = SWAP_IMAGE_RAM;
    } else
#endif
    {
#ifdef SWAP_IMAGE_ALIGN
        if (malloc3_contiguous_next (swapmap,
            btod (source->p_dsize - reservation->text),
            btod (source->p_ssize), btod (USIZE),
            SWAP_IMAGE_ALIGN, &next, reservation->addr) == 0)
            return ENOMEM;
        swapnext = next;
        swap_cursor_publish (swapnext);
#else
        if (malloc3 (swapmap, btod (source->p_dsize - reservation->text),
            btod (source->p_ssize), btod (USIZE), reservation->addr) == 0)
            return ENOMEM;
#endif
    }
    reservation->owner = owner;
    reservation->state = SWAP_IMAGE_RESERVED;
    return 0;
}

/* Cancellation returns untouched backing and is safe to repeat. */
void
swap_image_cancel (struct swap_image_reservation *reservation)
{
    if (reservation->state == SWAP_IMAGE_EMPTY)
        return;
    if (reservation->state != SWAP_IMAGE_RESERVED)
        panic ("swap image: cancel state");
#ifdef SWAPRAM
    if (reservation->tier == SWAP_IMAGE_RAM)
        swapram_cancel (reservation->owner);
    else
#endif
    {
#ifdef SWAP_IMAGE_ALIGN
        mfree (swapmap, reservation->span, reservation->addr[0]);
#else
        mfree (swapmap, reservation->data_blocks, reservation->addr[0]);
        mfree (swapmap, reservation->stack_blocks, reservation->addr[1]);
        mfree (swapmap, btod (USIZE), reservation->addr[2]);
#endif
    }
    reservation->state = SWAP_IMAGE_EMPTY;
    reservation->owner = NULL;
}

/* Release publishes free capacity before waking a potential swapper. */
void
swap_image_release (struct proc *p)
{
    int spl;

    spl = splhigh ();
#ifdef SWAPRAM
    if (swapram_present (p))
        swapram_cancel (p);
    else
#endif
    {
#ifdef SWAP_IMAGE_ALIGN
        if (swap_exchange.owner != NULL || swap_exchange.base != 0 ||
            swap_exchange.span != 0)
            panic ("swap exchange: occupied on transfer");
        swap_exchange.owner = p;
        swap_exchange.base = p->p_daddr;
        swap_exchange.span = swap_image_span (p);
#else
        mfree (swapmap, btod (p->p_dsize - swaptext (p)), p->p_daddr);
        mfree (swapmap, btod (p->p_ssize), p->p_saddr);
        mfree (swapmap, btod (USIZE), p->p_addr);
#endif
    }
    splx (spl);
    wakeup ((caddr_t) &runout);
}

/*
 * Swap a process in.
 * Allocate data and possible text separately.  It would be better
 * to do largest first.  Text, data, and stack are allocated in
 * that order, as that is likely to be in order of size.
 * U area goes into u0 buffer.
 */
void
swapin (register struct proc *p)
{
    size_t daddr = (size_t)__user_data_start;
    size_t saddr = USER_TOP (p) - p->p_ssize;
    size_t uaddr = (size_t) &u0;
    size_t tsize = swaptext (p);

    /*
     * The text comes back from the executable, which swapout never
     * wrote; only what follows it is on swap.
     */
    if (tsize && exec_text_restore (p, (caddr_t) daddr, tsize) != 0) {
        /*
         * The file no longer yields the text the process ran: a flipped
         * bit in a packed stream, a short raw image. The process comes
         * back with garbage text and SIGKILL takes it before that text
         * runs, while the swapper and every other process go on.
         */
        printf ("swapin: pid %d: executable text corrupt\n", p->p_pid);
        psignal (p, SIGKILL);
    }
#ifdef SWAPRAM
    /*
     * An image the RAM tier took holds no swapmap blocks, so it comes back
     * whole from the pool and nothing here is freed. Restoring it frees
     * pool capacity, so the swapper is woken exactly as
     * swap_image_release() wakes it for flash capacity.
     */
    if (swapram_present (p)) {
        swapram_in (p, (caddr_t) (daddr + tsize), (caddr_t) saddr,
            (caddr_t) uaddr);
        wakeup ((caddr_t) &runout);
    } else
#endif
    {
    if (p->p_dsize > tsize) {
        swap (p->p_daddr, daddr + tsize, p->p_dsize - tsize, B_READ);
    }
    if (p->p_ssize) {
        swap (p->p_saddr, saddr, p->p_ssize, B_READ);
    }
    swap (p->p_addr, uaddr, USIZE, B_READ);
    swap_image_release (p);
    }

    p->p_daddr = daddr;
    p->p_saddr = saddr;
    p->p_addr = uaddr;
    if (p->p_stat == SRUN)
        setrq (p);
    p->p_flag |= SLOAD;
    p->p_time = 0;
#ifdef UCB_METER
    cnt.v_swpin++;
#endif
}

/* Copy into owned backing; capacity cannot fail after reservation. */
void
swap_image_copy_out (struct swap_image_reservation *reservation,
    struct proc *p)
{
    size_t tsize = reservation->text;
    size_t odata = reservation->data;
    size_t ostack = reservation->stack;
    size_t *a = reservation->addr;
#ifdef SWAPRAM
    int ram = reservation->tier == SWAP_IMAGE_RAM;
#endif

    if (reservation->owner != p ||
        reservation->state != SWAP_IMAGE_RESERVED)
        panic ("swap image: copy owner");
#ifdef SWAPRAM
    storage_note (STORAGE_SWAPRAM_ATTEMPTS, 1);
    if (ram)
        storage_note (STORAGE_SWAPRAM_ADMISSIONS, 1);
#endif
    p->p_flag |= SLOCK;
    if (odata) {
#ifdef SWAPRAM
        if (ram)
            swapram_put (p, SWAPRAM_DATA, (caddr_t) (p->p_daddr + tsize),
                odata);
        else
#endif
        swap (a[0], p->p_daddr + tsize, odata, SWAP_IMAGE_WRITE);
    }
    if (ostack) {
#ifdef SWAPRAM
        if (ram)
            swapram_put (p, SWAPRAM_STACK, (caddr_t) p->p_saddr, ostack);
        else
#endif
        swap (a[1], p->p_saddr, ostack, SWAP_IMAGE_WRITE);
    }
    /*
     * Increment u_ru.ru_nswap for process being tossed out of core.
     * We can be called to swap out a process other than the current
     * process, so we have to map in the victim's u structure briefly.
     * Note, savekdsa6 *must* be a static, because we remove the stack
     * in the next instruction.  The splclock is to prevent the clock
     * from coming in and doing accounting for the wrong process, plus
     * we don't want to come through here twice.  Why are we doing
     * this, anyway?
     */
    {
        int s;

        s = splclock();
        u.u_ru.ru_nswap++;
        splx (s);
    }
#ifdef SWAPRAM
    /*
     * The u area is captured after the ru_nswap increment above, because
     * for the current process that increment lands inside these USIZE
     * bytes.
     */
    if (ram) {
        swapram_put (p, SWAPRAM_U, (caddr_t) p->p_addr, USIZE);
        swapram_commit (p);
    } else
#endif
    swap (a[2], p->p_addr, USIZE, SWAP_IMAGE_WRITE);
    reservation->state = SWAP_IMAGE_COPIED;
}

/* Publish addresses only after every image byte has reached owned backing. */
void
swap_image_commit (struct swap_image_reservation *reservation, struct proc *p)
{
    size_t *a = reservation->addr;

    if (reservation->owner != p ||
        reservation->state != SWAP_IMAGE_COPIED)
        panic ("swap image: commit owner");
    /*
     * a[] is zero on the RAM tier, which is how swapin and pstat see that
     * the process holds no flash swap blocks. A zero p_addr is never jumped
     * to: swtch longjmps to p_addr only for processes on the run queue, and
     * setrq is guarded by SLOAD everywhere, which swapout clears below.
     */
    p->p_daddr = a[0];
    p->p_saddr = a[1];
    p->p_addr = a[2];
    p->p_flag &= ~(SLOAD|SLOCK);
    p->p_time = 0;
    reservation->state = SWAP_IMAGE_COMMITTED;

#ifdef UCB_METER
    cnt.v_swpout++;
#endif
    if (runout) {
        runout = 0;
        wakeup ((caddr_t)&runout);
    }
}

/* Every caller reserves the image first; swapout itself cannot fail. */
void
swapout (register struct proc *p, int freecore __unused, u_int odata __unused,
    u_int ostack __unused, struct swap_image_reservation *reservation)
{
    if (reservation == NULL || reservation->state != SWAP_IMAGE_RESERVED)
        panic ("swapout: no reservation");
    swap_image_copy_out (reservation, p);
    swap_image_commit (reservation, p);
}
