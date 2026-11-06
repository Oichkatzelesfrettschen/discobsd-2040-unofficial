/*
 * Host rule test of unruly.c (games/pzcommon/tatham). For every preset and
 * fixed seeds it generates a puzzle, asks solve_game for the solution, and
 * checks that solution with a rule checker written here (equal counts of
 * ones and zeros in every row and column, no three equal digits in a line,
 * distinct rows and columns when the unique flag is set, every clue kept).
 * Then it plays the solution one P move at a time through execute_move and
 * requires the puzzle to report completion only after the last cell, and
 * the S move to complete and mark the game as solved with help. Bad
 * parameters, descriptions and moves must be refused.
 */
#include "pztest.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "../tatham/unruly.c"
#pragma GCC diagnostic pop

#define NSEEDS 5

/* Rule check of a w2 by h2 grid of '0' and '1'; returns a reason or NULL. */
static const char *
solution_error(const char *g, int w2, int h2, int unique)
{
	int x, y, k, ones;

	for (y = 0; y < h2; y++) {
		for (x = 0, ones = 0; x < w2; x++)
			ones += g[y * w2 + x] == '1';
		if (ones != w2 / 2)
			return "row count";
		for (x = 2; x < w2; x++)
			if (g[y * w2 + x] == g[y * w2 + x - 1] && g[y * w2 + x] == g[y * w2 + x - 2])
				return "three in a row";
	}
	for (x = 0; x < w2; x++) {
		for (y = 0, ones = 0; y < h2; y++)
			ones += g[y * w2 + x] == '1';
		if (ones != h2 / 2)
			return "column count";
		for (y = 2; y < h2; y++)
			if (g[y * w2 + x] == g[(y - 1) * w2 + x] && g[y * w2 + x] == g[(y - 2) * w2 + x])
				return "three in a column";
	}
	if (unique) {
		for (y = 0; y < h2; y++)
			for (k = y + 1; k < h2; k++)
				if (memcmp(g + y * w2, g + k * w2, (size_t)w2) == 0)
					return "equal rows";
		for (x = 0; x < w2; x++)
			for (k = x + 1; k < w2; k++) {
				for (y = 0; y < h2; y++)
					if (g[y * w2 + x] != g[y * w2 + k])
						break;
				if (y == h2)
					return "equal columns";
			}
	}
	return NULL;
}

static void
play_one(int preset, int seed)
{
	game_params *p;
	char *name = NULL, *aux = NULL, *desc, *sol, mv[40];
	const char *err = NULL, *why;
	random_state *rs = seeded("unruly", preset * 100 + seed);
	game_state *s0, *s, *t;
	int w2, h2, i, last = -1, unique;

	if (!game_fetch_preset(preset, &name, &p))
		return;
	w2 = p->w2;
	h2 = p->h2;
	unique = p->unique;
	desc = new_game_desc(p, rs, &aux, false);
	CHECK(validate_desc(p, desc) == NULL, "%s/%d: generated description", name, seed);
	s0 = new_game(NULL, p, desc);
	CHECK(!s0->completed && game_status(s0) == 0, "%s/%d: fresh game completed", name, seed);
	sol = solve_game(s0, s0, aux, &err);
	CHECK(sol != NULL && sol[0] == 'S' && (int)strlen(sol) == 1 + w2 * h2,
	    "%s/%d: solve_game", name, seed);
	if (!sol)
		return;
	why = solution_error(sol + 1, w2, h2, unique);
	CHECK(why == NULL, "%s/%d: solution breaks a rule: %s", name, seed, why ? why : "");
	for (i = 0; i < w2 * h2; i++)
		if (s0->common->immutable[i])
			CHECK(sol[1 + i] == (s0->grid[i] == N_ONE ? '1' : '0'),
			    "%s/%d: solution changes clue %d", name, seed, i);

	/* One cell at a time, the way a player fills the grid. */
	s = dup_game(s0);
	for (i = 0; i < w2 * h2; i++)
		if (!s0->common->immutable[i])
			last = i;
	for (i = 0; i < w2 * h2; i++) {
		if (s0->common->immutable[i])
			continue;
		sprintf(mv, "P%c,%d,%d", sol[1 + i], i % w2, i / w2);
		t = execute_move(s, mv);
		CHECK(t != NULL, "%s/%d: move %s refused", name, seed, mv);
		if (!t)
			break;
		free_game(s);
		s = t;
		if (i != last)
			CHECK(!s->completed, "%s/%d: completed before cell %d", name, seed, last);
	}
	CHECK(s->completed && !s->cheated && game_status(s) == +1,
	    "%s/%d: not completed by the solution", name, seed);

	/* An immutable clue cannot be changed. */
	for (i = 0; i < w2 * h2 && !s0->common->immutable[i]; i++)
		;
	if (i < w2 * h2) {
		sprintf(mv, "P%c,%d,%d", s0->grid[i] == N_ONE ? '0' : '1', i % w2, i / w2);
		t = execute_move(s0, mv);
		CHECK(t == NULL, "%s/%d: clue %d overwritten", name, seed, i);
		if (t)
			free_game(t);
	}

	/* The S move completes the game and marks it cheated. */
	t = execute_move(s0, sol);
	CHECK(t != NULL && t->completed && t->cheated && game_status(t) == +1,
	    "%s/%d: S move", name, seed);
	if (t)
		free_game(t);
	free_game(s);
	free_game(s0);
	sfree(sol);
	sfree(desc);
	sfree(aux);
	sfree(name);
	free_params(p);
	random_free(rs);
}

static void
refusals(void)
{
	game_params *p = default_params();
	game_state *s, *t;
	size_t i;
	static const char *const bad[] = {
		"", "P", "P1", "P1,0", "P1,x,0", "P1,99,0", "P1,0,99", "P1,-1,0", "Pz,0,0",
		"S", "S01", "S2222222222222222222222222222222222222222222222222222222222222222",
		"Q1,0,0"
	};
	const char *e;

	random_state *rs = seeded("unruly-refusals", 1);
	char *aux = NULL, *desc;

	decode_params(p, "6x6");
	desc = new_game_desc(p, rs, &aux, false);
	s = new_game(NULL, p, desc);
	sfree(desc);
	sfree(aux);
	random_free(rs);
	for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		t = execute_move(s, bad[i]);
		CHECK(t == NULL, "bad move \"%s\" accepted", bad[i]);
		if (t)
			free_game(t);
	}
	free_game(s);
	CHECK(validate_desc(p, "abc") != NULL, "short description accepted");
	CHECK(validate_desc(p, "a") != NULL, "one-letter description accepted");
	decode_params(p, "7x8");
	CHECK(validate_params(p, true) != NULL, "odd width accepted");
	decode_params(p, "4x4");
	CHECK(validate_params(p, true) != NULL, "4x4 accepted");
	decode_params(p, "8x8");
	e = validate_params(p, true);
	CHECK(e == NULL, "8x8 refused: %s", e ? e : "");
	free_params(p);
}

static void
run(void)
{
	int preset, seed;

	for (preset = 0; preset < 7; preset++)
		for (seed = 0; seed < NSEEDS; seed++)
			play_one(preset, seed);
	refusals();
}

RUN_PUZZLE_TEST("unruly")
