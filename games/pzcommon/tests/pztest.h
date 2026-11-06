/*
 * Shared parts of the host rule tests of games/puzzlebox. A test includes
 * this header, then the puzzle source it checks (inside the diagnostic
 * guard below, since the collection's sources are not held to this tree's
 * -Wextra), and defines run(). It is compiled with -fwhole-program, so the
 * puzzle table is static and unreferenced and the drawing functions the
 * puzzle table points at never reach the link.
 *
 * Each test generates puzzles from fixed seeds, plays the puzzle's own
 * solution through execute_move, and judges the result with a checker
 * written here from the puzzle's rules rather than from its source.
 */
#ifndef GAMES_PZTEST_H
#define GAMES_PZTEST_H

#define NO_TGMATH_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../tatham/puzzles.h"

static int nchk, nchk_bad;

#define CHECK(cond, ...) do { \
	nchk++; \
	if (!(cond)) { \
		nchk_bad++; \
		if (nchk_bad <= 30) { \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} \
} while (0)

/* A generator seeded by a name and a number, so a failure is replayable. */
static random_state *
seeded(const char *tag, int n)
{
	char buf[64];

	sprintf(buf, "%s-%d", tag, n);
	return random_new(buf, (int)strlen(buf));
}

#define RUN_PUZZLE_TEST(name) \
int \
main(void) \
{ \
	run(); \
	printf("%s: %d checks (%d failed)\n", name, nchk, nchk_bad); \
	return nchk_bad ? 1 : 0; \
}

#endif /* GAMES_PZTEST_H */
