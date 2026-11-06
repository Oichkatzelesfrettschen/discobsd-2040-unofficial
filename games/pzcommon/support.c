/*
 * The parts of Simon Tatham's portable puzzle collection that the puzzles of
 * games/puzzlebox call: random numbers from a seed (random.c), allocation
 * with the collection's checks (malloc.c), the shuffle, cursor and string
 * helpers (misc.c) and the combination iterator (combi.c). The sources sit
 * unchanged in tatham/. They are compiled here as one unit with a section
 * per function, and the link discards the helpers the text front end never
 * reaches (colour mixing and drawing helpers that need the collection's
 * graphics layer).
 *
 * The tracking allocator at the end (pzscope.h) lets a puzzle wrapper reclaim
 * the blocks an imported generator abandons.
 *
 * The collection's sources carry no warning policy of this tree: the
 * diagnostics below are accepted for the imported text only.
 */
/* The tree has no <tgmath.h>; the sources include <math.h> instead. */
#define NO_TGMATH_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "tatham/misc.c"
#include "tatham/random.c"
#include "tatham/malloc.c"
#include "tatham/combi.c"
#pragma GCC diagnostic pop

/*
 * The tracking allocator behind pzscope.h. This unit takes the collection's
 * allocator names unredirected, so pz_scope_alloc() calls the real smalloc().
 */
#define PZ_SCOPE_NO_REDIRECT
#include "pzscope.h"

static void **scope_live;		/* blocks allocated and not yet freed */
static size_t scope_n, scope_cap;
static int scope_on;

static void
scope_record(void *p)
{
	if (scope_n == scope_cap) {
		scope_cap = scope_cap ? 2 * scope_cap : 64;
		scope_live = srealloc(scope_live, scope_cap * sizeof *scope_live);
	}
	scope_live[scope_n++] = p;
}

/* Index of p in the list, searching the newest first, or scope_n. */
static size_t
scope_find(const void *p)
{
	size_t i = scope_n;

	while (i > 0 && scope_live[i - 1] != p)
		i--;
	return i > 0 ? i - 1 : scope_n;
}

static void
scope_forget(size_t i)
{
	scope_live[i] = scope_live[--scope_n];
}

void
pz_scope_begin(void)
{
	scope_on = 1;
}

void *
pz_scope_alloc(size_t size)
{
	void *p = smalloc(size);

	if (scope_on && p)
		scope_record(p);
	return p;
}

void *
pz_scope_realloc(void *p, size_t size)
{
	void *q;
	size_t i = scope_n;

	if (scope_on && p)
		i = scope_find(p);
	q = srealloc(p, size);
	if (!scope_on)
		return q;
	if (i < scope_n) {
		if (q)
			scope_live[i] = q;
		else
			scope_forget(i);
	} else if (q) {
		scope_record(q);
	}
	return q;
}

void
pz_scope_free(void *p)
{
	size_t i;

	if (scope_on && p) {
		i = scope_find(p);
		if (i < scope_n)
			scope_forget(i);
	}
	sfree(p);
}

char *
pz_scope_finish(char *keep)
{
	size_t i;

	for (i = 0; i < scope_n; i++)
		if (scope_live[i] != keep)
			sfree(scope_live[i]);
	sfree(scope_live);
	scope_live = NULL;
	scope_n = scope_cap = 0;
	scope_on = 0;
	return keep;
}
