#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char *caddr_t;

/* The production tick rate; the SysTick period follows from it. */
#include <machine/machparam.h>

#include <rp2040/rp2040/clock_catchup.h>

#define TICK_US (1000000U / (unsigned int)HZ)

_Static_assert(1000000U % HZ == 0, "HZ divides one second");

static void
require(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "clock_catchup: FAIL: %s\n", message);
		exit(EXIT_FAILURE);
	}
}

/*
 * A model of one core: a SysTick that raises an exception every TICK_US of
 * the microsecond timer and holds at most one pending, the lbolt and time
 * counters, and an empty callout queue. Interrupts masked means the
 * exception stays pending instead of running the handler.
 */
struct sim {
	uint32_t now_us;		/* TIMERAWL, wraps modulo 2^32. */
	uint32_t next_boundary_us;	/* Next SysTick expiry. */
	int pending;			/* ICSR.PENDSTSET. */
	int lbolt;
	long seconds;
	struct callout head;
	struct clock_catchup cc;
	unsigned int handler_latency_us;
	unsigned int boundaries;	/* Ticks an unmasked SysTick raises. */
	unsigned int handled;		/* Hardware ticks the handler ran. */
};

static void
sim_init(struct sim *s, uint32_t start_us, uint32_t phase_us)
{
	memset(s, 0, sizeof(*s));
	s->now_us = start_us;
	s->next_boundary_us = start_us + TICK_US - phase_us;
	s->cc.stamp_us = start_us - phase_us;
	s->cc.armed = 1;
	s->lbolt = 0;
}

/* The tick sequence of hardclock that touches time and the callout queue. */
static void
model_hardclock(int *lbolt, long *seconds, struct callout *head)
{
	struct callout *p1 = head->c_next;

	while (p1) {
		if (--p1->c_time > 0)
			break;
		if (p1->c_time == 0)
			break;
		p1 = p1->c_next;
	}
	if (++*lbolt >= HZ) {
		*lbolt -= HZ;
		++*seconds;
	}
}

static void
sim_advance(struct sim *s, uint32_t usec, int masked)
{
	uint32_t end = s->now_us + usec;

	/* Signed differences keep the comparison valid across the wrap. */
	while ((int32_t)(s->next_boundary_us - end) <= 0) {
		if ((int32_t)(s->next_boundary_us - s->now_us) > 0)
			s->now_us = s->next_boundary_us;
		s->next_boundary_us += TICK_US;
		s->boundaries++;
		if (masked) {
			s->pending = 1;
			continue;
		}
		s->now_us += s->handler_latency_us;
		model_hardclock(&s->lbolt, &s->seconds, &s->head);
		clock_catchup_tick(&s->cc, s->now_us);
		s->handled++;
		if ((int32_t)(s->now_us - end) > 0)
			end = s->now_us;
	}
	if ((int32_t)(end - s->now_us) > 0)
		s->now_us = end;
}

/* PRIMASK clears: the held tick runs its handler. */
static void
sim_unmask(struct sim *s)
{
	if (s->pending) {
		s->pending = 0;
		s->now_us += s->handler_latency_us;
		model_hardclock(&s->lbolt, &s->seconds, &s->head);
		clock_catchup_tick(&s->cc, s->now_us);
		s->handled++;
	}
}

/* clock_catchup() as clock.c wires it, over the model. */
static void
sim_catchup(struct sim *s)
{
	unsigned int owed = clock_catchup_owed(&s->cc, s->now_us, s->pending,
	    TICK_US);

	if (owed != 0)
		clock_catchup_apply(owed, HZ, &s->lbolt, &s->seconds,
		    &s->head);
}

static long
sim_ticks(const struct sim *s)
{
	return s->seconds * HZ + s->lbolt;
}

/* A masked window that closes the way flash_erase does. */
static void
sim_masked_op(struct sim *s, uint32_t usec)
{
	sim_advance(s, usec, 1);
	sim_catchup(s);
	sim_unmask(s);
}

static void
test_erase_45ms_every_phase(void)
{
	uint32_t phase;

	for (phase = 0; phase < TICK_US; phase++) {
		struct sim s;
		long before;

		sim_init(&s, 1000000U, phase);
		before = sim_ticks(&s);
		sim_masked_op(&s, 45000U);
		require(sim_ticks(&s) - before == 45L * HZ / 1000L,
		    "a 45 ms masked erase advances 45 ms of ticks");
		require(sim_ticks(&s) - before == (long)s.boundaries,
		    "accounted ticks equal the ticks SysTick raised");
	}
}

static void
test_second_rollover(void)
{
	struct sim s;

	sim_init(&s, 5000U, 0);
	s.lbolt = HZ - 10;
	sim_masked_op(&s, 45000U);
	require(s.seconds == 1 && s.lbolt == 35,
	    "catch-up carries lbolt into the time of day");

	sim_init(&s, 5000U, 0);
	sim_masked_op(&s, 10U * 1000000U + 3U * TICK_US);
	require(s.seconds == 10 && s.lbolt == 3,
	    "a ten-second gap advances ten seconds");
}

static void
test_no_double_count(void)
{
	struct sim s;
	long before;
	int i;

	/* A second catch-up at the same instant owes nothing. */
	sim_init(&s, 0, 400);
	sim_advance(&s, 45000U, 1);
	sim_catchup(&s);
	before = sim_ticks(&s);
	sim_catchup(&s);
	sim_catchup(&s);
	require(sim_ticks(&s) == before, "repeated catch-up counts once");
	sim_unmask(&s);
	require(sim_ticks(&s) == (long)s.boundaries,
	    "pending tick plus catch-up equals the boundaries");

	/* Erases and programs nested in one masked window. */
	sim_init(&s, 123456U, 250);
	for (i = 0; i < 5; i++) {
		sim_advance(&s, 45000U, 1);
		sim_catchup(&s);
		sim_advance(&s, 700U, 1);
		sim_catchup(&s);
	}
	sim_catchup(&s);
	sim_unmask(&s);
	require(sim_ticks(&s) == (long)s.boundaries,
	    "nested masked operations count every tick once");

	/* Unmasked time between windows is the handler's, not catch-up. */
	sim_init(&s, 0, 0);
	for (i = 0; i < 20; i++) {
		sim_advance(&s, 5000U, 0);
		sim_masked_op(&s, 45000U + (uint32_t)i * 13U);
	}
	require(sim_ticks(&s) == (long)s.boundaries,
	    "alternating masked and live time counts every tick once");
}

static void
test_no_spurious_ticks(void)
{
	struct sim s;

	sim_init(&s, 0, 100);
	sim_masked_op(&s, 300U);
	require(sim_ticks(&s) == 0 && s.boundaries == 0,
	    "a window shorter than the remaining period adds no tick");

	sim_init(&s, 0, 0);
	s.cc.armed = 0;
	sim_advance(&s, 45000U, 1);
	sim_catchup(&s);
	require(sim_ticks(&s) == 0, "catch-up waits for clkstart");
}

static void
test_timer_wrap(void)
{
	uint32_t starts[] = { 0xffffffffU - 45000U, 0xffffffffU - 22500U,
	    0xffffffffU - 999U, 0xffffffffU, 0xfffffc00U };
	size_t i;

	for (i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
		uint32_t phase;

		for (phase = 0; phase < TICK_US; phase += 97) {
			struct sim s;

			sim_init(&s, starts[i], phase);
			sim_masked_op(&s, 45000U);
			require(sim_ticks(&s) == 45,
			    "a window across the TIMERAWL wrap owes 45 ticks");
			require(sim_ticks(&s) == (long)s.boundaries,
			    "wrap leaves catch-up equal to raised ticks");
		}
	}
}

static void
test_late_handler(void)
{
	uint32_t latency;

	for (latency = 0; latency <= 600; latency += 50) {
		struct sim s;
		int i;

		sim_init(&s, 77777U, 0);
		s.handler_latency_us = latency;
		for (i = 0; i < 30; i++) {
			sim_advance(&s, 3000U, 0);
			sim_masked_op(&s, 45000U);
		}
		require(sim_ticks(&s) <= (long)s.boundaries,
		    "a late handler never raises the count above SysTick");
		require(sim_ticks(&s) + 30 >= (long)s.boundaries,
		    "a late handler loses at most one tick per window");
	}
}

static unsigned int rng = 12345U;

static unsigned int
rnd(unsigned int n)
{
	rng = rng * 1103515245U + 12345U;
	return (rng >> 8) % n;
}

static void
test_apply_matches_hardclock(void)
{
	int trial;

	for (trial = 0; trial < 20000; trial++) {
		struct callout pool_a[8], pool_b[8];
		struct callout head_a, head_b;
		int count = (int)rnd(7);
		unsigned int owed = rnd(60);
		int lb_a = (int)rnd(HZ), lb_b;
		long sec_a = 100, sec_b = 100;
		unsigned int t;
		int i;

		memset(&head_a, 0, sizeof(head_a));
		memset(&head_b, 0, sizeof(head_b));
		for (i = count - 1; i >= 0; i--) {
			int v = (int)rnd(25) - (rnd(3) == 0 ? 8 : 0);

			memset(&pool_a[i], 0, sizeof(pool_a[i]));
			memset(&pool_b[i], 0, sizeof(pool_b[i]));
			pool_a[i].c_time = pool_b[i].c_time = v;
			pool_a[i].c_next = i == count - 1 ? 0 : &pool_a[i + 1];
			pool_b[i].c_next = i == count - 1 ? 0 : &pool_b[i + 1];
		}
		head_a.c_next = count ? &pool_a[0] : 0;
		head_b.c_next = count ? &pool_b[0] : 0;
		lb_b = lb_a;

		for (t = 0; t < owed; t++)
			model_hardclock(&lb_a, &sec_a, &head_a);
		clock_catchup_apply(owed, HZ, &lb_b, &sec_b, &head_b);

		require(lb_a == lb_b && sec_a == sec_b,
		    "apply matches hardclock on lbolt and seconds");
		for (i = 0; i < count; i++)
			require(pool_a[i].c_time == pool_b[i].c_time,
			    "apply matches hardclock on every callout time");
	}
}

static int callout_ran;

static void
callout_fn(caddr_t arg)
{
	(void)arg;
	callout_ran++;
}

static void
test_callouts_defer(void)
{
	struct sim s;
	struct callout c[3];

	sim_init(&s, 0, 0);
	memset(c, 0, sizeof(c));
	c[0].c_time = 10;
	c[0].c_func = callout_fn;
	c[0].c_next = &c[1];
	c[1].c_time = 20;
	c[1].c_func = callout_fn;
	c[1].c_next = &c[2];
	c[2].c_time = 4000;
	c[2].c_func = callout_fn;
	s.head.c_next = &c[0];

	sim_masked_op(&s, 45000U);
	require(callout_ran == 0,
	    "catch-up leaves every due callout to softclock");
	require(s.head.c_next == &c[0] && c[0].c_next == &c[1] &&
	    c[1].c_next == &c[2], "catch-up keeps due callouts queued");
	require(c[0].c_time <= 0 && c[1].c_time < 0,
	    "due callouts read as due at the next hardclock");
	require(c[2].c_time == 4000 - (45 - 30),
	    "a callout not yet due keeps the remainder");
}

int
main(void)
{
	test_erase_45ms_every_phase();
	test_second_rollover();
	test_no_double_count();
	test_no_spurious_ticks();
	test_timer_wrap();
	test_late_handler();
	test_apply_matches_hardclock();
	test_callouts_defer();
	puts("clock_catchup: PASS (45 ms erase advances 45 ticks; no double count; wrap safe)");
	return 0;
}
