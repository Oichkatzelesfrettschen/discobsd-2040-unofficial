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
 * Message transport between the two RP2040 cores: one single-writer ring per
 * direction in shared SRAM and the SIO inter-core FIFO as a doorbell.
 * ARMv6-M has no load-exclusive or compare-and-swap, so every word has one
 * writing core: the producer alone stores cr_windex and the entries, the
 * consumer alone stores cr_rindex and cr_bellerr. Both indices run free and
 * are reduced modulo CRING_SLOTS at use, so cr_windex - cr_rindex is the
 * number of unread entries and all CRING_SLOTS entries are usable.
 *
 * The FIFO carries no payload. Its level-triggered interrupt (FIFO_ST.VLD,
 * datasheet 2.3.1.4) tells the receiving core that its inbound ring may hold
 * entries, and corering.c states the two ordering rules that keep an entry
 * from being stranded without a pending doorbell.
 *
 * corering.c includes no kernel header and reaches the SIO block only through
 * CRING_RD and CRING_WR, so tests/corering compiles it unchanged against a
 * model of the FIFO registers. docs/research/rp2040-second-core.md decides
 * what may run on core 1; the transport implies no launch of that core.
 */

#ifndef _RP2040_CORERING_H_
#define _RP2040_CORERING_H_

/*
 * Entries per ring. A power of two, so a free-running index wraps into it.
 * A unit that includes corering.c may define a smaller ring first.
 */
#ifndef CRING_SLOTS
#define	CRING_SLOTS	32U
#endif

/*
 * Linkage of the functions below: CRING_FN for the producer and consumer,
 * which both cores execute, and CRING_REQ_FN for ring initialization and
 * the requester side, which only the requesting core does. A unit that
 * includes corering.c defines them to place its private copy in sections
 * of its choosing: code that core 1 executes has to lie in SRAM, because a
 * flash erase or program takes execute-in-place down for both cores.
 */
#ifndef CRING_FN
#define	CRING_FN
#endif
#ifndef CRING_REQ_FN
#define	CRING_REQ_FN	CRING_FN
#endif

#define	CRING_FULL	(-1)	/* no free entry, or no credit */

/*
 * One request or reply. The ring carries a slot number instead of a pointer
 * to the message body, so a core that restarts never follows an address
 * left over from before its reset.
 */
struct cring_entry {
	unsigned short	ce_op;		/* operation or completion code */
	unsigned short	ce_slot;	/* static message slot of the body */
	unsigned int	ce_arg;
};

struct cring {
	unsigned int	cr_windex;	/* stored by the producer only */
	unsigned int	cr_rindex;	/* stored by the consumer only */
	unsigned int	cr_bellerr;	/* consumer: sticky FIFO errors seen */
	struct cring_entry cr_entry[CRING_SLOTS];
};

/*
 * The requesting core's view of a request ring and the ring its replies
 * return on. cc_inflight counts requests whose reply the requester has not
 * consumed. It never exceeds CRING_SLOTS, and the serving core puts exactly
 * one reply per request, so a reply always finds a free entry and the two
 * rings cannot fill against each other.
 */
struct cring_chan {
	struct cring	*cc_req;
	struct cring	*cc_rep;
	unsigned int	cc_inflight;
};

typedef void cring_handler_t(void *ctx, const struct cring_entry *e);

/*
 * cring_init() empties a ring. It stores both indices, so it runs only
 * while the other core cannot touch the ring: before that core is launched
 * or while PSM.FRCE_OFF holds it in reset.
 *
 * cring_put(), cring_request() and cring_replies() have one caller at a
 * time per ring or channel. A core whose interrupt handlers also call them
 * masks interrupts around each call.
 */
CRING_REQ_FN void	cring_init(struct cring *r);
CRING_FN unsigned int cring_pending(const struct cring *r);

/* Producer. Returns 0, or CRING_FULL with the ring unchanged. */
CRING_FN int	cring_put(struct cring *r, const struct cring_entry *e);

/*
 * Consumer, called for the inbound FIFO interrupt. Runs fn on every entry
 * published before the call's snapshot and returns their number. An entry
 * stays in the ring until fn returns.
 */
CRING_FN unsigned int cring_service(struct cring *r, cring_handler_t *fn, void *ctx);

/* Requester. cring_request() returns 0, or CRING_FULL with nothing sent. */
CRING_REQ_FN void	cring_chan_init(struct cring_chan *c, struct cring *req,
	    struct cring *rep);
CRING_REQ_FN int	cring_request(struct cring_chan *c, const struct cring_entry *e);
CRING_REQ_FN unsigned int cring_replies(struct cring_chan *c, cring_handler_t *fn,
	    void *ctx);

#endif /* !_RP2040_CORERING_H_ */
