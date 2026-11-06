/*
 * Host rule test of lightup.c (games/pzcommon/tatham). For the presets and
 * fixed seeds it generates a puzzle, asks solve_game for the solution, plays
 * its L and I moves one at a time through execute_move, and judges the
 * finished grid with a checker written here (every white cell lit, no two
 * lamps seeing each other, every number met exactly). The puzzle must report
 * completion at the end and the S move must mark the game as solved with
 * help. Bad parameters, descriptions and moves must be refused.
 */
#include "pztest.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "../tatham/lightup.c"
#pragma GCC diagnostic pop

#define NSEEDS 3

/* Rule check of the lamps on a finished state; returns a reason or NULL. */
static const char *
lamps_error(const game_state *s)
{
	int w = s->w, h = s->h, x, y, k, nx, ny, n;
	static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
	unsigned f;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			f = s->flags[y * w + x];
			if (f & F_BLACK) {
				if (!(f & F_NUMBERED))
					continue;
				for (k = 0, n = 0; k < 4; k++) {
					nx = x + dx[k];
					ny = y + dy[k];
					if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
					    (s->flags[ny * w + nx] & F_LIGHT))
						n++;
				}
				if (n != s->lights[y * w + x])
					return "number not met";
				continue;
			}
			/* Walk each direction until a black cell or the edge. */
			for (k = 0, n = (f & F_LIGHT) ? 1 : 0; k < 4; k++) {
				nx = x + dx[k];
				ny = y + dy[k];
				while (nx >= 0 && nx < w && ny >= 0 && ny < h &&
				    !(s->flags[ny * w + nx] & F_BLACK)) {
					if (s->flags[ny * w + nx] & F_LIGHT) {
						if (f & F_LIGHT)
							return "lamps see each other";
						n++;
						break;
					}
					nx += dx[k];
					ny += dy[k];
				}
			}
			if (n == 0)
				return "dark cell";
		}
	return NULL;
}

static void
play_one(int preset, int seed)
{
	game_params *p;
	char *name = NULL, *aux = NULL, *desc, *sol, *tok, *save, *rest;
	const char *err = NULL, *why;
	random_state *rs = seeded("lightup", preset * 100 + seed);
	game_state *s0, *s, *t;
	int moves = 0;

	if (!game_fetch_preset(preset, &name, &p))
		return;
	desc = new_game_desc(p, rs, &aux, false);
	CHECK(validate_desc(p, desc) == NULL, "%s/%d: generated description", name, seed);
	s0 = new_game(NULL, p, desc);
	CHECK(!s0->completed && game_status(s0) == 0, "%s/%d: fresh game completed", name, seed);
	sol = solve_game(s0, s0, aux, &err);
	CHECK(sol != NULL && sol[0] == 'S', "%s/%d: solve_game", name, seed);
	if (!sol)
		return;

	/* The moves after "S;", one at a time. */
	save = dupstr(sol);
	s = dup_game(s0);
	for (tok = save + 2; *tok; tok = rest) {
		rest = strchr(tok, ';');
		if (rest)
			*rest++ = 0;
		else
			rest = tok + strlen(tok);
		t = execute_move(s, tok);
		CHECK(t != NULL, "%s/%d: move %s refused", name, seed, tok);
		if (!t)
			break;
		free_game(s);
		s = t;
		moves++;
	}
	CHECK(moves > 0, "%s/%d: empty solution", name, seed);
	why = lamps_error(s);
	CHECK(why == NULL, "%s/%d: finished grid breaks a rule: %s", name, seed, why ? why : "");
	CHECK(s->completed && !s->used_solve && game_status(s) == +1,
	    "%s/%d: not completed by the solution", name, seed);

	t = execute_move(s0, sol);
	CHECK(t != NULL && t->completed && t->used_solve && game_status(t) == +1,
	    "%s/%d: S move", name, seed);
	if (t)
		free_game(t);

	/* A grid short of the solution is not complete. */
	if (s0->nlights == 0) {
		t = execute_move(s0, "L0,0");
		if (t) {
			CHECK(!t->completed || lamps_error(t) == NULL,
			    "%s/%d: completion without a valid grid", name, seed);
			free_game(t);
		}
	}
	free_game(s);
	free_game(s0);
	sfree(save);
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
	random_state *rs = seeded("lightup-refusals", 1);
	char *aux = NULL, *desc, buf[40];
	game_state *s, *t;
	size_t i;
	int x, y, bx = -1, by = -1;
	static const char *const bad[] = {
		"", "L", "L,", "Lx,y", "L99,0", "L0,99", "L-1,0", "I99,99", "Q0,0", "L0,0;;L1,1", "L0"
	};

	decode_params(p, "7x7b20s4d0");
	desc = new_game_desc(p, rs, &aux, false);
	s = new_game(NULL, p, desc);
	for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		t = execute_move(s, bad[i]);
		CHECK(t == NULL, "bad move \"%s\" accepted", bad[i]);
		if (t)
			free_game(t);
	}
	for (y = 0; y < s->h && bx < 0; y++)
		for (x = 0; x < s->w; x++)
			if (s->flags[y * s->w + x] & F_BLACK) {
				bx = x;
				by = y;
				break;
			}
	CHECK(bx >= 0, "no black cell in the generated grid");
	sprintf(buf, "L%d,%d", bx, by);
	t = execute_move(s, buf);
	CHECK(t == NULL, "lamp on a black cell accepted");
	if (t)
		free_game(t);
	CHECK(validate_desc(p, "xyz") != NULL, "bad description accepted");
	CHECK(validate_desc(p, "") != NULL, "empty description accepted");
	decode_params(p, "1x9");
	CHECK(validate_params(p, true) != NULL, "1x9 accepted");
	decode_params(p, "7x7b2");
	CHECK(validate_params(p, true) != NULL, "2 percent black accepted");
	free_game(s);
	sfree(desc);
	sfree(aux);
	free_params(p);
	random_free(rs);
}

static void
run(void)
{
	int preset, seed;

	for (preset = 0; preset < 8; preset++)
		for (seed = 0; seed < NSEEDS; seed++)
			play_one(preset, seed);
	refusals();
}

RUN_PUZZLE_TEST("lightup")
