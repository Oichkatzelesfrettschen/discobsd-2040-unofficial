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
swaptext (p)
    register struct proc *p;
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

#ifdef SWAP_IMAGE_ALIGN
static size_t
swap_exchange_largest_run (void)
{
    struct mapent *entry;
    size_t largest = 0;

    for (entry = swapmap->m_map; entry->m_size != 0; entry++) {
        if ((entry->m_addr & (SWAP_IMAGE_ALIGN - 1)) != 0 ||
            (entry->m_size & (SWAP_IMAGE_ALIGN - 1)) != 0)
            panic ("swap exchange: unaligned map");
        if (entry->m_size > largest)
            largest = entry->m_size;
    }
    return largest;
}
#endif

/* A successful flash grant leaves the resident's complete departure route. */
size_t
swap_exchange_alloc (size_t data_blocks, size_t stack_blocks,
    size_t u_blocks, size_t *nextp, size_t addresses[3])
{
    size_t saved_next, span;
    int spl;

    spl = splhigh ();
#ifdef SWAP_IMAGE_ALIGN
    saved_next = *nextp;
    span = malloc3_contiguous_next (swapmap, data_blocks, stack_blocks,
        u_blocks, SWAP_IMAGE_ALIGN, nextp, addresses);
    if (span != 0 && swap_exchange_largest_run () < swap_exchange.span) {
        mfree (swapmap, span, addresses[0]);
        *nextp = saved_next;
        span = 0;
    }
    if (span != 0 && nextp == &swapnext)
        swap_cursor_publish (swapnext);
#else
    (void) nextp;
    saved_next = 0;
    span = malloc3 (swapmap, data_blocks, stack_blocks, u_blocks, addresses);
#endif
    splx (spl);
    (void) saved_next;
    return span;
}

/* Publish the next window owner only while its future route is available. */
int
swap_exchange_prepare (struct swap_exchange_state *previous,
    struct proc *incoming, int flash_backed)
{
    size_t required;
    int spl;

    required = flash_backed ? 0 : swap_image_span (incoming);
    spl = splhigh ();
    *previous = swap_exchange;
#ifdef SWAP_IMAGE_ALIGN
    if (required != 0 && swap_exchange_largest_run () < required) {
        splx (spl);
        return ENOMEM;
    }
#endif
    swap_exchange.owner = incoming;
    swap_exchange.span = required;
    splx (spl);
    return 0;
}

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
    size_t required;
    int spl;

    required = swap_image_span_geometry (data_size, stack_size, clean_text);
    spl = splhigh ();
    if (swap_exchange.owner != owner) {
        splx (spl);
        return ENOMEM;
    }
#ifdef SWAP_IMAGE_ALIGN
    if (swap_exchange_largest_run () < required) {
        splx (spl);
        return ENOMEM;
    }
#endif
    swap_exchange.span = required;
    splx (spl);
    return 0;
}

void
swap_exchange_release (struct proc *owner)
{
    int spl;

    spl = splhigh ();
    if (swap_exchange.owner != owner)
        panic ("swap exchange: release owner");
    swap_exchange.owner = NULL;
    swap_exchange.span = 0;
    splx (spl);
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

    bzero (reservation, sizeof (*reservation));
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
#ifdef SWAPRAM
    if (swapram_reserve (owner,
        (caddr_t) (source->p_daddr + reservation->text), reservation->data,
        (caddr_t) source->p_saddr, reservation->stack, USIZE)) {
        reservation->tier = SWAP_IMAGE_RAM;
    } else
#endif
    {
#ifdef SWAP_IMAGE_ALIGN
        if (swap_exchange_alloc (
            btod (source->p_dsize - reservation->text),
            btod (source->p_ssize), btod (USIZE),
            &next, reservation->addr) == 0)
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
        mfree (swapmap, swap_image_span (p), p->p_daddr);
#else
        mfree (swapmap, btod (p->p_dsize - swaptext (p)), p->p_daddr);
        mfree (swapmap, btod (p->p_ssize), p->p_saddr);
        mfree (swapmap, btod (USIZE), p->p_addr);
#endif
    }
    swap_exchange.owner = p;
    swap_exchange.span = swap_image_span (p);
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
swapin (p)
    register struct proc *p;
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
swapout (p, freecore, odata, ostack, reservation)
    register struct proc *p;
    int freecore __unused;
    u_int odata __unused, ostack __unused;
    struct swap_image_reservation *reservation;
{
    if (reservation == NULL || reservation->state != SWAP_IMAGE_RESERVED)
        panic ("swapout: no reservation");
    swap_image_copy_out (reservation, p);
    swap_image_commit (reservation, p);
}
