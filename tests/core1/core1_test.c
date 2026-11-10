/*
 * Host test of the two decisions in sys/arch/rp2040/rp2040/core1.h: core 1's
 * reading of core 0's tick record, and core 0's supervision of core 1's
 * message loop. Each case drives one function with the values the registers
 * would hold and checks every action it returns.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "core1.h"

#define	SITE_ERASE	1U

static int failures;

#define	CHECK(cond) do {						\
	if (!(cond)) {							\
		printf("core1_test: %s:%d: %s\n", __func__, __LINE__,	\
		    #cond);						\
		failures++;						\
	}								\
} while (0)

/*
 * Core 1 samples every period from t0. tick() gives the tick record core 0
 * would hold at a time; site() gives the declared section. Returns the
 * actions seen, ORed, and stores the time of the first of each kind.
 */
struct run {
	unsigned int	probes, resets, records;
	unsigned int	first_probe, first_reset;
};

static struct run
sample(struct core1_obs *o, unsigned int t0, unsigned int until,
    unsigned int (*tick)(unsigned int), unsigned int (*site)(unsigned int))
{
	struct run r = { 0, 0, 0, 0, 0 };
	unsigned int t, act;

	for (t = t0; t - t0 < until; t += C1_PERIOD_US) {
		act = core1_observe(o, t, tick(t), site(t));
		if (act & C1_ACT_PROBE) {
			if (r.probes++ == 0)
				r.first_probe = t - t0;
		}
		if (act & C1_ACT_RESET) {
			if (r.resets++ == 0)
				r.first_reset = t - t0;
			if (act & C1_ACT_RECORD)
				r.records++;
			/* The chip resets; nothing samples after it. */
			break;
		}
		CHECK((act & C1_ACT_RECORD) == 0 || (act & C1_ACT_RESET));
	}
	return r;
}

static unsigned int base;		/* time the scenario starts */
static unsigned int freeze_at, thaw_at;	/* tick record stands still between */
static unsigned int site_from, site_to;	/* a section is declared between */

/* A 1 kHz tick record that stands still in [freeze_at, thaw_at). */
static unsigned int
tick_frozen(unsigned int t)
{
	unsigned int rel = t - base;

	if (rel >= freeze_at && rel < thaw_at)
		rel = freeze_at;
	return base + rel - rel % 1000U;
}

static unsigned int
site_window(unsigned int t)
{
	unsigned int rel = t - base;

	return rel >= site_from && rel < site_to ? SITE_ERASE : C1_SITE_NONE;
}

static void
scenario(unsigned int start, unsigned int f, unsigned int th,
    unsigned int sf, unsigned int st)
{
	base = start;
	freeze_at = f;
	thaw_at = th;
	site_from = sf;
	site_to = st;
}

static void
test_ticking_clock_is_quiet(void)
{
	struct core1_obs o = { 0 };
	struct run r;

	scenario(5000, ~0U, ~0U, ~0U, ~0U);
	r = sample(&o, base, 20000000U, tick_frozen, site_window);
	CHECK(r.probes == 0 && r.resets == 0);
	CHECK(o.co_stalls == 0 && o.co_gap_us == 0);
}

static void
test_undeclared_stall_probes_once_then_resets(void)
{
	struct core1_obs o = { 0 };
	struct run r;

	scenario(0, 50000U, ~0U, ~0U, ~0U);
	r = sample(&o, base, 20000000U, tick_frozen, site_window);
	/* One probe for the stall, one for the hang, then the reset. */
	CHECK(r.probes == 2);
	CHECK(r.first_probe == 50000U + C1_STALL_US);
	CHECK(r.resets == 1 && r.records == 1);
	CHECK(r.first_reset == 50000U + C1_HANG_US + C1_PERIOD_US);
	CHECK(o.co_stalls == 1);
	CHECK(o.co_gap_us >= C1_STALL_US && o.co_gap_us < C1_HANG_US);
}

static void
test_stall_threshold_is_inclusive(void)
{
	struct core1_obs o = { 0 };

	/* Two samples of a still record, the second exactly C1_STALL_US old. */
	CHECK(core1_observe(&o, 1000U, 1000U, C1_SITE_NONE) == 0);
	CHECK(core1_observe(&o, 1000U + C1_STALL_US - 1, 1000U,
	    C1_SITE_NONE) == 0);
	CHECK(o.co_stalls == 0);
	CHECK(core1_observe(&o, 1000U + C1_STALL_US, 1000U,
	    C1_SITE_NONE) == C1_ACT_PROBE);
	CHECK(o.co_stalls == 1);
}

static void
test_each_stall_is_probed_again(void)
{
	struct core1_obs o = { 0 };
	struct run r;

	scenario(0, 100000U, 400000U, ~0U, ~0U);
	r = sample(&o, base, 1000000U, tick_frozen, site_window);
	CHECK(r.probes == 1 && r.resets == 0 && o.co_stalls == 1);
	CHECK(o.co_gap_us >= 280000U && o.co_gap_us < 300000U);

	scenario(1000000U, 100000U, 250000U, ~0U, ~0U);
	r = sample(&o, base, 1000000U, tick_frozen, site_window);
	CHECK(r.probes == 1 && r.resets == 0 && o.co_stalls == 2);
	/* The longest stall stays on record. */
	CHECK(o.co_gap_us >= 280000U && o.co_gap_us < 300000U);
}

static void
test_declared_erase_is_no_stall(void)
{
	struct core1_obs o = { 0 };
	struct run r;

	/* A 400 ms erase: the section covers the whole still stretch. */
	scenario(0, 100000U, 500000U, 100000U, 500000U);
	r = sample(&o, base, 2000000U, tick_frozen, site_window);
	CHECK(r.probes == 0 && r.resets == 0);
	CHECK(o.co_stalls == 0 && o.co_gap_us == 0);
}

static void
test_record_and_section_read_apart(void)
{
	struct core1_obs o = { 0 };

	/*
	 * Core 1 reads the section, then the record. An erase that ends
	 * between samples can leave one sample holding the record from
	 * inside the erase beside a section that reads clear.
	 */
	CHECK(core1_observe(&o, 10000U, 10000U, C1_SITE_NONE) == 0);
	CHECK(core1_observe(&o, 20000U, 10000U, SITE_ERASE) == 0);
	CHECK(core1_observe(&o, 210000U, 10000U, SITE_ERASE) == 0);
	CHECK(core1_observe(&o, 220000U, 10000U, C1_SITE_NONE) == 0);
	/* The next sample sees the record the catch-up advanced. */
	CHECK(core1_observe(&o, 230000U, 229000U, C1_SITE_NONE) == 0);
	CHECK(o.co_stalls == 0);
}

static void
test_declared_section_that_never_returns(void)
{
	struct core1_obs o = { 0 };
	struct run r;

	scenario(0, 100000U, ~0U, 100000U, ~0U);
	r = sample(&o, base, 20000000U, tick_frozen, site_window);
	/* The section names itself, so the reset carries no record. */
	CHECK(r.resets == 1 && r.records == 0);
	CHECK(r.first_reset == 100000U + C1_HANG_US + C1_PERIOD_US);
	CHECK(o.co_stalls == 0);
}

static void
test_timer_wrap(void)
{
	struct core1_obs o = { 0 };
	struct run r;

	/* The microsecond timer wraps 50 ms into the stall. */
	scenario(0U - 150000U, 100000U, ~0U, ~0U, ~0U);
	r = sample(&o, base, 20000000U, tick_frozen, site_window);
	CHECK(r.probes == 2 && r.resets == 1 && r.records == 1);
	CHECK(r.first_probe == 100000U + C1_STALL_US);

	/* And a ticking clock across the wrap stays quiet. */
	o = (struct core1_obs){ 0 };
	scenario(0U - 150000U, ~0U, ~0U, ~0U, ~0U);
	r = sample(&o, base, 2000000U, tick_frozen, site_window);
	CHECK(r.probes == 0 && r.resets == 0);
}

/* Core 0's side. core 1 answers ping n when answer is set. */
static unsigned int
period(struct core1_sup *s, int answer)
{
	unsigned int act = core1_sup_tick(s);

	if (act == C1_SUP_PING && answer)
		core1_sup_reply(s, C1_OP_PONG, s->cs_seq & 0xffffU,
		    s->cs_epoch);
	return act;
}

static void
test_supervisor_healthy(void)
{
	struct core1_sup s = { 0 };
	unsigned int i;

	CHECK(core1_sup_tick(&s) == 0);		/* never started */
	core1_sup_start(&s);
	CHECK(core1_sup_tick(&s) == C1_SUP_LAUNCH);
	CHECK(core1_sup_launched(&s, 1) == 0);
	CHECK(s.cs_state == C1_RUN && s.cs_epoch == 1);
	for (i = 1; i <= 100000; i++) {
		CHECK(period(&s, 1) == C1_SUP_PING);
		CHECK(s.cs_seq == i && s.cs_pong == i);
	}
	CHECK(s.cs_restarts == 0 && s.cs_missed == 0);
}

static void
test_supervisor_restarts_with_backoff(void)
{
	struct core1_sup s = { 0 };
	unsigned int attempt, wait, i;

	core1_sup_start(&s);
	CHECK(core1_sup_tick(&s) == C1_SUP_LAUNCH);
	CHECK(core1_sup_launched(&s, 1) == 0);
	for (attempt = 0; attempt < C1_RESTART_MAX; attempt++) {
		/* The first ping of a launch has nothing to miss yet. */
		CHECK(period(&s, 0) == C1_SUP_PING);
		for (i = 1; i < C1_MISS_MAX; i++)
			CHECK(period(&s, 0) == C1_SUP_PING);
		CHECK(period(&s, 0) == C1_SUP_PARK);
		CHECK(s.cs_state == C1_WAIT && s.cs_restarts == attempt + 1);
		for (wait = 1; wait < (1U << attempt); wait++)
			CHECK(core1_sup_tick(&s) == 0);
		CHECK(core1_sup_tick(&s) == C1_SUP_LAUNCH);
		CHECK(core1_sup_launched(&s, 1) == 0);
		CHECK(s.cs_epoch == attempt + 2);
	}
	CHECK(period(&s, 0) == C1_SUP_PING);
	for (i = 1; i < C1_MISS_MAX; i++)
		CHECK(period(&s, 0) == C1_SUP_PING);
	CHECK(period(&s, 0) == C1_SUP_PARK);
	CHECK(s.cs_state == C1_DEAD);
	for (i = 0; i < 10; i++)
		CHECK(core1_sup_tick(&s) == 0);

	/* A request to start gives a fresh budget. */
	core1_sup_start(&s);
	CHECK(s.cs_restarts == 0);
	CHECK(core1_sup_tick(&s) == C1_SUP_LAUNCH);
}

static void
test_supervisor_one_miss_recovers(void)
{
	struct core1_sup s = { 0 };

	core1_sup_start(&s);
	(void)core1_sup_tick(&s);
	(void)core1_sup_launched(&s, 1);
	CHECK(period(&s, 1) == C1_SUP_PING);
	CHECK(period(&s, 0) == C1_SUP_PING);	/* ping 2 goes unanswered */
	CHECK(period(&s, 1) == C1_SUP_PING);	/* one miss, ping 3 answered */
	CHECK(s.cs_missed == 1);
	CHECK(period(&s, 0) == C1_SUP_PING);	/* answered: the count clears */
	CHECK(s.cs_missed == 0);
	CHECK(period(&s, 0) == C1_SUP_PING);
	CHECK(s.cs_missed == 1 && s.cs_state == C1_RUN);
}

static void
test_supervisor_rejects_stale_replies(void)
{
	struct core1_sup s = { 0 };

	core1_sup_start(&s);
	(void)core1_sup_tick(&s);
	(void)core1_sup_launched(&s, 1);
	(void)core1_sup_tick(&s);
	(void)core1_sup_tick(&s);		/* ping 2 of epoch 1 is out */
	core1_sup_reply(&s, C1_OP_PONG, 1, 1);	/* an older ping */
	CHECK(s.cs_pong != s.cs_seq);
	core1_sup_reply(&s, C1_OP_PONG, 2, 0);	/* an older launch */
	CHECK(s.cs_pong != s.cs_seq);
	core1_sup_reply(&s, C1_OP_BAD, 2, 1);	/* not an answer */
	CHECK(s.cs_pong != s.cs_seq);
	core1_sup_reply(&s, C1_OP_PONG, 2, 1);
	CHECK(s.cs_pong == s.cs_seq);
}

static void
test_supervisor_failed_launch(void)
{
	struct core1_sup s = { 0 };
	unsigned int n, launches = 0;

	core1_sup_start(&s);
	for (n = 0; n < 100; n++)
		if (core1_sup_tick(&s) == C1_SUP_LAUNCH) {
			launches++;
			CHECK(core1_sup_launched(&s, 0) == C1_SUP_PARK);
		}
	CHECK(launches == C1_RESTART_MAX + 1);
	CHECK(s.cs_state == C1_DEAD && s.cs_epoch == 0);
}

int
main(void)
{
	test_ticking_clock_is_quiet();
	test_undeclared_stall_probes_once_then_resets();
	test_stall_threshold_is_inclusive();
	test_each_stall_is_probed_again();
	test_declared_erase_is_no_stall();
	test_record_and_section_read_apart();
	test_declared_section_that_never_returns();
	test_timer_wrap();
	test_supervisor_healthy();
	test_supervisor_restarts_with_backoff();
	test_supervisor_one_miss_recovers();
	test_supervisor_rejects_stale_replies();
	test_supervisor_failed_launch();
	if (failures != 0) {
		printf("core1_test: %d checks failed\n", failures);
		return 1;
	}
	return 0;
}
