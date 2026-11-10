/*
 * Host test of sys/arch/rp2040/rp2040/corering.c against a model of the SIO
 * inter-core FIFO registers (RP2040 datasheet 2.3.1.4). The source is
 * included unchanged; CRING_RD and CRING_WR route its register accesses to
 * the model, which keeps one eight-word FIFO per direction, the VLD and RDY
 * status bits and the sticky WOF and ROE errors, each seen from the core
 * that the running thread stands for.
 *
 *   corering_test det           fixed interleavings: the other core acts at
 *                               a chosen register access or inside a handler
 *   corering_test stress N      two threads exchange N numbered requests and
 *                               replies with random stalls; a core services
 *                               its ring only while its FIFO interrupt is
 *                               pending, so a stranded entry stops progress
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned int model_rd(unsigned long off);
static void model_wr(unsigned long off, unsigned int val);

#define	CRING_RD(off)		model_rd(off)
#define	CRING_WR(off, val)	model_wr((off), (val))

#include "corering.c"

#define	FIFO_DEPTH	8

static atomic_int fifo[2];		/* words waiting for core 0, core 1 */
static atomic_uint sticky[2];		/* WOF and ROE of each core */
static atomic_ulong notready[2];	/* status reads that showed RDY clear */
static _Thread_local int me;		/* core this thread stands for */

/* One-shot actions of the other core at a register access. */
static void (*hook_empty)(void);	/* a status read that shows VLD clear */
static void (*hook_pushed)(void);	/* after a write to FIFO_WR */

static void
fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "corering_test: FAIL: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

#define	CHECK(cond) \
	do { if (!(cond)) fail("%s:%d: %s", __func__, __LINE__, #cond); } \
	while (0)

static void
run_hook(void (**hook)(void))
{
	void (*fn)(void) = *hook;
	int saved = me;

	if (fn == NULL)
		return;
	*hook = NULL;
	me = !me;
	fn();
	me = saved;
}

static unsigned int
model_rd(unsigned long off)
{
	unsigned int v;

	switch (off) {
	case SIO_FIFO_ST:
		v = atomic_load(&sticky[me]);
		if (atomic_load(&fifo[me]) > 0)
			v |= FIFO_ST_VLD;
		if (atomic_load(&fifo[!me]) < FIFO_DEPTH)
			v |= FIFO_ST_RDY;
		else
			atomic_fetch_add(&notready[me], 1);
		if (!(v & FIFO_ST_VLD))
			run_hook(&hook_empty);
		return v;
	case SIO_FIFO_RD:
		if (atomic_load(&fifo[me]) == 0)
			atomic_fetch_or(&sticky[me], FIFO_ST_ROE);
		else
			atomic_fetch_sub(&fifo[me], 1);
		return 0;
	default:
		fail("read of SIO offset %#lx", off);
		return 0;
	}
}

static void
model_wr(unsigned long off, unsigned int val)
{
	switch (off) {
	case SIO_FIFO_WR:
		/* Only this core adds to the FIFO, so the level cannot rise. */
		if (atomic_load(&fifo[!me]) >= FIFO_DEPTH)
			atomic_fetch_or(&sticky[me], FIFO_ST_WOF);
		else
			atomic_fetch_add(&fifo[!me], 1);
		run_hook(&hook_pushed);
		break;
	case SIO_FIFO_ST:
		atomic_fetch_and(&sticky[me], ~(val & FIFO_ST_ERR));
		break;
	default:
		fail("write of SIO offset %#lx", off);
	}
}

static void
model_reset(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		atomic_store(&fifo[i], 0);
		atomic_store(&sticky[i], 0);
		atomic_store(&notready[i], 0);
	}
	hook_empty = NULL;
	hook_pushed = NULL;
	me = 0;
}

static struct cring_entry
entry_of(unsigned int seq)
{
	struct cring_entry e;

	e.ce_op = (unsigned short)(seq & 0xffff);
	e.ce_slot = (unsigned short)(seq >> 16);
	e.ce_arg = seq * 2654435761U;
	return e;
}

static int
entry_is(const struct cring_entry *e, unsigned int seq)
{
	struct cring_entry want = entry_of(seq);

	return e->ce_op == want.ce_op && e->ce_slot == want.ce_slot &&
	    e->ce_arg == want.ce_arg;
}

/*
 * The property the two ordering rules exist for, checked whenever neither
 * core is inside a call: unread entries imply a pending FIFO interrupt on
 * the consuming core, and no access ever raised a sticky error.
 */
static void
quiescent(const struct cring *r, int consumer)
{
	if (cring_pending(r) != 0 && atomic_load(&fifo[consumer]) == 0)
		fail("%u entries stranded with no doorbell pending",
		    cring_pending(r));
	if (atomic_load(&sticky[0]) != 0 || atomic_load(&sticky[1]) != 0)
		fail("sticky FIFO error %#x %#x", atomic_load(&sticky[0]),
		    atomic_load(&sticky[1]));
}

/* ---- deterministic tests: core 0 produces on ring a, core 1 consumes ---- */

static struct cring a, b;
static struct cring_chan chan;
static unsigned int det_base, next_put, next_got;
static void (*in_handler)(void);

static void
put_next(void)
{
	struct cring_entry e = entry_of(next_put);

	CHECK(cring_put(&a, &e) == 0);
	next_put++;
}

static void
expect_handler(void *ctx, const struct cring_entry *e)
{
	struct cring *r = ctx;
	void (*fn)(void) = in_handler;

	if (!entry_is(e, next_got))
		fail("entry %u arrived out of order or damaged", next_got);
	/* The entry in hand still counts as unread. */
	if (r->cr_rindex != det_base + next_got)
		fail("entry %u left the ring before its handler returned",
		    next_got);
	next_got++;
	if (fn != NULL && next_got == next_put) {
		in_handler = NULL;
		me = !me;
		fn();
		me = !me;
	}
}

static unsigned int
serve_a(void)
{
	unsigned int n;

	me = 1;
	n = cring_service(&a, expect_handler, &a);
	me = 0;
	return n;
}

static void
serve_a_hook(void)
{
	(void)cring_service(&a, expect_handler, &a);
}

static void
det_begin(unsigned int start)
{
	model_reset();
	cring_init(&a);
	/* Any start value exercises the free-running index arithmetic. */
	a.cr_windex = a.cr_rindex = det_base = start;
	next_put = next_got = 0;
	in_handler = NULL;
}

static void
test_order_and_wrap(void)
{
	unsigned int i, start[] = { 0, 0xfffffff0U, 0x7ffffff5U };
	size_t s;

	for (s = 0; s < sizeof(start) / sizeof(start[0]); s++) {
		det_begin(start[s]);
		for (i = 0; i < 200; i++) {
			put_next();
			if (i % 3 == 0)
				put_next();
			quiescent(&a, 1);
			CHECK(serve_a() >= 1);
			CHECK(next_got == next_put);
			quiescent(&a, 1);
			CHECK(atomic_load(&fifo[1]) == 0);
		}
	}
}

static void
test_full_ring_and_elision(void)
{
	struct cring_entry e = entry_of(999);
	unsigned int i;

	det_begin(0xffffffe8U);
	for (i = 0; i < CRING_SLOTS; i++)
		put_next();
	CHECK(cring_put(&a, &e) == CRING_FULL);
	CHECK(cring_pending(&a) == CRING_SLOTS);
	/* Eight doorbells fit; the rest are skipped, none overflows. */
	CHECK(atomic_load(&fifo[1]) == FIFO_DEPTH);
	CHECK(atomic_load(&notready[0]) == CRING_SLOTS - FIFO_DEPTH);
	quiescent(&a, 1);
	CHECK(serve_a() == CRING_SLOTS);
	CHECK(next_got == CRING_SLOTS);
	quiescent(&a, 1);
	put_next();
	CHECK(serve_a() == 1);
}

static void
test_spurious_service(void)
{
	det_begin(0);
	CHECK(serve_a() == 0);
	CHECK(a.cr_bellerr == 0);
	quiescent(&a, 1);
}

static void
test_sticky_errors(void)
{
	det_begin(0);
	atomic_store(&sticky[1], FIFO_ST_WOF);
	CHECK(serve_a() == 0);
	CHECK(a.cr_bellerr == 1);
	CHECK(atomic_load(&sticky[1]) == 0);
	atomic_store(&sticky[1], FIFO_ST_ROE);
	put_next();
	CHECK(serve_a() == 1);
	CHECK(a.cr_bellerr == 2);
	CHECK(atomic_load(&sticky[1]) == 0);
	CHECK(serve_a() == 0);
	CHECK(a.cr_bellerr == 2);
}

/* The producer publishes after the consumer's snapshot. */
static void
test_put_during_handler(void)
{
	det_begin(0);
	put_next();
	put_next();
	in_handler = put_next;
	CHECK(serve_a() == 2);
	CHECK(cring_pending(&a) == 1);
	quiescent(&a, 1);
	CHECK(serve_a() == 1);
	quiescent(&a, 1);
}

/* The producer publishes between the consumer's last pop and snapshot. */
static void
test_put_at_empty_fifo(void)
{
	det_begin(0);
	put_next();
	me = 1;
	hook_empty = put_next;
	CHECK(cring_service(&a, expect_handler, &a) == 2);
	me = 0;
	CHECK(hook_empty == NULL);
	quiescent(&a, 1);
	(void)serve_a();
	CHECK(next_got == next_put);
}

/* The consumer runs to completion at the producer's doorbell write. */
static void
test_service_at_push(void)
{
	det_begin(0);
	hook_pushed = serve_a_hook;
	put_next();
	CHECK(hook_pushed == NULL);
	CHECK(next_got == 1);
	quiescent(&a, 1);
}

/* A full FIFO of stale doorbells still covers a new entry. */
static void
test_put_into_full_fifo(void)
{
	det_begin(0);
	atomic_store(&fifo[1], FIFO_DEPTH);
	put_next();
	CHECK(atomic_load(&fifo[1]) == FIFO_DEPTH);
	quiescent(&a, 1);
	CHECK(serve_a() == 1);
	quiescent(&a, 1);
}

static void
test_init_empties(void)
{
	det_begin(0);
	put_next();
	put_next();
	CHECK(serve_a() == 2);
	put_next();
	a.cr_bellerr = 3;
	cring_init(&a);
	CHECK(cring_pending(&a) == 0);
	CHECK(a.cr_windex == 0 && a.cr_rindex == 0 && a.cr_bellerr == 0);
	model_reset();
	det_base = next_put = next_got = 0;
	put_next();
	CHECK(serve_a() == 1);
}

/* Server on core 1: one reply per request, which must always fit. */
static unsigned int served, replied;
static int resend;

static void
server(void *ctx, const struct cring_entry *e)
{
	(void)ctx;
	if (!entry_is(e, served))
		fail("request %u arrived out of order or damaged", served);
	if (cring_put(&b, e) != 0)
		fail("reply ring full at request %u", served);
	served++;
}

static void
serve_requests(void)
{
	int saved = me;

	me = 1;
	(void)cring_service(&a, server, NULL);
	me = saved;
}

static int
request_next(void)
{
	struct cring_entry e = entry_of(next_put);
	int error = cring_request(&chan, &e);

	if (error == 0)
		next_put++;
	return error;
}

static void
client(void *ctx, const struct cring_entry *e)
{
	(void)ctx;
	if (!entry_is(e, replied))
		fail("reply %u arrived out of order or damaged", replied);
	replied++;
	if (resend) {
		/* A handler that sends at once, served at once. */
		(void)request_next();
		serve_requests();
	}
}

static void
chan_begin(void)
{
	model_reset();
	cring_init(&a);
	cring_init(&b);
	cring_chan_init(&chan, &a, &b);
	next_put = served = replied = 0;
	resend = 0;
}

static void
test_credits(void)
{
	unsigned int i, round;

	chan_begin();
	CHECK(chan.cc_req == &a && chan.cc_rep == &b && chan.cc_inflight == 0);
	for (round = 0; round < 3; round++) {
		/*
		 * The server empties the request ring as it fills, so only
		 * the credit count stops the requester.
		 */
		for (i = 0; i < CRING_SLOTS; i++) {
			CHECK(request_next() == 0);
			serve_requests();
		}
		CHECK(cring_pending(&a) == 0);
		CHECK(cring_pending(&b) == CRING_SLOTS);
		CHECK(request_next() == CRING_FULL);
		CHECK(chan.cc_inflight == CRING_SLOTS);
		quiescent(&b, 0);
		CHECK(cring_replies(&chan, client, NULL) == CRING_SLOTS);
		CHECK(chan.cc_inflight == 0);
		CHECK(replied == next_put);
		quiescent(&b, 0);
	}
}

/*
 * Replies fill their ring and every reply handler sends again. A reply
 * keeps its entry until its handler returns, so the first handler's send
 * finds no credit; the later ones do, and no reply ever lacks an entry.
 */
static void
test_resend_from_reply_handler(void)
{
	unsigned int i;

	chan_begin();
	for (i = 0; i < CRING_SLOTS; i++) {
		CHECK(request_next() == 0);
		serve_requests();
	}
	resend = 1;
	CHECK(cring_replies(&chan, client, NULL) == CRING_SLOTS);
	CHECK(next_put == 2 * CRING_SLOTS - 1);
	CHECK(served == next_put);
	CHECK(chan.cc_inflight == CRING_SLOTS - 1);
	resend = 0;
	CHECK(cring_replies(&chan, client, NULL) == CRING_SLOTS - 1);
	CHECK(replied == next_put && chan.cc_inflight == 0);
	quiescent(&a, 1);
	quiescent(&b, 0);
}

static void
deterministic(void)
{
	test_order_and_wrap();
	test_full_ring_and_elision();
	test_spurious_service();
	test_sticky_errors();
	test_put_during_handler();
	test_put_at_empty_fifo();
	test_service_at_push();
	test_put_into_full_fifo();
	test_init_empties();
	test_credits();
	test_resend_from_reply_handler();
}

/* ---- threaded stress ---- */

static atomic_ulong progress;
static atomic_int finished;
static unsigned long total;

static unsigned int
xorshift(unsigned int *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return *s;
}

/* Rarely sleep long enough for the other core to fill a ring or the FIFO. */
static void
stall(unsigned int *seed)
{
	unsigned int r = xorshift(seed);
	struct timespec ts;

	if ((r & 0x3fff) != 0)
		return;
	ts.tv_sec = 0;
	ts.tv_nsec = (long)((r >> 14) % 300000);
	(void)nanosleep(&ts, NULL);
}

static void
stress_server(void *ctx, const struct cring_entry *e)
{
	stall(ctx);
	server(NULL, e);
}

static void *
core1_main(void *arg)
{
	unsigned int seed = 0x9e3779b9U;

	(void)arg;
	me = 1;
	while (!atomic_load(&finished)) {
		stall(&seed);
		if (atomic_load(&fifo[1]) > 0)
			(void)cring_service(&a, stress_server, &seed);
	}
	return NULL;
}

static void
stress_client(void *ctx, const struct cring_entry *e)
{
	stall(ctx);
	if (!entry_is(e, replied))
		fail("reply %u arrived out of order or damaged", replied);
	replied++;
	atomic_fetch_add(&progress, 1);
}

static void *
core0_main(void *arg)
{
	unsigned int seed = 0x2545f491U;

	(void)arg;
	me = 0;
	while (replied < total) {
		stall(&seed);
		if (atomic_load(&fifo[0]) > 0)
			(void)cring_replies(&chan, stress_client, &seed);
		if (next_put < total)
			(void)request_next();
	}
	return NULL;
}

static void
stress(unsigned long n)
{
	pthread_t t0, t1;
	unsigned long last = 0, now;
	int idle = 0;
	struct timespec ts = { 0, 100000000L };

	chan_begin();
	total = n;
	atomic_store(&progress, 0);
	atomic_store(&finished, 0);
	if (pthread_create(&t1, NULL, core1_main, NULL) != 0 ||
	    pthread_create(&t0, NULL, core0_main, NULL) != 0)
		fail("pthread_create");
	/* Cores wait on interrupts only, so a lost doorbell ends progress. */
	while ((now = atomic_load(&progress)) < n) {
		(void)nanosleep(&ts, NULL);
		idle = now == last ? idle + 1 : 0;
		if (idle == 100)
			fail("no reply for 10 s after %lu of %lu: "
			    "an entry is stranded", now, n);
		last = now;
	}
	(void)pthread_join(t0, NULL);
	atomic_store(&finished, 1);
	(void)pthread_join(t1, NULL);
	CHECK(served == n && replied == n && chan.cc_inflight == 0);
	CHECK(cring_pending(&a) == 0 && cring_pending(&b) == 0);
	CHECK(a.cr_bellerr == 0 && b.cr_bellerr == 0);
	CHECK(atomic_load(&sticky[0]) == 0 && atomic_load(&sticky[1]) == 0);
	if (atomic_load(&notready[0]) == 0)
		fail("no doorbell was skipped: the full-FIFO path did not run");
	printf("stress: %lu round trips, %lu skipped doorbells\n", n,
	    atomic_load(&notready[0]) + atomic_load(&notready[1]));
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "det") == 0) {
		deterministic();
		printf("det: ok\n");
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "stress") == 0) {
		char *end;
		unsigned long n = strtoul(argv[2], &end, 10);

		if (*end != '\0' || n == 0 || n > 0xfffffffeUL)
			fail("bad count '%s'", argv[2]);
		stress(n);
		return 0;
	}
	fprintf(stderr, "usage: corering_test det | stress COUNT\n");
	return 2;
}
