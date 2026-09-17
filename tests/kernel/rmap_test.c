/*
 * Host gate for sys/kern/subr_rmap.c, the resource map allocator the swap
 * code allocates process images from.
 *
 * The gate links the kernel source itself, so a change to the allocator is
 * measured against these invariants rather than against a second copy of the
 * algorithm. The Makefile builds this file three times: once in the shape
 * sys/arch/rp2040/compile/PICO configures (COMPACT_SWAPMAP, sixteen-bit
 * descriptors), once with the wide descriptors the stm32 and pic32 ports
 * use, and once with DIAGNOSTIC, which is what compiles in the three overlap
 * panics mfree() raises when a caller returns space it does not hold.
 *
 * A map is an ascending array of free runs terminated by a zero-size entry,
 * m_limit addresses the last usable slot, and address zero is the
 * terminator's, so no allocation may return it.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>

#define SLOTS		8

/*
 * The erase unit the cursor allocator rotates across, in DEV_BSIZE blocks.
 * The gate carries its own value so it builds for a port that allocates swap
 * without an alignment, and pins it to the port's own when there is one.
 */
#define ALIGN		4
#ifdef SWAP_IMAGE_ALIGN
_Static_assert(ALIGN == SWAP_IMAGE_ALIGN,
    "the gate's erase unit must match the port's swap image alignment");
#endif

static struct mapent ent[SLOTS + 1];
static struct map fixture[1];
static char fixture_name[] = "testmap";

/*
 * Reset to an empty map whose last usable slot is slots - 1, so a gate can
 * choose how much descriptor headroom the allocator gets.
 */
static void
reset(unsigned slots)
{
	unsigned i;

	for (i = 0; i < SLOTS + 1; i++) {
		ent[i].m_size = 0;
		ent[i].m_addr = 0;
	}
	fixture->m_map = ent;
	fixture->m_limit = &ent[slots - 1];
	fixture->m_name = fixture_name;
	hk_reset_output();
}

/* Number of free runs the map describes. */
static unsigned
runs(void)
{
	struct mapent *bp;
	unsigned n = 0;

	for (bp = fixture->m_map; bp->m_size; bp++)
		n++;
	return n;
}

static unsigned long
run_addr(unsigned i)
{
	return (unsigned long)fixture->m_map[i].m_addr;
}

static unsigned long
run_size(unsigned i)
{
	return (unsigned long)fixture->m_map[i].m_size;
}

/* Total free space, which every scenario conserves unless it says otherwise. */
static unsigned long
free_total(void)
{
	struct mapent *bp;
	unsigned long total = 0;

	for (bp = fixture->m_map; bp->m_size; bp++)
		total += (unsigned long)bp->m_size;
	return total;
}

/*
 * malloc() takes the first run large enough. An exact fit removes the run and
 * shifts the rest down; a partial fit moves the run's base up.
 */
static void
first_fit(void)
{
	size_t got;

	reset(SLOTS);
	HK_CHECK(malloc(fixture, 1) == 0);
	HK_CHECK(runs() == 0);

	/* Two runs: [16,24) and [64,128). Only the second holds 40 units. */
	mfree(fixture, 8, 16);
	mfree(fixture, 64, 64);
	HK_CHECK(runs() == 2);

	got = malloc(fixture, 40);
	HK_CHECK(got == 64);
	HK_CHECK(runs() == 2);
	HK_CHECK(run_addr(1) == 104);
	HK_CHECK(run_size(1) == 24);

	/* An exact fit on the first run collapses it out of the array. */
	got = malloc(fixture, 8);
	HK_CHECK(got == 16);
	HK_CHECK(runs() == 1);
	HK_CHECK(run_addr(0) == 104);

	/* Nothing left is large enough. */
	HK_CHECK(malloc(fixture, 25) == 0);
	HK_CHECK(run_size(0) == 24);
}

/*
 * Scratch the panic callbacks below share. Each one runs inside the harness
 * frame that catches the panic, so it takes no argument and returns nothing.
 */
static size_t scratch_a[3];
static size_t scratch_cursor;

static void
call_malloc_zero(void)
{
	(void)malloc(fixture, 0);
}

static void
call_mfree_at_zero(void)
{
	mfree(fixture, 4, 0);
}

/*
 * The two defensive checks that bracket the allocator's protocol: a zero
 * request has no answer to return, and address zero terminates the array, so
 * it can never be handed back to it.
 */
static void
protocol_panics(void)
{
	reset(SLOTS);
	HK_EXPECT_PANIC("malloc: size = 0", call_malloc_zero);

	mfree(fixture, 16, 16);
	HK_EXPECT_PANIC("mfree: addr = 0", call_mfree_at_zero);

	/* A zero-size free is a no-op rather than a panic. */
	mfree(fixture, 0, 64);
	HK_CHECK(runs() == 1);
	HK_CHECK(free_total() == 16);
}

/*
 * mfree() sorts the returned run into the array and merges it with whichever
 * neighbours it touches. Merging on both sides is the case that shortens the
 * array, and it is the one that keeps descriptor use proportional to live
 * allocations rather than to allocation history.
 */
static void
coalescing(void)
{
	/* Left neighbour only. */
	reset(SLOTS);
	mfree(fixture, 16, 16);
	mfree(fixture, 16, 32);
	HK_CHECK(runs() == 1);
	HK_CHECK(run_addr(0) == 16);
	HK_CHECK(run_size(0) == 32);

	/* Right neighbour only. */
	reset(SLOTS);
	mfree(fixture, 16, 32);
	mfree(fixture, 16, 16);
	HK_CHECK(runs() == 1);
	HK_CHECK(run_addr(0) == 16);
	HK_CHECK(run_size(0) == 32);

	/* Both neighbours: two runs and the gap become one run. */
	reset(SLOTS);
	mfree(fixture, 16, 16);
	mfree(fixture, 16, 48);
	HK_CHECK(runs() == 2);
	mfree(fixture, 16, 32);
	HK_CHECK(runs() == 1);
	HK_CHECK(run_addr(0) == 16);
	HK_CHECK(run_size(0) == 48);

	/* Neither: a third run sorts into address order. */
	reset(SLOTS);
	mfree(fixture, 8, 64);
	mfree(fixture, 8, 16);
	mfree(fixture, 8, 40);
	HK_CHECK(runs() == 3);
	HK_CHECK(run_addr(0) == 16);
	HK_CHECK(run_addr(1) == 40);
	HK_CHECK(run_addr(2) == 64);
	HK_CHECK(free_total() == 24);
}

/*
 * A map with no spare descriptor cannot record another disjoint run. The
 * allocator says so on the console and drops the space rather than
 * overwriting the slot past m_limit.
 */
static void
overflow_is_reported(void)
{
	unsigned i;

	/* Three usable slots, one of which the terminator takes. */
	reset(3);
	mfree(fixture, 8, 16);
	mfree(fixture, 8, 48);
	HK_CHECK(runs() == 2);
	HK_CHECK(hk_printf_calls == 0);

	/* A third disjoint run has nowhere to go. */
	mfree(fixture, 8, 80);
	HK_CHECK(hk_printf_calls == 1);
	HK_CHECK(hk_contains(hk_printf_text, "testmap: overflow"));
	HK_CHECK(runs() == 2);
	HK_CHECK(free_total() == 16);

	/* The terminator past m_limit is intact, so the array still ends. */
	HK_CHECK(ent[2].m_size == 0);
	for (i = 0; i < SLOTS + 1; i++)
		if (i > 2)
			HK_CHECK(ent[i].m_size == 0);

	/* Space adjacent to a recorded run still merges, needing no slot. */
	hk_reset_output();
	mfree(fixture, 8, 24);
	HK_CHECK(hk_printf_calls == 0);
	HK_CHECK(runs() == 2);
	HK_CHECK(run_size(0) == 16);
}

#ifdef DIAGNOSTIC
static void
call_mfree_into_left(void)
{
	mfree(fixture, 4, 24);
}

static void
call_mfree_across_gap(void)
{
	mfree(fixture, 20, 32);
}

static void
call_mfree_into_right(void)
{
	mfree(fixture, 40, 16);
}

/*
 * Returning space the map already describes is an internal error, not a
 * recoverable condition: the three panics separate overlap with the run on
 * the left, with both, and with the run on the right.
 */
static void
overlap_panics(void)
{
	reset(SLOTS);
	mfree(fixture, 16, 16);
	HK_EXPECT_PANIC("mfree overlap #1", call_mfree_into_left);

	reset(SLOTS);
	mfree(fixture, 16, 16);
	mfree(fixture, 16, 48);
	HK_EXPECT_PANIC("mfree overlap #2", call_mfree_across_gap);

	reset(SLOTS);
	mfree(fixture, 16, 48);
	HK_EXPECT_PANIC("mfree overlap #3", call_mfree_into_right);
}
#endif

/*
 * malloc3() places data, stack and u area independently and returns the u
 * area's address. A request it cannot satisfy in full restores every run it
 * had already taken from, so a failed exec leaves the map as it found it.
 */
static void
three_segments(void)
{
	size_t a[3];
	unsigned long before;

	reset(SLOTS);
	mfree(fixture, 64, 64);
	HK_CHECK(malloc3(fixture, 16, 8, 4, a) == a[2]);
	HK_CHECK(a[0] == 64);
	HK_CHECK(a[1] == 80);
	HK_CHECK(a[2] == 88);
	HK_CHECK(free_total() == 36);

	/* Zero-size data and stack are how init() comes in. */
	reset(SLOTS);
	mfree(fixture, 16, 16);
	HK_CHECK(malloc3(fixture, 0, 0, 8, a) != 0);
	HK_CHECK(free_total() == 8);

	/* A request larger than any run leaves the map unchanged. */
	reset(SLOTS);
	mfree(fixture, 16, 16);
	mfree(fixture, 16, 48);
	before = free_total();
	HK_CHECK(malloc3(fixture, 40, 4, 4, a) == 0);
	HK_CHECK(free_total() == before);
	HK_CHECK(runs() == 2);
	HK_CHECK(run_addr(0) == 16);
	HK_CHECK(run_size(0) == 16);
	HK_CHECK(run_addr(1) == 48);
	HK_CHECK(run_size(1) == 16);
}

static void
call_contiguous_align_six(void)
{
	(void)malloc3_contiguous(fixture, 4, 0, 0, 6, scratch_a);
}

static void
call_contiguous_align_zero(void)
{
	(void)malloc3_contiguous(fixture, 4, 0, 0, 0, scratch_a);
}

/*
 * malloc3_contiguous() places the three segments in one run rounded up to the
 * alignment, which is what a raw-NOR swap image needs: the erase unit, not
 * the block, is the smallest thing the device can reuse.
 */
static void
one_contiguous_run(void)
{
	size_t a[3];
	size_t span;

	reset(SLOTS);
	mfree(fixture, 64, ALIGN);
	span = malloc3_contiguous(fixture, 5, 3, 2, ALIGN, a);

	/* Ten units round up to three four-unit erase units. */
	HK_CHECK(span == 12);
	HK_CHECK(a[0] == ALIGN);
	HK_CHECK(a[1] == a[0] + 5);
	HK_CHECK(a[2] == a[1] + 3);
	HK_CHECK(free_total() == 52);

	/* The rounding is charged to the map, so returning the span restores it. */
	mfree(fixture, span, a[0]);
	HK_CHECK(runs() == 1);
	HK_CHECK(free_total() == 64);

	/* An empty request has no run to place. */
	HK_CHECK(malloc3_contiguous(fixture, 0, 0, 0, ALIGN, a) == 0);

	/* A request past the map's largest run fails without disturbing it. */
	HK_CHECK(malloc3_contiguous(fixture, 96, 0, 0, ALIGN, a) == 0);
	HK_CHECK(free_total() == 64);

	reset(SLOTS);
	mfree(fixture, 64, ALIGN);
	HK_EXPECT_PANIC("malloc3_contiguous: bad alignment",
	    call_contiguous_align_six);
	HK_EXPECT_PANIC("malloc3_contiguous: bad alignment",
	    call_contiguous_align_zero);
}

/*
 * malloc3_contiguous_next() is the raw-swap allocator. It searches from the
 * cursor upward, wraps below it once, and advances the cursor past whatever
 * it placed, so consecutive images land on different erase units instead of
 * returning to the lowest free address after every release.
 */
static void
cursor_rotation(void)
{
	size_t a[3];
	size_t cursor;
	size_t first, second;

	reset(SLOTS);
	mfree(fixture, 60, ALIGN);
	cursor = ALIGN;

	/* The first two images take the bottom of the run in order. */
	first = malloc3_contiguous_next(fixture, 8, 0, 0, ALIGN, &cursor, a);
	HK_CHECK(first == 8);
	HK_CHECK(a[0] == 4);
	HK_CHECK(cursor == 12);

	second = malloc3_contiguous_next(fixture, 8, 0, 0, ALIGN, &cursor, a);
	HK_CHECK(second == 8);
	HK_CHECK(a[0] == 12);
	HK_CHECK(cursor == 20);

	/* Releasing the first image reopens the bottom of the map. */
	mfree(fixture, first, 4);
	HK_CHECK(runs() == 2);
	HK_CHECK(run_addr(0) == 4);

	/*
	 * The third image passes over the reopened run and lands at the
	 * cursor. Without the cursor this is where the allocator would hand
	 * back address 4 and erase that unit a second time.
	 */
	HK_CHECK(malloc3_contiguous_next(fixture, 8, 0, 0, ALIGN, &cursor, a) == 8);
	HK_CHECK(a[0] == 20);
	HK_CHECK(cursor == 28);
	HK_CHECK(run_addr(0) == 4);
	HK_CHECK(run_size(0) == 8);

	/* A cursor past every run wraps to the lowest address that fits. */
	reset(SLOTS);
	mfree(fixture, 60, ALIGN);
	cursor = 64;
	HK_CHECK(malloc3_contiguous_next(fixture, 8, 0, 0, ALIGN, &cursor, a) == 8);
	HK_CHECK(a[0] == 4);
	HK_CHECK(cursor == 12);

	/* The three addresses keep the layout the swap code reads them in. */
	reset(SLOTS);
	mfree(fixture, 60, ALIGN);
	cursor = ALIGN;
	HK_CHECK(malloc3_contiguous_next(fixture, 5, 3, 2, ALIGN, &cursor, a) == 12);
	HK_CHECK(a[0] == 4);
	HK_CHECK(a[1] == 9);
	HK_CHECK(a[2] == 12);
	HK_CHECK(cursor == 16);

	/* A request no run can hold returns zero and leaves the cursor alone. */
	HK_CHECK(malloc3_contiguous_next(fixture, 200, 0, 0, ALIGN, &cursor, a) == 0);
	HK_CHECK(cursor == 16);
}

/*
 * Placing a run inside a free run costs one descriptor. When the map has none
 * to spare the search takes a run boundary instead, which weakens rotation
 * but never turns a request the map can hold into a refusal.
 */
static void
cursor_under_descriptor_pressure(void)
{
	size_t a[3];
	size_t cursor;

	/* With headroom, the cursor splits the interior of the upper run. */
	reset(SLOTS);
	mfree(fixture, 8, 4);
	mfree(fixture, 44, 20);
	cursor = 24;
	HK_CHECK(runs() == 2);
	HK_CHECK(malloc3_contiguous_next(fixture, 8, 0, 0, ALIGN, &cursor, a) == 8);
	HK_CHECK(a[0] == 24);
	HK_CHECK(runs() == 3);
	HK_CHECK(cursor == 32);

	/*
	 * Three slots, two of them live, leave no descriptor for a split, so
	 * the same request takes the top of the run. The map still holds both
	 * runs and the allocation still succeeds.
	 */
	reset(3);
	mfree(fixture, 8, 4);
	mfree(fixture, 44, 20);
	cursor = 24;
	HK_CHECK(runs() == 2);
	HK_CHECK(malloc3_contiguous_next(fixture, 8, 0, 0, ALIGN, &cursor, a) == 8);
	HK_CHECK(a[0] == 56);
	HK_CHECK(runs() == 2);
	HK_CHECK(run_addr(1) == 20);
	HK_CHECK(run_size(1) == 36);
	HK_CHECK(cursor == 64);
}

static void
call_next_align_six(void)
{
	(void)malloc3_contiguous_next(fixture, 4, 0, 0, 6, &scratch_cursor,
	    scratch_a);
}

static void
call_next_without_cursor(void)
{
	(void)malloc3_contiguous_next(fixture, 4, 0, 0, ALIGN, NULL, scratch_a);
}

static void
call_next_on_unaligned_map(void)
{
	(void)malloc3_contiguous_next(fixture, 4, 0, 0, ALIGN, &scratch_cursor,
	    scratch_a);
}

/*
 * The cursor allocator's own protocol checks. A caller without a cursor has
 * no rotation state to advance, and a map whose runs are not aligned cannot
 * yield an aligned run without splitting one, which is the condition the
 * allocator refuses to reason about.
 */
static void
cursor_panics(void)
{
	reset(SLOTS);
	mfree(fixture, 60, ALIGN);
	scratch_cursor = ALIGN;

	HK_EXPECT_PANIC("malloc3_contiguous_next: bad alignment",
	    call_next_align_six);
	HK_EXPECT_PANIC("malloc3_contiguous_next: no cursor",
	    call_next_without_cursor);

	reset(SLOTS);
	mfree(fixture, 10, 6);
	scratch_cursor = ALIGN;
	HK_EXPECT_PANIC("malloc3_contiguous_next: unaligned map",
	    call_next_on_unaligned_map);
}

/*
 * Fill the map, drain it in each order, and require that no space is lost and
 * no descriptor past m_limit is touched.
 */
static void
fill_and_drain(void)
{
	size_t taken[SLOTS];
	unsigned i;

	reset(SLOTS);
	mfree(fixture, 8 * SLOTS, ALIGN);

	for (i = 0; i < SLOTS; i++) {
		taken[i] = malloc(fixture, 8);
		HK_CHECK(taken[i] != 0);
	}
	HK_CHECK(free_total() == 0);
	HK_CHECK(malloc(fixture, 1) == 0);

	/* Returning them in reverse order merges each into its successor. */
	for (i = SLOTS; i-- > 0; )
		mfree(fixture, 8, taken[i]);
	HK_CHECK(runs() == 1);
	HK_CHECK(run_addr(0) == ALIGN);
	HK_CHECK(free_total() == 8 * SLOTS);
	HK_CHECK(hk_printf_calls == 0);

	/* Returning them in allocation order merges each into its predecessor. */
	for (i = 0; i < SLOTS; i++)
		HK_CHECK(malloc(fixture, 8) == taken[i]);
	for (i = 0; i < SLOTS; i++)
		mfree(fixture, 8, taken[i]);
	HK_CHECK(runs() == 1);
	HK_CHECK(free_total() == 8 * SLOTS);
	HK_CHECK(hk_printf_calls == 0);
}

int
main(void)
{
	first_fit();
	protocol_panics();
	coalescing();
	overflow_is_reported();
#ifdef DIAGNOSTIC
	overlap_panics();
#endif
	three_segments();
	one_contiguous_run();
	cursor_rotation();
	cursor_under_descriptor_pressure();
	cursor_panics();
	fill_and_drain();
	return hk_verdict("subr_rmap");
}
