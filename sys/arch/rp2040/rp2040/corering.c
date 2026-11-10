/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Single-writer rings and the SIO FIFO doorbell described in corering.h.
 *
 * Two orderings keep every published entry covered by a pending doorbell
 * or by a snapshot that already includes it:
 *
 *   producer: store the entry, store cr_windex, then ring the doorbell;
 *   consumer: empty the inbound FIFO, then snapshot cr_windex.
 *
 * A doorbell pushed after the consumer emptied the FIFO stays valid and
 * raises the interrupt again. A doorbell the consumer popped was pushed
 * before the snapshot, after the index store it announces, so the snapshot
 * includes that entry. A producer that finds the outbound FIFO full skips
 * the push: the full FIFO was observed after the index store, and the
 * consumer empties it before its next snapshot.
 */

#include "corering.h"

/*
 * SIO inter-core FIFO, datasheet 2.3.1.4. Each core reaches its own pair of
 * views at the same addresses: FIFO_WR feeds the other core, FIFO_RD and
 * VLD describe the inbound FIFO, RDY the outbound one. WOF and ROE are
 * sticky and hold the FIFO interrupt asserted until written back as ones.
 */
#define	SIO_BASE		0xd0000000UL
#define	SIO_FIFO_ST		0x050
#define	SIO_FIFO_WR		0x054
#define	SIO_FIFO_RD		0x058

#define	FIFO_ST_VLD		(1U << 0)	/* inbound FIFO holds a word */
#define	FIFO_ST_RDY		(1U << 1)	/* outbound FIFO has room */
#define	FIFO_ST_WOF		(1U << 2)	/* wrote a full outbound FIFO */
#define	FIFO_ST_ROE		(1U << 3)	/* read an empty inbound FIFO */
#define	FIFO_ST_ERR		(FIFO_ST_WOF | FIFO_ST_ROE)

#ifndef CRING_RD
#define	CRING_RD(off) \
	(*(volatile unsigned int *)(SIO_BASE + (off)))
#define	CRING_WR(off, val) \
	(*(volatile unsigned int *)(SIO_BASE + (off)) = (val))
#endif

#define	CRING_MASK	(CRING_SLOTS - 1)

_Static_assert((CRING_SLOTS & CRING_MASK) == 0,
    "CRING_SLOTS is a power of two");
_Static_assert(sizeof(struct cring_entry) == 8,
    "a ring entry is two words");

/*
 * An index is one aligned 32-bit word, which a Cortex-M0+ loads and stores
 * in a single bus transfer. For ARMv6-M the builtins compile to that plain
 * access with a DMB after the load and before the store, so the entry
 * stores reach the bus ahead of the index that publishes them. They also
 * give the host gate's thread sanitizer the ordering between the cores.
 *
 * The four helpers below are always inlined, so they execute from whatever
 * section CRING_FN gives their callers.
 */
#define	CRING_INLINE	static inline __attribute__((always_inline))

CRING_INLINE unsigned int
index_load(const unsigned int *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

CRING_INLINE void
index_store(unsigned int *p, unsigned int v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

/* A full outbound FIFO already holds a doorbell for the other core. */
CRING_INLINE void
bell_ring(void)
{
	if (CRING_RD(SIO_FIFO_ST) & FIFO_ST_RDY)
		CRING_WR(SIO_FIFO_WR, 0);
}

CRING_INLINE void
bell_clear(struct cring *r)
{
	unsigned int st;

	while ((st = CRING_RD(SIO_FIFO_ST)) & FIFO_ST_VLD)
		(void)CRING_RD(SIO_FIFO_RD);
	if (st & FIFO_ST_ERR) {
		CRING_WR(SIO_FIFO_ST, st & FIFO_ST_ERR);
		r->cr_bellerr++;
	}
}

CRING_REQ_FN void
cring_init(struct cring *r)
{
	index_store(&r->cr_windex, 0);
	index_store(&r->cr_rindex, 0);
	r->cr_bellerr = 0;
}

CRING_FN unsigned int
cring_pending(const struct cring *r)
{
	return index_load(&r->cr_windex) - index_load(&r->cr_rindex);
}

CRING_FN int
cring_put(struct cring *r, const struct cring_entry *e)
{
	unsigned int w = r->cr_windex;

	if (w - index_load(&r->cr_rindex) >= CRING_SLOTS)
		return CRING_FULL;
	r->cr_entry[w & CRING_MASK] = *e;
	index_store(&r->cr_windex, w + 1);
	bell_ring();
	return 0;
}

CRING_FN unsigned int
cring_service(struct cring *r, cring_handler_t *fn, void *ctx)
{
	unsigned int rd, w, n;

	bell_clear(r);
	rd = r->cr_rindex;
	w = index_load(&r->cr_windex);
	for (n = 0; rd != w; n++) {
		fn(ctx, &r->cr_entry[rd & CRING_MASK]);
		index_store(&r->cr_rindex, ++rd);
	}
	return n;
}

CRING_REQ_FN void
cring_chan_init(struct cring_chan *c, struct cring *req, struct cring *rep)
{
	c->cc_req = req;
	c->cc_rep = rep;
	c->cc_inflight = 0;
}

CRING_REQ_FN int
cring_request(struct cring_chan *c, const struct cring_entry *e)
{
	if (c->cc_inflight >= CRING_SLOTS)
		return CRING_FULL;
	if (cring_put(c->cc_req, e) != 0)
		return CRING_FULL;
	c->cc_inflight++;
	return 0;
}

struct reply_ctx {
	struct cring_chan *chan;
	cring_handler_t	*fn;
	void		*ctx;
};

/*
 * The reply occupies its ring entry until cring_service() advances
 * cr_rindex, so its credit returns after fn: a request that fn sent on a
 * credit returned earlier could be answered into that still-occupied entry.
 */
static void
reply_one(void *ctx, const struct cring_entry *e)
{
	struct reply_ctx *rc = ctx;

	rc->fn(rc->ctx, e);
	rc->chan->cc_inflight--;
}

CRING_REQ_FN unsigned int
cring_replies(struct cring_chan *c, cring_handler_t *fn, void *ctx)
{
	struct reply_ctx rc = { c, fn, ctx };

	return cring_service(c->cc_rep, reply_one, &rc);
}
