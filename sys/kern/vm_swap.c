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
 * Take one fresh flash extent for the image the reservation describes.
 * Refusal leaves the map and the next-fit cursor unchanged.
 */
static int
swap_image_reserve_flash (struct swap_image_reservation *reservation)
{
#ifdef SWAP_IMAGE_ALIGN
    size_t next = swapnext;

    if (malloc3_contiguous_next (swapmap, reservation->data_blocks,
        reservation->stack_blocks, btod (USIZE),
        SWAP_IMAGE_ALIGN, &next, reservation->addr) == 0)
        return ENOMEM;
    swapnext = next;
    swap_cursor_publish (swapnext);
#else
    if (malloc3 (swapmap, reservation->data_blocks,
        reservation->stack_blocks, btod (USIZE), reservation->addr) == 0)
        return ENOMEM;
#endif
    return 0;
}

#ifdef SWAP_IMAGE_ALIGN
/* Consume a free suffix without changing the next-fit cursor. */
static int
swap_exchange_tail_reserve (size_t base, size_t incoming_span,
    size_t outgoing_span)
{
    struct mapent *entry;
    size_t end, tail;

    if (outgoing_span <= incoming_span)
        return 1;
    end = base + incoming_span;
    tail = outgoing_span - incoming_span;
    for (entry = swapmap->m_map;
        entry->m_size != 0 && entry->m_addr < end; entry++)
        continue;
    if (entry->m_size < tail || entry->m_addr != end)
        return 0;
    entry->m_addr += tail;
    entry->m_size -= tail;
    if (entry->m_size == 0)
        do {
            *entry = entry[1];
        } while ((entry++)->m_size != 0);
    return 1;
}
#endif

/*
 * Reserve every byte the transition can write before changing either image.
 * A resident owns no flash blocks, so its destination is found here: the
 * SwapRAM pool, else the incoming image's own extent plus a tail of
 * max(0, outgoing span - incoming span) blocks directly after it
 * (SWAP_IMAGE_EXCHANGE, moved by swap_exchange_inplace()), else one fresh
 * extent for the whole outgoing image, which swapout() fills before
 * swapin() releases the incoming extent. ENOMEM means none of the three
 * exists; the map, the cursor and both images are unchanged.
 */
int
swap_exchange_switch (struct swap_image_reservation *reservation,
    struct proc *outgoing, struct proc *incoming, int pool_backed)
{
#ifdef SWAP_IMAGE_ALIGN
    int spl;
#endif

    if (outgoing == NULL) {
        bzero (reservation, sizeof (*reservation));
        return 0;
    }
    if (pool_backed)
        return swap_image_reserve (reservation, outgoing, outgoing,
            X_OLDSIZE, X_OLDSIZE);

#ifdef SWAP_IMAGE_ALIGN
    swap_image_reservation_init (reservation, outgoing, outgoing,
        X_OLDSIZE, X_OLDSIZE);
#ifdef SWAPRAM
    if (swapram_reserve (outgoing,
        (caddr_t) (outgoing->p_daddr + reservation->text),
        reservation->data, (caddr_t) outgoing->p_saddr,
        reservation->stack, USIZE)) {
        reservation->tier = SWAP_IMAGE_RAM;
        reservation->state = SWAP_IMAGE_RESERVED;
        return 0;
    }
#endif
    reservation->exchange_span = swap_image_span (incoming);
    spl = splhigh ();
    if (! swap_exchange_tail_reserve (incoming->p_daddr,
        reservation->exchange_span, reservation->span)) {
        splx (spl);
        reservation->exchange_span = 0;
        if (swap_image_reserve_flash (reservation) != 0)
            return ENOMEM;
        reservation->state = SWAP_IMAGE_RESERVED;
        return 0;
    }
    splx (spl);
    reservation->addr[0] = incoming->p_daddr;
    reservation->addr[1] = reservation->addr[0] +
        reservation->data_blocks;
    reservation->addr[2] = reservation->addr[1] +
        reservation->stack_blocks;
    reservation->tier = SWAP_IMAGE_EXCHANGE;
    reservation->state = SWAP_IMAGE_RESERVED;
    return 0;
#else
    (void) incoming;
    return swap_image_reserve (reservation, outgoing, outgoing,
        X_OLDSIZE, X_OLDSIZE);
#endif
}

/* Refusal changes neither the owner nor the map, cursor or image bytes. */
int
swap_image_reserve (struct swap_image_reservation *reservation,
    struct proc *owner, struct proc *source, u_int odata, u_int ostack)
{
    swap_image_reservation_init (reservation, owner, source, odata, ostack);
#ifdef SWAPRAM
    if (swapram_reserve (owner,
        (caddr_t) (source->p_daddr + reservation->text), reservation->data,
        (caddr_t) source->p_saddr, reservation->stack, USIZE)) {
        reservation->tier = SWAP_IMAGE_RAM;
    } else
#endif
    if (swap_image_reserve_flash (reservation) != 0)
        return ENOMEM;
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
        if (reservation->tier == SWAP_IMAGE_EXCHANGE) {
            if (reservation->span > reservation->exchange_span)
                mfree (swapmap,
                    reservation->span - reservation->exchange_span,
                    reservation->addr[0] + reservation->exchange_span);
        } else
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
    wakeup ((caddr_t) &runout);
    wakeup ((caddr_t) &runin);
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

#ifdef SWAP_IMAGE_ALIGN
#define SWAP_EXCHANGE_SECTOR_BYTES (SWAP_IMAGE_ALIGN * DEV_BSIZE)
#define SWAP_EXCHANGE_MAX_SECTORS \
    ((MAXMEM + USIZE + SWAP_EXCHANGE_SECTOR_BYTES - 1) / \
    SWAP_EXCHANGE_SECTOR_BYTES)

struct swap_exchange_segment {
    size_t image_start;
    size_t length;
    size_t window_start;
};

static u_char swap_exchange_sector[SWAP_EXCHANGE_SECTOR_BYTES];
static u_char swap_exchange_done[SWAP_EXCHANGE_MAX_SECTORS];

static void
swap_exchange_segments (struct proc *p, int resident,
    struct swap_exchange_segment segments[3])
{
    size_t text = swaptext (p);
    size_t data = p->p_dsize - text;

    segments[0].image_start = 0;
    segments[0].length = data;
    segments[0].window_start = resident ? p->p_daddr + text :
        (size_t) __user_data_start + text;
    segments[1].image_start = btod (data) * DEV_BSIZE;
    segments[1].length = p->p_ssize;
    segments[1].window_start = resident ? p->p_saddr : USER_TOP (p) -
        p->p_ssize;
    segments[2].image_start = segments[1].image_start +
        btod (p->p_ssize) * DEV_BSIZE;
    segments[2].length = USIZE;
    segments[2].window_start = resident ? p->p_addr : (size_t) &u0;
}

static int
swap_exchange_overlap (size_t first_start, size_t first_length,
    size_t second_start, size_t second_length)
{
    return first_length != 0 && second_length != 0 &&
        first_start < second_start + second_length &&
        second_start < first_start + first_length;
}

static int
swap_exchange_sector_piece (const struct swap_exchange_segment *segment,
    size_t sector, size_t *image_start, size_t *window_start, size_t *length)
{
    size_t sector_start = sector * SWAP_EXCHANGE_SECTOR_BYTES;
    size_t sector_end = sector_start + SWAP_EXCHANGE_SECTOR_BYTES;
    size_t segment_end = segment->image_start + segment->length;
    size_t start, end;

    start = segment->image_start > sector_start ? segment->image_start :
        sector_start;
    end = segment_end < sector_end ? segment_end : sector_end;
    if (start >= end)
        return 0;
    *image_start = start;
    *window_start = segment->window_start + start - segment->image_start;
    *length = end - start;
    return 1;
}

/*
 * A sector may be exchanged once its incoming bytes land on no window byte
 * that another unsaved outgoing sector still has to program. Such a sector
 * always exists. Both images map image offsets to window addresses in
 * strictly increasing order (data below stack below u0), so the map from an
 * incoming offset to the outgoing offset sharing its window byte is strictly
 * increasing where defined. A cycle of sectors waiting on each other would
 * give its highest sector one byte that lands in a lower sector and a lower
 * sector one byte that lands in the highest, which reverses that order.
 */
static int
swap_exchange_candidate_safe (size_t candidate, size_t sector_count,
    const struct swap_exchange_segment incoming[3],
    const struct swap_exchange_segment outgoing[3])
{
    size_t incoming_image, incoming_window, incoming_length;
    size_t outgoing_image, outgoing_window, outgoing_length;
    size_t other;
    int incoming_segment, outgoing_segment;

    for (incoming_segment = 0; incoming_segment < 3; incoming_segment++) {
        if (! swap_exchange_sector_piece (&incoming[incoming_segment],
            candidate, &incoming_image, &incoming_window, &incoming_length))
            continue;
        for (other = 0; other < sector_count; other++) {
            if (other == candidate || swap_exchange_done[other])
                continue;
            for (outgoing_segment = 0; outgoing_segment < 3;
                outgoing_segment++) {
                if (! swap_exchange_sector_piece (
                    &outgoing[outgoing_segment], other, &outgoing_image,
                    &outgoing_window, &outgoing_length))
                    continue;
                if (swap_exchange_overlap (incoming_window,
                    incoming_length, outgoing_window, outgoing_length))
                    return 0;
            }
        }
    }
    return 1;
}

static void
swap_exchange_program_sector (struct buf *bp, size_t flash_base,
    size_t sector, const struct swap_exchange_segment outgoing[3])
{
    size_t image_start, window_start, length;
    size_t sector_start = sector * SWAP_EXCHANGE_SECTOR_BYTES;
    int segment, writes = 0;

    for (segment = 0; segment < 3; segment++) {
        if (! swap_exchange_sector_piece (&outgoing[segment], sector,
            &image_start, &window_start, &length))
            continue;
        /* fl_raw() erases a sector only when a write starts on it. */
        if (writes == 0 && image_start != sector_start)
            panic ("swap exchange: geometry");
        if (swap_with_buf_result (bp,
            flash_base + btod (image_start), window_start, length,
            SWAP_IMAGE_WRITE) != 0)
            panic ("swap exchange: media fault");
        writes++;
    }
    if (writes == 0)
        panic ("swap exchange: geometry");
}

static void
swap_exchange_scatter_sector (size_t sector,
    const struct swap_exchange_segment incoming[3])
{
    size_t image_start, window_start, length;
    int segment;

    for (segment = 0; segment < 3; segment++) {
        if (! swap_exchange_sector_piece (&incoming[segment], sector,
            &image_start, &window_start, &length))
            continue;
        bcopy (swap_exchange_sector + image_start -
            sector * SWAP_EXCHANGE_SECTOR_BYTES,
            (caddr_t) window_start, length);
    }
}
#endif

/* Exchange two images only after the complete destination span is owned. */
void
swap_exchange_inplace (struct swap_image_reservation *reservation,
    struct proc *outgoing, struct proc *incoming)
{
#ifdef SWAP_IMAGE_ALIGN
    struct swap_exchange_segment outgoing_segments[3], incoming_segments[3];
    struct buf *bp;
    size_t candidate, completed, incoming_sectors, outgoing_sectors;
    size_t sector_count;
    int found;

    if (reservation->owner != outgoing ||
        reservation->state != SWAP_IMAGE_RESERVED ||
        reservation->tier != SWAP_IMAGE_EXCHANGE)
        panic ("swap exchange: reservation");
    incoming_sectors = reservation->exchange_span / SWAP_IMAGE_ALIGN;
    outgoing_sectors = reservation->span / SWAP_IMAGE_ALIGN;
    sector_count = incoming_sectors > outgoing_sectors ? incoming_sectors :
        outgoing_sectors;
    if (sector_count > SWAP_EXCHANGE_MAX_SECTORS)
        panic ("swap exchange: geometry");

    swap_exchange_segments (outgoing, 1, outgoing_segments);
    swap_exchange_segments (incoming, 0, incoming_segments);
    bzero (swap_exchange_done, sizeof (swap_exchange_done));
    outgoing->p_flag |= SLOCK;
    {
        int s = splclock ();

        u.u_ru.ru_nswap++;
        splx (s);
    }
#ifdef SWAPRAM
    storage_note (STORAGE_SWAPRAM_ATTEMPTS, 1);
#endif
    bp = geteblk ();
    for (completed = 0; completed < sector_count; completed++) {
        found = 0;
        for (candidate = 0; candidate < sector_count; candidate++) {
            if (swap_exchange_done[candidate] ||
                ! swap_exchange_candidate_safe (candidate, sector_count,
                incoming_segments, outgoing_segments))
                continue;
            found = 1;
            break;
        }
        if (! found)
            panic ("swap exchange: ordering");
        if (candidate < incoming_sectors &&
            swap_with_buf_result (bp,
            incoming->p_daddr + candidate * SWAP_IMAGE_ALIGN,
            (size_t) swap_exchange_sector, SWAP_EXCHANGE_SECTOR_BYTES,
            B_READ) != 0)
            panic ("swap exchange: media fault");
        if (candidate < outgoing_sectors)
            swap_exchange_program_sector (bp, reservation->addr[0],
                candidate, outgoing_segments);
        if (candidate < incoming_sectors)
            swap_exchange_scatter_sector (candidate, incoming_segments);
        swap_exchange_done[candidate] = 1;
    }
    brelse (bp);

    if (swaptext (incoming) && exec_text_restore (incoming,
        __user_data_start, swaptext (incoming)) != 0) {
        printf ("swapin: pid %d: executable text corrupt\n",
            incoming->p_pid);
        psignal (incoming, SIGKILL);
    }
    reservation->state = SWAP_IMAGE_COPIED;
    swap_image_commit (reservation, outgoing);
    if (reservation->exchange_span > reservation->span) {
        mfree (swapmap, reservation->exchange_span - reservation->span,
            reservation->addr[0] + reservation->span);
        wakeup ((caddr_t) &runin);
    }
    incoming->p_daddr = (size_t) __user_data_start;
    incoming->p_saddr = USER_TOP (incoming) - incoming->p_ssize;
    incoming->p_addr = (size_t) &u0;
    if (incoming->p_stat == SRUN)
        setrq (incoming);
    incoming->p_flag |= SLOAD;
    incoming->p_time = 0;
#ifdef UCB_METER
    cnt.v_swpin++;
#endif
#else
    (void) reservation;
    (void) outgoing;
    (void) incoming;
    panic ("swap exchange: unsupported");
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
