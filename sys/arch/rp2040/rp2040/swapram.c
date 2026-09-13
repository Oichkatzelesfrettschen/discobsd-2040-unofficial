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
 * Nothing on this path sleeps or allocates. The encoder, the decoder, and
 * the segment table are file-scope statics, which is safe only because
 * swapout and swapin run to completion without a context switch: the flash
 * path sleeps inside swap() on B_DONE, and this path has no geteblk, no
 * sleep, and no buffer. The same property keeps the kernel stack out of it,
 * which matters because USIZE is 3072 bytes for the u structure and the
 * kernel stack together.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <machine/swapram.h>

#include "heatshrink_encoder.h"
#include "heatshrink_decoder.h"

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

static u_char           sr_pool[SWAPRAM_KB * 1024];
static struct swapram_seg sr_seg[NPROC + 1];
static struct swapram_pool sr_map;
static int              sr_ready;

/*
 * One entry per proc slot, indexed by p - proc, because a swapped-out
 * process keeps its slot and struct proc has no room for three more fields.
 */
static struct sr_ent {
    unsigned int    e_off;              /* pool offset of the reservation */
    unsigned int    e_res;              /* bytes reserved */
    unsigned int    e_cap[SR_NSEG];     /* bytes reserved per segment */
    unsigned int    e_clen[SR_NSEG];    /* compressed bytes per segment */
    unsigned int    e_rlen[SR_NSEG];    /* the length swapin must produce */
    unsigned int    e_fill;             /* bytes of the reservation used */
    char            e_live;             /* an image is here */
} sr_tab[NPROC];

static heatshrink_encoder sr_enc;
static heatshrink_decoder sr_dec;

/* One line per swapout and swapin names the tier that took the image. */
int swapramdebug = 0;		/* off by default; the tier is verified. flip to 1 to trace swaps */

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
    unsigned int in = 0, out = 0, moved = 0;
    u_char scratch[64];
    u_char *buf;
    size_t room, n;
    HSE_poll_res pres;
    HSE_finish_res fres;

    heatshrink_encoder_reset (&sr_enc);
    for (;;) {
        if (in < len) {
            if (heatshrink_encoder_sink (&sr_enc, (uint8_t *) src + in,
                len - in, &n) < 0)
                return -1;
            if (n == 0 && out == moved)
                return -1;      /* neither side advanced: no termination */
            in += n;
        } else {
            fres = heatshrink_encoder_finish (&sr_enc);
            if (fres < 0)
                return -1;
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
            pres = heatshrink_encoder_poll (&sr_enc, buf, room, &n);
            if (pres < 0)
                return -1;
            if (buf == scratch && dst != NULL && n > 0)
                return -1;      /* more than cap bytes: overflow */
            out += n;
        } while (pres == HSER_POLL_MORE);
    }
    return (int) out;
}

/*
 * Expand clen bytes at pool offset off to dst, refusing to write more than
 * rlen. Returns the expanded length, which the caller checks against the
 * length swapout recorded.
 */
static int
sr_expand (unsigned int off, unsigned int clen, caddr_t dst,
    unsigned int rlen)
{
    unsigned int in = 0, out = 0, moved = 0;
    size_t n;
    HSD_poll_res pres;
    HSD_finish_res fres;

    heatshrink_decoder_reset (&sr_dec);
    while (in < clen && out < rlen) {
        if (heatshrink_decoder_sink (&sr_dec, sr_pool + off + in,
            clen - in, &n) < 0)
            return -1;
        if (n == 0 && out == moved)
            return -1;          /* neither side advanced: no termination */
        in += n;
        moved = out;
        do {
            if (out == rlen)
                break;
            pres = heatshrink_decoder_poll (&sr_dec, (uint8_t *) dst + out,
                rlen - out, &n);
            if (pres < 0)
                return -1;
            out += n;
        } while (pres == HSDR_POLL_MORE);
    }
    for (moved = out + 1; out < rlen; moved = out) {
        if (out == moved)
            return -1;          /* finish says more and poll yields none */
        fres = heatshrink_decoder_finish (&sr_dec);
        if (fres < 0)
            return -1;
        if (fres == HSDR_FINISH_DONE)
            break;
        do {
            if (out == rlen)
                break;
            pres = heatshrink_decoder_poll (&sr_dec, (uint8_t *) dst + out,
                rlen - out, &n);
            if (pres < 0)
                return -1;
            out += n;
        } while (pres == HSDR_POLL_MORE);
    }
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
    unsigned int want;
    int dn = 0, sn = 0;

    if (! sr_ready) {
        swapram_pool_init (&sr_map, sr_pool, sizeof sr_pool, sr_seg,
            NPROC + 1);
        sr_ready = 1;
    }
    if (e->e_live)
        panic ("swapram: image already resident");

    if (dlen && (dn = sr_encode (dsrc, dlen, NULL, 0)) < 0)
        return 0;
    if (slen && (sn = sr_encode (ssrc, slen, NULL, 0)) < 0)
        return 0;
    want = dn + sn + SR_WORST (ulen);
    if (swapram_pool_alloc (&sr_map, want, &e->e_off) < 0) {
        if (swapramdebug)
            printf ("swapram: pid %d flash %u bytes (%u compressed), pool %u free\n",
                p->p_pid, (u_int) (dlen + slen + ulen), want,
                swapram_pool_avail (&sr_map));
        return 0;
    }
    e->e_res = want;
    e->e_fill = 0;
    e->e_rlen[SR_DATA] = dlen;
    e->e_rlen[SR_STACK] = slen;
    e->e_rlen[SR_U] = ulen;
    e->e_cap[SR_DATA] = dn;
    e->e_cap[SR_STACK] = sn;
    e->e_cap[SR_U] = SR_WORST (ulen);
    e->e_clen[SR_DATA] = 0;
    e->e_clen[SR_STACK] = 0;
    e->e_clen[SR_U] = 0;
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
    int n;

    if (len != e->e_rlen[seg])
        panic ("swapram: segment length changed");
    if (len == 0) {
        e->e_clen[seg] = 0;
        return;
    }
    n = sr_encode (src, len, sr_pool + e->e_off + e->e_fill,
        e->e_cap[seg]);
    if (n < 0)
        panic ("swapram: reservation overflow");
    if (seg != SR_U && (unsigned int) n != e->e_cap[seg])
        panic ("swapram: count mismatch");
    e->e_clen[seg] = n;
    e->e_fill += n;
}

/*
 * Release the unused tail of the reservation, which is the u area's slack
 * alone, and mark the image resident.
 */
void
swapram_commit (struct proc *p)
{
    struct sr_ent *e = sr_slot (p);

    if (swapram_pool_trim (&sr_map, e->e_off, e->e_res, e->e_fill) < 0)
        panic ("swapram: trim");
    e->e_res = e->e_fill;
    e->e_live = 1;
    if (swapramdebug)
        printf ("swapram: pid %d ram %u -> %u bytes, pool %u free\n",
            p->p_pid,
            (u_int) (e->e_rlen[SR_DATA] + e->e_rlen[SR_STACK] +
                e->e_rlen[SR_U]),
            e->e_fill, swapram_pool_avail (&sr_map));
}

/* True when swapin must read this process back out of the pool. */
int
swapram_present (struct proc *p)
{
    return sr_slot (p)->e_live;
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
        n = sr_expand (off, e->e_clen[seg], dst[seg], e->e_rlen[seg]);
        if (n != (int) e->e_rlen[seg])
            panic ("swapram: short expand");
        off += e->e_clen[seg];
    }
    if (swapram_pool_free (&sr_map, e->e_off, e->e_res) < 0)
        panic ("swapram: free");
    if (swapramdebug)
        printf ("swapram: pid %d in %u -> %u bytes, pool %u free\n",
            p->p_pid, e->e_fill,
            (u_int) (e->e_rlen[SR_DATA] + e->e_rlen[SR_STACK] +
                e->e_rlen[SR_U]),
            swapram_pool_avail (&sr_map));
    e->e_live = 0;
    e->e_fill = 0;
    e->e_res = 0;
}
