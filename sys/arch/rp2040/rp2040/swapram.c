/*
 * The compressed RAM tier in front of the flash swap unit.
 *
 * kern/vm_swap.c writes three segments per swapout -- the data area past
 * the clean text, the stack, and the USIZE u area -- and dev/flash.c turns
 * each 1 KB of that into a 4 KB read-modify-write of QSPI NOR. This file
 * compresses the same three segments with heatshrink into a bss pool and
 * keeps them there whenever the pool can hold the worst-case output; when
 * it cannot, swapout takes the flash path with nothing changed.
 *
 * The pool holds compressed bytes only. A process whose image is here holds
 * no swapmap blocks, and swapout leaves p_daddr, p_saddr, and p_addr zero
 * to say so.
 *
 * Nothing on the swapout and swapin paths sleeps or allocates. The
 * encoder, the decoder, and the segment table are file-scope statics,
 * which is safe only because swapout and swapin run to completion without
 * a context switch: the flash path sleeps inside swap() on B_DONE, and
 * this path has no geteblk, no sleep, and no buffer. The same property
 * keeps the kernel stack out of it, which matters because USIZE is 3072
 * bytes for the u structure and the kernel stack together. The evacuation
 * request runs in the swapper, where sleeping in swap_with_buf() is the
 * norm; it uses the decoder while no swapin can, and the pool is closed to
 * new images for the evacuation's duration.
 */

#include <sys/param.h>
#include <sys/storage_stats.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <machine/swapram.h>

#include <machine/hsx_decoder.h>

#include "heatshrink_encoder.h"

#define SR_DATA         SWAPRAM_DATA
#define SR_STACK        SWAPRAM_STACK
#define SR_U            SWAPRAM_U
#define SR_NSEG         SWAPRAM_NSEG

/*
 * heatshrink emits a literal as nine bits, so the encoder's worst case is
 * the input plus an eighth, and the flush adds at most one byte per stream.
 * The u area is reserved at this size, because the clock interrupt keeps
 * writing into the current process's u between the counting pass and the
 * capture, so its exact length is not knowable in advance. The data and
 * stack segments, which are the bulk of an image, are reserved at the
 * length a counting encode measures instead.
 */
#define SR_WORST(n)     ((n) + (n) / 8 + 4)

/*
 * The pool lives in a NOLOAD section outside bss clearing. Every stored byte
 * is written before it is read, so the pool needs no reset-time initializer.
 */
u_char                  swapram_pool_mem[SWAPRAM_KB * 1024]
#ifdef __ELF__          /* the host test on macOS builds this as Mach-O */
                            __attribute__ ((section (".swapram_pool")))
#endif
                            ;
#define sr_pool         swapram_pool_mem
static struct swapram_seg sr_seg[NPROC + 1];
static struct swapram_pool sr_map;
static int              sr_ready;
static int              sr_spools;

/*
 * One entry per proc slot, indexed by p - proc, because a swapped-out
 * process keeps its slot and struct proc has no room for three more fields.
 */
#if SWAPRAM_KB * 1024 <= 65535
typedef u_short sr_offset_t;
#else
typedef unsigned int sr_offset_t;
#endif

static struct sr_ent {
    unsigned int    e_rlen[SR_NSEG];    /* the length swapin must produce */
    sr_offset_t     e_off;              /* pool offset of the reservation */
    sr_offset_t     e_res;              /* bytes reserved, then bytes used */
    sr_offset_t     e_clen[SR_NSEG];    /* capacity, then compressed bytes */
    u_char          e_live;             /* an image is here */
} sr_tab[NPROC];

#if SWAPRAM_KB * 1024 <= 65535
_Static_assert (sizeof (struct sr_ent) == 24,
    "SwapRAM process records must remain compact");
#endif

static unsigned int
sr_compressed_before (struct sr_ent *e, int stop)
{
    unsigned int size = 0;
    int seg;

    for (seg = 0; seg < stop; seg++)
        size += e->e_clen[seg];
    return size;
}

static unsigned int
sr_compressed_size (struct sr_ent *e)
{
    return sr_compressed_before (e, SR_NSEG);
}

#ifdef SWAP_IMAGE_ALIGN
static size_t
sr_flash_span (struct sr_ent *e)
{
    size_t blocks = btod (e->e_rlen[SR_DATA]) +
        btod (e->e_rlen[SR_STACK]) + btod (e->e_rlen[SR_U]);

    return (blocks + SWAP_IMAGE_ALIGN - 1) & ~(SWAP_IMAGE_ALIGN - 1);
}
#endif

/*
 * Admission: swapram_out takes an image only while this is set. An
 * evacuation clears it first, so no image enters the pool behind the move.
 */
static int              sr_admit = 1;

/*
 * The evacuation request the sysctl posts and the swapper services:
 * SWAPRAM_EVAC_IDLE, then SWAPRAM_EVAC_PENDING while the request waits,
 * then the result.
 */
int                     swapram_evac = SWAPRAM_EVAC_IDLE;

#ifdef RP2040
extern unsigned char swapram_codec_work[SWAPRAM_CODEC_WORK_BYTES];
#else
unsigned char swapram_codec_work[SWAPRAM_CODEC_WORK_BYTES]
    __attribute__ ((aligned (4)));
#endif
static u_char sr_codec_owner;

_Static_assert (sizeof (heatshrink_encoder) <= SWAPRAM_CODEC_WORK_BYTES,
    "SwapRAM encoder must fit the shared codec workspace");
_Static_assert (sizeof (struct hsx_dec) <= SWAPRAM_CODEC_WORK_BYTES,
    "SwapRAM decoder must fit the shared codec workspace");
_Static_assert (HEATSHRINK_STATIC_WINDOW_BITS == HSXD_WINDOW_BITS &&
    HEATSHRINK_STATIC_LOOKAHEAD_BITS <= HSXD_LOOKAHEAD_MAX,
    "the port decoder must implement the encoder's window and lookahead");

void *
swapram_codec_acquire (unsigned int owner)
{
    if (owner == 0 || sr_codec_owner != 0)
        panic ("swapram: codec workspace busy");
    sr_codec_owner = owner;
    return swapram_codec_work;
}

void
swapram_codec_release (unsigned int owner)
{
    if (owner == 0 || sr_codec_owner != owner)
        panic ("swapram: codec workspace owner");
    sr_codec_owner = 0;
}

/* One line per swapout and swapin names the tier that took the image. */
int swapramdebug = 0;		/* off by default; the tier is verified. flip to 1 to trace swaps */

static void
sr_pool_init_once (void)
{
    if (sr_ready)
        return;
    swapram_pool_init (&sr_map, sr_pool, sizeof sr_pool, sr_seg,
        NPROC + 1);
    sr_ready = 1;
}

/*
 * Exec argument spools share the byte allocator with compressed images and
 * release every reservation on the exec path's common cleanup path.
 */
int
swapram_spool_alloc (unsigned int size, unsigned int *offp)
{
    sr_pool_init_once ();
    if (! sr_admit || swapram_pool_alloc (&sr_map, size, offp) < 0)
        return -1;
    sr_spools++;
    return 0;
}

void
swapram_spool_write (unsigned int off, unsigned int pos, const void *src,
    unsigned int len)
{
    if (off > sizeof sr_pool || pos > sizeof sr_pool - off ||
        len > sizeof sr_pool - off - pos)
        panic ("swapram: spool write");
    bcopy (src, sr_pool + off + pos, len);
}

void
swapram_spool_read (unsigned int off, unsigned int pos, void *dst,
    unsigned int len)
{
    if (off > sizeof sr_pool || pos > sizeof sr_pool - off ||
        len > sizeof sr_pool - off - pos)
        panic ("swapram: spool read");
    bcopy (sr_pool + off + pos, dst, len);
}

void
swapram_spool_free (unsigned int off, unsigned int size)
{
    if (sr_spools <= 0 || swapram_pool_free (&sr_map, off, size) < 0)
        panic ("swapram: spool free");
    sr_spools--;
}

static struct sr_ent *
sr_slot(struct proc *p)
{
    int i = p - proc;

    if (i < 0 || i >= NPROC)
        panic ("swapram: proc slot");
    return &sr_tab[i];
}

/*
 * Encode len bytes at src. With dst set, the output goes to dst and the
 * call returns the compressed length, or -1 when that would exceed cap:
 * once cap bytes are out, the encoder is polled into a one-byte scratch
 * and any byte it yields is the overflow. With dst NULL nothing is kept,
 * every poll lands in the scratch, and the return value is the length
 * the same input would produce: the encoder is deterministic, so a
 * counting pass and a writing pass over unchanged bytes agree exactly,
 * which swapram_put checks. The loop follows heatshrink's own driver:
 * poll until the result stops saying HSER_POLL_MORE, because a poll that
 * copies nothing may still have output pending.
 */
static int
sr_encode (caddr_t src, unsigned int len, u_char *dst, unsigned int cap)
{
    heatshrink_encoder *encoder;
    unsigned int in = 0, out = 0, moved = 0;
    u_char scratch[64];
    u_char *buf;
    size_t room, n;
    HSE_poll_res pres;
    HSE_finish_res fres;
    int result = -1;

    encoder = swapram_codec_acquire (SWAPRAM_CODEC_ENCODER);
    heatshrink_encoder_reset (encoder);
    for (;;) {
        if (in < len) {
            if (heatshrink_encoder_sink (encoder, (uint8_t *) src + in,
                len - in, &n) < 0)
                goto done;
            if (n == 0 && out == moved)
                goto done;      /* neither side advanced: no termination */
            in += n;
        } else {
            fres = heatshrink_encoder_finish (encoder);
            if (fres < 0)
                goto done;
            if (fres == HSER_FINISH_DONE)
                break;
        }
        moved = out;
        do {
            if (dst != NULL && out < cap) {
                buf = dst + out;
                room = cap - out;
            } else {
                buf = scratch;
                room = dst != NULL ? 1 : sizeof scratch;
            }
            pres = heatshrink_encoder_poll (encoder, buf, room, &n);
            if (pres < 0)
                goto done;
            if (buf == scratch && dst != NULL && n > 0)
                goto done;      /* more than cap bytes: overflow */
            out += n;
        } while (pres == HSER_POLL_MORE);
    }
    result = (int) out;
done:
    swapram_codec_release (SWAPRAM_CODEC_ENCODER);
    return result;
}

/*
 * Expand clen bytes at pool offset off, refusing to produce more than
 * rlen. With dst set the bytes land there. With dst NULL they go through
 * the payload of the busy cache buffer bp a block at a time to the flash
 * blocks from blk, the path an evacuation takes: the decoder runs into its
 * own history ring in pieces that do not cross the ring end or the block
 * end, and each piece is copied to the block. Returns the expanded length,
 * which the caller checks against the length swapout recorded; a stream
 * that ends early yields fewer bytes and no flash write for the partial
 * block.
 */
static int
sr_expand (unsigned int off, unsigned int clen, caddr_t dst,
    unsigned int rlen, size_t blk, struct buf *bp)
{
    struct hsx_dec *decoder;
    unsigned int out = 0, fill = 0, room, n, ring;

    decoder = swapram_codec_acquire (SWAPRAM_CODEC_DECODER);
    hsx_dec_init (decoder, HEATSHRINK_STATIC_LOOKAHEAD_BITS);
    hsx_dec_input (decoder, sr_pool + off, clen);
    if (dst != NULL) {
        out = hsx_dec_run (decoder, (u_char *) dst, ~0U, rlen);
    } else {
        while (out < rlen) {
            ring = out & (HSXD_WINDOW - 1);
            room = rlen - out;
            if (room > DEV_BSIZE - fill)
                room = DEV_BSIZE - fill;
            if (room > HSXD_WINDOW - ring)
                room = HSXD_WINDOW - ring;
            n = hsx_dec_run (decoder, decoder->win, HSXD_WINDOW - 1, room);
            bcopy (decoder->win + ring, (u_char *) bp->b_addr + fill, n);
            out += n;
            fill += n;
            if (fill == DEV_BSIZE || out == rlen) {
                swap_with_buf (bp, blk, (size_t) bp->b_addr, fill,
                    B_WRITE | B_SWAPIMAGE);
                blk += btod (fill);
                fill = 0;
            }
            if (n < room)
                break;
        }
    }
    swapram_codec_release (SWAPRAM_CODEC_DECODER);
    return (int) out;
}

/*
 * Claim pool space for a whole image before any of it is compressed.
 * Returns 1 when the tier takes the image, 0 when swapout must use flash.
 * The data and stack segments are counted first and reserved at exactly
 * the length they will compress to; the u area gets the encoder's worst
 * case. Every later put therefore fits by construction and swapout never
 * has to unwind a half-written image, while a compressible image is
 * admitted whenever its real size fits rather than its raw size.
 */
int
swapram_out (struct proc *p, caddr_t dsrc, size_t dlen, caddr_t ssrc,
    size_t slen, size_t ulen)
{
    struct sr_ent *e = sr_slot (p);
    unsigned int off, want;
    int dn = 0, sn = 0;

    storage_note(STORAGE_SWAPRAM_ATTEMPTS, 1);
    sr_pool_init_once ();
    if (e->e_live)
        panic ("swapram: image already resident");
    /*
     * Packed-text restoration holds the shared decoder across rdwri sleeps.
     * A swapout scheduled during that interval must use flash instead of
     * entering the owner-checked codec workspace.
     */
    if (! sr_admit || sr_codec_owner == SWAPRAM_CODEC_PACKED_TEXT)
        return 0;

    if (dlen && (dn = sr_encode (dsrc, dlen, NULL, 0)) < 0)
        return 0;
    if (slen && (sn = sr_encode (ssrc, slen, NULL, 0)) < 0)
        return 0;
    want = dn + sn + SR_WORST (ulen);
    if (swapram_pool_alloc (&sr_map, want, &off) < 0) {
        if (swapramdebug)
            printf ("swapram: pid %d flash %u bytes (%u compressed), pool %u free\n",
                p->p_pid, (u_int) (dlen + slen + ulen), want,
                swapram_pool_avail (&sr_map));
        return 0;
    }
    e->e_off = off;
    e->e_res = want;
    e->e_rlen[SR_DATA] = dlen;
    e->e_rlen[SR_STACK] = slen;
    e->e_rlen[SR_U] = ulen;
    e->e_clen[SR_DATA] = dn;
    e->e_clen[SR_STACK] = sn;
    e->e_clen[SR_U] = SR_WORST (ulen);
    storage_note(STORAGE_SWAPRAM_ADMISSIONS, 1);
    return 1;
}

/*
 * Compress one segment into the reservation swapram_out made. The data
 * and stack segments must produce exactly the length the counting pass
 * measured: a shorter or longer result means the bytes changed between
 * the passes or the encoder is not deterministic, and either is a bug
 * rather than a full pool. The u area may come out shorter than its
 * worst-case reservation and never longer.
 */
void
swapram_put (struct proc *p, int seg, caddr_t src, size_t len)
{
    struct sr_ent *e = sr_slot (p);
    unsigned int cap, fill;
    int n;

    if (len != e->e_rlen[seg])
        panic ("swapram: segment length changed");
    if (len == 0) {
        e->e_clen[seg] = 0;
        return;
    }
    cap = e->e_clen[seg];
    fill = sr_compressed_before (e, seg);
    n = sr_encode (src, len, sr_pool + e->e_off + fill, cap);
    if (n < 0)
        panic ("swapram: reservation overflow");
    if (seg != SR_U && (unsigned int) n != cap)
        panic ("swapram: count mismatch");
    e->e_clen[seg] = n;
}

/*
 * Release the unused tail of the reservation, which is the u area's slack
 * alone, and mark the image resident.
 */
void
swapram_commit (struct proc *p)
{
    struct sr_ent *e = sr_slot (p);
    unsigned int used = sr_compressed_size (e);

    if (swapram_pool_trim (&sr_map, e->e_off, e->e_res, used) < 0)
        panic ("swapram: trim");
    e->e_res = used;
    e->e_live = 1;
    if (swapramdebug)
        printf ("swapram: pid %d ram %u -> %u bytes, pool %u free\n",
            p->p_pid,
            (u_int) (e->e_rlen[SR_DATA] + e->e_rlen[SR_STACK] +
                e->e_rlen[SR_U]),
            used, swapram_pool_avail (&sr_map));
}

/* True when swapin must read this process back out of the pool. */
int
swapram_present (struct proc *p)
{
    return sr_slot (p)->e_live;
}

/*
 * Copy a prefix of a resident image's u area without removing the image.
 * Process sysctl needs fields from the first block while a process remains
 * swapped out; p_addr is deliberately zero for a SwapRAM image and therefore
 * cannot name raw flash.  Stopping the decoder after the requested prefix
 * avoids a permanent u-area staging buffer.
 */
int
swapram_uarea_prefix (struct proc *p, void *dst, unsigned int len)
{
    struct sr_ent *e = sr_slot (p);
    unsigned int off;
    int n;

    if (! e->e_live || len > e->e_rlen[SR_U])
        return -1;
    off = e->e_off + e->e_clen[SR_DATA] + e->e_clen[SR_STACK];
    n = sr_expand (off, e->e_clen[SR_U], dst, len, 0, NULL);
    return n == (int) len ? 0 : -1;
}

/*
 * Expand the three segments to the addresses swapin computed and release
 * the pool space. A length that disagrees with what swapout recorded is a
 * panic, not a partial image: the caller has already committed the core.
 */
void
swapram_in (struct proc *p, caddr_t ddst, caddr_t sdst, caddr_t udst)
{
    struct sr_ent *e = sr_slot (p);
    caddr_t dst[SR_NSEG];
    unsigned int off = e->e_off;
    int seg, n;

    if (! e->e_live)
        panic ("swapram: no image");
    dst[SR_DATA] = ddst;
    dst[SR_STACK] = sdst;
    dst[SR_U] = udst;
    for (seg = 0; seg < SR_NSEG; seg++) {
        if (e->e_rlen[seg] == 0)
            continue;
        n = sr_expand (off, e->e_clen[seg], dst[seg], e->e_rlen[seg], 0,
            NULL);
        if (n != (int) e->e_rlen[seg])
            panic ("swapram: short expand");
        off += e->e_clen[seg];
    }
    if (swapram_pool_free (&sr_map, e->e_off, e->e_res) < 0)
        panic ("swapram: free");
    if (swapramdebug)
        printf ("swapram: pid %d in %u -> %u bytes, pool %u free\n",
            p->p_pid, e->e_res,
            (u_int) (e->e_rlen[SR_DATA] + e->e_rlen[SR_STACK] +
                e->e_rlen[SR_U]),
            swapram_pool_avail (&sr_map));
    e->e_live = 0;
    e->e_res = 0;
}

/* Open or close the pool to new images. */
void
swapram_admit (int on)
{
    sr_admit = on;
}

/* Images the pool holds. */
int
swapram_images (void)
{
    int i, n = 0;

    for (i = 0; i < NPROC; i++)
        n += sr_tab[i].e_live;
    return n;
}

/*
 * Move every image in the pool to flash swap, all or none. The flash
 * blocks for every image are reserved first, at the expanded sizes
 * swapin will read, and one reservation that fails releases them all
 * and leaves the pool as it was. Only then is each image expanded a
 * block at a time into its extents, and a process switches to them
 * (p_daddr, p_saddr, p_addr, the same words swapout sets on the flash
 * path) after its last block is written, so a process is on one tier
 * or the other at every moment and a panic part way strands nothing.
 * The pool entry is freed last. Admission stays as the caller set it;
 * an evacuation with images still admissible is a moment's emptiness.
 * Runs in the swapper, the one context that swaps in, so no image leaves
 * the pool under it; swap_with_buf() sleeps, and a swapout from another
 * process meanwhile finds admission closed and takes flash. One cache
 * buffer supplies both the busy I/O header and the expansion payload for
 * the complete evacuation.
 */
int
swapram_evacuate (void)
{
    struct sr_ent *e;
    struct proc *p, *reserved;
    struct buf *bp;
    unsigned int off;
    size_t blocks[SR_NSEG], next;
    int i, images = 0, j, seg, n;

    next = swapnext;
    for (i = 0; i < NPROC; i++) {
        e = &sr_tab[i];
        if (! e->e_live)
            continue;
        images++;
        p = &proc[i];
        if (p->p_flag & SLOAD)
            panic ("swapram: evacuate loaded");
        if (p->p_daddr || p->p_saddr || p->p_addr)
            panic ("swapram: flash blocks already assigned");
#ifdef SWAP_IMAGE_ALIGN
        if (malloc3_contiguous_next (swapmap, btod (e->e_rlen[SR_DATA]),
            btod (e->e_rlen[SR_STACK]), btod (e->e_rlen[SR_U]),
            SWAP_IMAGE_ALIGN, &next, blocks) == 0) {
#else
        if (malloc3 (swapmap, btod (e->e_rlen[SR_DATA]),
            btod (e->e_rlen[SR_STACK]), btod (e->e_rlen[SR_U]),
            blocks) == 0) {
#endif
            for (j = 0; j < i; j++) {
                e = &sr_tab[j];
                if (! e->e_live)
                    continue;
                reserved = &proc[j];
#ifdef SWAP_IMAGE_ALIGN
                mfree (swapmap, sr_flash_span (e), reserved->p_daddr);
#else
                for (seg = 0; seg < SR_NSEG; seg++)
                    if (e->e_rlen[seg])
                        mfree (swapmap, btod (e->e_rlen[seg]),
                            seg == SR_DATA ? reserved->p_daddr :
                            seg == SR_STACK ? reserved->p_saddr :
                            reserved->p_addr);
#endif
                reserved->p_daddr = 0;
                reserved->p_saddr = 0;
                reserved->p_addr = 0;
            }
            if (swapramdebug)
                printf ("swapram: evacuate: no flash for pid %d\n",
                    p->p_pid);
            return -1;
        }
        p->p_daddr = blocks[SR_DATA];
        p->p_saddr = blocks[SR_STACK];
        p->p_addr = blocks[SR_U];
    }
    if (images == 0)
        return 0;
    swapnext = next;
    swap_cursor_publish (swapnext);
    bp = geteblk ();
    for (i = 0; i < NPROC; i++) {
        e = &sr_tab[i];
        if (! e->e_live)
            continue;
        p = &proc[i];
        off = e->e_off;
        for (seg = 0; seg < SR_NSEG; seg++) {
            if (e->e_rlen[seg] == 0)
                continue;
            n = sr_expand (off, e->e_clen[seg], NULL, e->e_rlen[seg],
                seg == SR_DATA ? p->p_daddr :
                seg == SR_STACK ? p->p_saddr : p->p_addr, bp);
            if (n != (int) e->e_rlen[seg])
                panic ("swapram: evacuate expand");
            off += e->e_clen[seg];
        }
        if (swapram_pool_free (&sr_map, e->e_off, e->e_res) < 0)
            panic ("swapram: evacuate free");
        if (swapramdebug)
            printf ("swapram: pid %d evacuated %u -> %u bytes at %u\n",
                p->p_pid, e->e_res,
                (u_int) (e->e_rlen[SR_DATA] + e->e_rlen[SR_STACK] +
                    e->e_rlen[SR_U]), (u_int) p->p_daddr);
        e->e_live = 0;
        e->e_res = 0;
    }
    brelse (bp);
    return 0;
}

/*
 * The swapper calls this at the top of its loop. A pending evacuation closes
 * admission, records whether every image reached flash, and reopens the pool.
 */
void
swapram_service (void)
{
    if (swapram_evac == SWAPRAM_EVAC_PENDING) {
        swapram_admit (0);
        swapram_evac = swapram_evacuate () == 0 ? SWAPRAM_EVAC_DONE :
            SWAPRAM_EVAC_NOFLASH;
        swapram_admit (1);
    }
}
