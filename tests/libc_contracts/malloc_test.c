/*
 * The arena allocator, lib/libc/gen/malloc.c, and calloc.c compiled from
 * the tree with their names moved aside, over an sbrk() the test owns: a
 * static arena with a movable ceiling, so exhaustion is a value the test
 * sets rather than a state the host has to be driven into. sbrk.c from
 * lib/libc/arm/sys runs over a _brk() the test owns for the same reason.
 *
 * The decisive check is the one the historical realloc() failed: after a
 * resize is refused, the original block is still the caller's. Its bytes
 * alone do not show that, because a freed block keeps its bytes until
 * something reuses it; the test allocates again after the refusal and
 * requires that allocation to land elsewhere.
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *db_malloc(size_t);
void db_free(void *);
void *db_realloc(void *, size_t);
void *db_calloc(size_t, size_t);
void *port_sbrk(int);
int test_brk(const void *);

/*
 * The arena: a window with a ceiling the test moves. The allocator links
 * its blocks in address order from its own static pair, so the window has
 * to sit above the allocator's bss, which the host heap does and a static
 * array in this file, linked ahead of malloc.o, does not.
 */
#define ARENA_BYTES	(256 * 1024)
#define BLOCK		1024
static char *arena_base;
static char *arena_break;
static char *arena_ceiling;
static int sbrk_calls;
static int sbrk_refusals;

void *
test_sbrk(int incr)
{
	char *old = arena_break;

	sbrk_calls++;
	if (incr < 0 || (size_t)incr > (size_t)(arena_ceiling - arena_break)) {
		sbrk_refusals++;
		errno = ENOMEM;
		return (void *)-1;
	}
	arena_break += incr;
	return old;
}

/*
 * The _brk() under sbrk.c: refuses everything above brk_ceiling. The
 * initial break, _end on the target, is this array, so the test decides
 * where the break starts on every host.
 */
char test_end[BLOCK];
static const char *brk_ceiling;
static const char *brk_seen;

int
test_brk(const void *addr)
{
	brk_seen = addr;
	if ((const char *)addr > brk_ceiling) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

static int failures;
static int checks;

static void
check(int condition, const char *what)
{
	checks++;
	if (!condition) {
		failures++;
		fprintf(stderr, "malloc test: FAIL: %s\n", what);
	}
}

static void
reset_arena(void)
{
	/* A fresh arena per phase is impossible: the allocator's statics
	 * persist. The ceiling is what each phase controls. */
	arena_ceiling = arena_base + ARENA_BYTES;
	sbrk_calls = 0;
	sbrk_refusals = 0;
}

static void
fill(unsigned char *p, size_t n, unsigned seed)
{
	size_t i;

	for (i = 0; i < n; i++)
		p[i] = (unsigned char)(seed + i * 7);
}

static int
holds(const unsigned char *p, size_t n, unsigned seed)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i] != (unsigned char)(seed + i * 7))
			return 0;
	return 1;
}

static int
inside(const void *p, const void *base, size_t n)
{
	return (const char *)p >= (const char *)base &&
	    (const char *)p < (const char *)base + n;
}

/* Sizes and the sbrk width are the target's: size_t and int are 32 bits. */
static void
width_contract(void)
{
#ifdef HK_ILP32
	check(sizeof(size_t) == 4, "size_t is four bytes at ILP32");
	check(sizeof(void *) == 4, "pointers are four bytes at ILP32");
	check(sizeof(int) == 4, "int is four bytes at ILP32");
#endif
}

static void
zero_and_limit_contract(void)
{
	void *p;
	int calls;

	reset_arena();
	errno = 0;
	p = db_malloc(0);
	check(p == NULL, "malloc(0) is NULL");
	check(errno == 0, "malloc(0) leaves errno alone");

	calls = sbrk_calls;
	errno = 0;
	p = db_malloc(SIZE_MAX);
	check(p == NULL, "malloc(SIZE_MAX) is NULL");
	check(errno == ENOMEM, "malloc(SIZE_MAX) is ENOMEM");
	check(sbrk_calls == calls, "malloc(SIZE_MAX) touches no arena");

	errno = 0;
	p = db_malloc((size_t)INT_MAX / 2);
	check(p == NULL, "malloc(INT_MAX/2) is NULL");
	check(errno == ENOMEM, "malloc(INT_MAX/2) is ENOMEM");
	check(sbrk_calls == calls, "malloc(INT_MAX/2) touches no arena");

	p = db_malloc(1);
	check(p != NULL, "malloc(1) succeeds after the rejected requests");
	db_free(p);
}

static void
calloc_contract(void)
{
	unsigned char *p;
	int calls;
	size_t i;

	reset_arena();
	calls = sbrk_calls;
	errno = 0;
	p = db_calloc(SIZE_MAX / 4 + 1, 8);
	check(p == NULL, "calloc overflow is NULL");
	check(errno == ENOMEM, "calloc overflow is ENOMEM");
	check(sbrk_calls == calls, "calloc overflow touches no arena");

	errno = 0;
	p = db_calloc(0x40000001U, 4);
	check(p == NULL, "calloc(0x40000001, 4) is NULL");
	check(errno == ENOMEM, "calloc(0x40000001, 4) is ENOMEM");

	errno = 0;
	p = db_calloc(0, 16);
	check(p == NULL, "calloc(0, n) is NULL, as malloc(0) is");
	check(errno == 0, "calloc(0, n) leaves errno alone");
	p = db_calloc(16, 0);
	check(p == NULL, "calloc(n, 0) is NULL, as malloc(0) is");

	p = db_calloc(7, 9);
	check(p != NULL, "calloc(7, 9) succeeds");
	if (p != NULL) {
		for (i = 0; i < 63; i++)
			if (p[i] != 0)
				break;
		check(i == 63, "calloc(7, 9) is zeroed");
		db_free(p);
	}
}

static void
realloc_in_place_contract(void)
{
	unsigned char *p, *q, *r, *s;

	reset_arena();
	p = db_malloc(64);
	check(p != NULL, "malloc(64)");
	fill(p, 64, 3);
	/* No block follows p but the arena remainder, so growth is in place. */
	q = db_realloc(p, 128);
	check(q == p, "growth into the free remainder stays in place");
	check(holds(q, 64, 3), "in-place growth keeps the contents");
	fill(q, 128, 5);

	r = db_realloc(q, 16);
	check(r == q, "shrink stays in place");
	check(holds(r, 16, 5), "shrink keeps the prefix");
	s = db_malloc(8);
	check(s != NULL, "malloc after shrink");
	check(inside(s, r + 16, 128 - 16),
	    "the split-off tail is allocatable");
	db_free(s);

	/* A busy neighbor: growth has to move. */
	s = db_malloc(32);
	fill(r, 16, 9);
	q = db_realloc(r, 4096);
	check(q != NULL && q != r, "growth past a busy neighbor moves");
	check(q != NULL && holds(q, 16, 9), "a moved block keeps its prefix");
	p = db_malloc(16);
	check(p == r, "the old block is free after a successful move");
	db_free(p);
	db_free(q);
	db_free(s);

	/* Growth that absorbs the block the search pointer stands on. */
	p = db_malloc(100);
	q = db_malloc(100);
	s = db_malloc(100);
	fill(p, 100, 11);
	db_free(q);		/* allocp now points at q's header */
	r = db_realloc(p, 180);
	check(r == p, "growth absorbs the freed neighbor in place");
	check(holds(r, 100, 11), "absorbing growth keeps the contents");
	fill(r, 180, 13);
	q = db_malloc(24);
	check(q != NULL && !inside(q, r, 180),
	    "malloc after absorbing growth lands outside the grown block");
	check(holds(r, 180, 13),
	    "malloc after absorbing growth leaves the grown block intact");
	db_free(q);
	db_free(r);
	db_free(s);
}

static void
realloc_zero_contract(void)
{
	unsigned char *p, *q;

	reset_arena();
	p = db_malloc(40);
	errno = 0;
	q = db_realloc(p, 0);
	check(q == NULL, "realloc(p, 0) is NULL");
	check(errno == 0, "realloc(p, 0) leaves errno alone");
	q = db_malloc(40);
	check(q == p, "realloc(p, 0) freed p");
	db_free(q);

	errno = 0;
	q = db_realloc(NULL, 40);
	check(q != NULL, "realloc(NULL, n) allocates");
	db_free(q);
	errno = 0;
	q = db_realloc(NULL, 0);
	check(q == NULL && errno == 0, "realloc(NULL, 0) is malloc(0)");
}

/*
 * The decisive case. The resize is refused; the block is then proven
 * still allocated by allocating again and requiring the result to lie
 * outside it. The historical allocator freed p first, so this second
 * allocation landed on p.
 *
 * The phase lays out its own arena: p, a live guard behind it so growth
 * cannot happen in place, and a spare block that is freed before the
 * ceiling freezes, so the allocation after the refusal has a legitimate
 * home. Without the spare, the phase run alone in a fresh process has no
 * free extent for it and a correct allocator fails the check; run after
 * the earlier phases it borrows their freed blocks, which is an order the
 * check must not depend on.
 */
static void
realloc_failure_preserves(void)
{
	unsigned char *p, *guard, *spare, *r, *m;
	int calls;

	reset_arena();
	p = db_malloc(2000);
	guard = db_malloc(16);
	spare = db_malloc(1200);
	check(p != NULL && guard != NULL && spare != NULL, "setup allocations");
	fill(p, 2000, 17);
	db_free(spare);

	/* Growth cannot happen in place (guard) and cannot extend (ceiling). */
	arena_ceiling = arena_break;
	calls = sbrk_calls;
	errno = 0;
	r = db_realloc(p, 200000);
	check(r == NULL, "a refused resize returns NULL");
	check(errno == ENOMEM, "a refused resize is ENOMEM");
	check(sbrk_refusals > 0, "the refusal came from the arena ceiling");
	check(holds(p, 2000, 17), "the original bytes are intact");

	m = db_malloc(1000);
	check(m != NULL, "an allocation that fits the freed spare succeeds");
	check(m == NULL || !inside(m, p, 2000),
	    "the allocation after a refused resize lands outside p");
	check(m == NULL || !inside(p, m, 1000),
	    "p does not lie inside the new allocation");
	/* Writing every byte of m is what shows p is not aliased by it:
	 * unchanged bytes right after the refusal prove nothing until
	 * something else has written. */
	if (m != NULL)
		fill(m, 1000, 91);
	check(holds(p, 2000, 17),
	    "the original bytes survive a write through the next allocation");
	check(m == NULL || holds(m, 1000, 91),
	    "the next allocation holds its own bytes");
	db_free(m);

	/* An arithmetic refusal never reaches the arena. */
	reset_arena();
	calls = sbrk_calls;
	errno = 0;
	r = db_realloc(p, SIZE_MAX - 3);
	check(r == NULL && errno == ENOMEM, "realloc(p, SIZE_MAX - 3) is ENOMEM");
	check(sbrk_calls == calls, "an oversized resize touches no arena");
	check(holds(p, 2000, 17), "an oversized resize leaves p intact");

	/* The block is still the caller's to resize and free. */
	r = db_realloc(p, 2100);
	check(r != NULL && holds(r, 2000, 17), "p resizes after the refusal");
	db_free(r);
	db_free(guard);
}

static void
malloc_exhaustion_contract(void)
{
	unsigned char *p, *q;

	reset_arena();
	p = db_malloc(300);
	fill(p, 300, 19);
	arena_ceiling = arena_break;
	errno = 0;
	q = db_malloc(200000);
	check(q == NULL, "malloc past the ceiling is NULL");
	check(errno == ENOMEM, "malloc past the ceiling is ENOMEM");
	check(holds(p, 300, 19), "a refused extension leaves live blocks alone");
	reset_arena();
	q = db_malloc(200000);
	check(q != NULL, "the same request succeeds once the ceiling lifts");
	check(q != NULL && !inside(q, p, 300) && !inside(p, q, 200000),
	    "the late block lies outside the live one");
	db_free(q);
	db_free(p);
}

/*
 * A deterministic sequence of allocate, resize and free over a table of
 * live blocks, each holding a pattern, so coalescing, splitting and the
 * search pointer are exercised together and any overlap shows as a
 * pattern that no longer holds.
 */
#define LIVE	48
static void
arena_integrity(void)
{
	struct { unsigned char *p; size_t n; unsigned seed; } live[LIVE];
	unsigned state = 12345;
	int i, k, ok = 1, moves = 0;
	size_t n;

	reset_arena();
	memset(live, 0, sizeof live);
	for (i = 0; i < 4000 && ok; i++) {
		state = state * 1103515245u + 12345u;
		k = (int)((state >> 16) % LIVE);
		state = state * 1103515245u + 12345u;
		n = 1 + (state >> 16) % 700;
		if (live[k].p == NULL) {
			live[k].p = db_malloc(n);
			if (live[k].p == NULL) { ok = 0; break; }
			live[k].n = n;
			live[k].seed = state;
			fill(live[k].p, n, state);
		} else if (state & 4) {
			unsigned char *r = db_realloc(live[k].p, n);
			if (r == NULL) { ok = 0; break; }
			if (r != live[k].p)
				moves++;
			if (!holds(r, n < live[k].n ? n : live[k].n,
			    live[k].seed)) { ok = 0; break; }
			live[k].p = r;
			live[k].n = n;
			live[k].seed = state;
			fill(r, n, state);
		} else {
			db_free(live[k].p);
			live[k].p = NULL;
		}
		for (k = 0; k < LIVE; k++)
			if (live[k].p != NULL &&
			    !holds(live[k].p, live[k].n, live[k].seed))
				ok = 0;
	}
	check(ok, "4000 mixed operations keep every live pattern");
	check(moves > 0, "the sequence exercised moved resizes");
	for (k = 0; k < LIVE; k++)
		db_free(live[k].p);
}

/* lib/libc/arm/sys/sbrk.c over the test's _brk(). */
extern const char *_curbrk;
static void
sbrk_contract(void)
{
	const char *start = _curbrk;
	void *r;

	brk_ceiling = start + 4096;
	r = port_sbrk(0);
	check(r == start, "sbrk(0) is the current break");
	check(brk_seen == NULL, "sbrk(0) makes no _brk call");

	r = port_sbrk(1024);
	check(r == start, "sbrk(n) returns the old break");
	check(_curbrk == start + 1024, "sbrk(n) advances the break");
	check(brk_seen == start + 1024, "sbrk(n) asks _brk for old + n");

	errno = 0;
	r = port_sbrk(8192);
	check(r == (void *)-1, "a refused sbrk returns (void *)-1");
	check(errno == ENOMEM, "a refused sbrk carries _brk's errno");
	check(_curbrk == start + 1024, "a refused sbrk leaves the break");

	r = port_sbrk(-1024);
	check(r == start + 1024 && _curbrk == start,
	    "a negative sbrk returns the break");
}

/*
 * A phase named on the command line runs alone, which is how the decisive
 * case is shown to fail on the historical allocator without first tripping
 * over its unchecked arithmetic; no argument runs every phase.
 */
static const struct { const char *name; void (*run)(void); } phases[] = {
	{ "width", width_contract },
	{ "zero_and_limit", zero_and_limit_contract },
	{ "calloc", calloc_contract },
	{ "realloc_in_place", realloc_in_place_contract },
	{ "realloc_zero", realloc_zero_contract },
	{ "realloc_failure_preserves", realloc_failure_preserves },
	{ "malloc_exhaustion", malloc_exhaustion_contract },
	{ "arena_integrity", arena_integrity },
	{ "sbrk", sbrk_contract },
};

int
main(int argc, char **argv)
{
	size_t i;
	int ran = 0;

	arena_base = malloc(ARENA_BYTES + BLOCK);
	if (arena_base == NULL)
		return 2;
	arena_base += BLOCK - (uintptr_t)arena_base % BLOCK;
	arena_break = arena_base;
	reset_arena();
	for (i = 0; i < sizeof phases / sizeof phases[0]; i++) {
		if (argc > 1 && strcmp(argv[1], phases[i].name) != 0)
			continue;
		phases[i].run();
		ran++;
	}
	if (ran == 0) {
		fprintf(stderr, "malloc test: no phase named %s\n", argv[1]);
		return 2;
	}
	if (failures) {
		fprintf(stderr, "malloc test: %d of %d checks failed\n",
		    failures, checks);
		return 1;
	}
	printf("malloc test: %d checks passed\n", checks);
	return 0;
}
