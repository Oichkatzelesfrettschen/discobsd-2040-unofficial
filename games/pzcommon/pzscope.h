/*
 * Reclaiming the memory a puzzle generator abandons. The collection's
 * unruly.c new_game_desc() drops a candidate game state, with every block it
 * owns, each time the candidate proves too easy for the chosen difficulty; the
 * imported source cannot change, so the wrapper that includes it redirects
 * the collection's allocator names (smalloc, srealloc, sfree) to the tracking
 * versions below and brackets the generator call with PZ_SCOPED_DESC.
 *
 * Between pz_scope_begin() and pz_scope_finish() every block the redirected
 * names allocate is recorded and every one they free is forgotten.
 * pz_scope_finish() frees the blocks still recorded, which the generator
 * leaked, except the one it returns to its caller. Outside that bracket the
 * redirected names call straight through to the collection's allocator.
 *
 * Include this header after tatham/puzzles.h and before the puzzle source.
 * The tracking list belongs to one thread of control and the bracket does not
 * nest.
 */
#ifndef GAMES_PZSCOPE_H
#define GAMES_PZSCOPE_H

#include <stddef.h>

void pz_scope_begin(void);
char *pz_scope_finish(char *keep);
void *pz_scope_alloc(size_t size);
void *pz_scope_realloc(void *p, size_t size);
void pz_scope_free(void *p);

#ifndef PZ_SCOPE_NO_REDIRECT
#define smalloc		pz_scope_alloc
#define srealloc	pz_scope_realloc
#define sfree		pz_scope_free
#endif

/* The generator's description, with its abandoned blocks reclaimed. The
 * arguments are the generator and its parameters, random state and aux
 * pointer; the comma operator runs the bracket in order. */
#define PZ_SCOPED_DESC(gen, params, rs, aux) \
	(pz_scope_begin(), pz_scope_finish((gen)((params), (rs), (aux), false)))

#endif /* GAMES_PZSCOPE_H */
