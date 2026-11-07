/*
 * Host rule test of flood.c (games/pzcommon/tatham). For fixed seeds and a
 * range of parameters it generates a puzzle, asks solve_game for its move
 * string, plays that string through execute_move (the solution path) and
 * interpret_move (CURSOR_SELECT2 advances one solver step), and requires
 * game_status to report a win within the move limit. A flood fill written
 * here from the rules replays the same colors on the generated grid and must
 * leave one color. Bad parameters, descriptions and moves must be refused.
 */
#include "pztest.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "../tatham/flood.c"
#pragma GCC diagnostic pop

#define NSEEDS 12
#define MAXCELLS 400

static const char *const specs[] = {
	"12x12c6m5", "12x12c6m0", "5x5c3m0", "8x8c4m2", "16x16c8m1", "3x3c3m0", "2x7c5m3"
};

/* Recolor the region of cell 0 with color c: the cells joined to cell 0
 * through cells of its current color. */
static void
fill_region(char *g, int w, int h, int c)
{
	int stack[MAXCELLS], sp = 0, i, n, k;
	char old = g[0];
	static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };

	if (old == c)
		return;
	g[0] = (char)c;
	stack[sp++] = 0;
	while (sp > 0) {
		i = stack[--sp];
		for (k = 0; k < 4; k++) {
			int x = i % w + dx[k], y = i / w + dy[k];

			if (x < 0 || x >= w || y < 0 || y >= h)
				continue;
			n = y * w + x;
			if (g[n] == old) {
				g[n] = (char)c;
				stack[sp++] = n;
			}
		}
	}
}

static int
uniform(const char *g, int n)
{
	int i;

	for (i = 1; i < n; i++)
		if (g[i] != g[0])
			return 0;
	return 1;
}

static void
play_one(const char *spec, int seed)
{
	game_params *p = default_params();
	random_state *rs = seeded("flood", seed);
	char *aux = NULL, *desc, *sol, *mv;
	const char *err = NULL;
	game_state *s0, *s, *t;
	game_ui *ui;
	char grid[MAXCELLS];
	int w, h, i, steps = 0, limit, leniency;

	decode_params(p, spec);
	CHECK(validate_params(p, true) == NULL, "%s: params", spec);
	w = p->w;
	h = p->h;
	desc = new_game_desc(p, rs, &aux, false);
	CHECK(validate_desc(p, desc) == NULL, "%s/%d: generated description", spec, seed);
	for (i = 0; i < w * h; i++)
		grid[i] = (char)(desc[i] <= '9' ? desc[i] - '0' : desc[i] - 'A' + 10);
	limit = atoi(strchr(desc, ',') + 1);
	leniency = p->leniency;
	s0 = new_game(NULL, p, desc);
	CHECK(game_status(s0) == 0, "%s/%d: fresh game status", spec, seed);
	CHECK(!uniform(grid, w * h), "%s/%d: generated an already solved grid", spec, seed);

	sol = solve_game(s0, s0, aux, &err);
	CHECK(sol != NULL && sol[0] == 'S', "%s/%d: solve_game", spec, seed);
	if (!sol)
		return;
	s = execute_move(s0, sol);
	CHECK(s != NULL, "%s/%d: solve move refused", spec, seed);
	ui = new_ui(s);
	while (s && !s->complete && steps < w * h) {
		mv = interpret_move(s, ui, NULL, 0, 0, CURSOR_SELECT2);
		CHECK(mv != MOVE_UNUSED && mv != MOVE_NO_EFFECT && mv != MOVE_UI_UPDATE && mv[0] == 'M',
		    "%s/%d: advance step %d", spec, seed, steps);
		if (mv[0] != 'M')
			break;
		fill_region(grid, w, h, atoi(mv + 1));
		t = execute_move(s, mv);
		sfree(mv);
		CHECK(t != NULL, "%s/%d: step %d refused", spec, seed, steps);
		if (!t)
			break;
		free_game(s);
		s = t;
		steps++;
	}
	CHECK(s && s->complete, "%s/%d: solution did not finish", spec, seed);
	CHECK(uniform(grid, w * h), "%s/%d: independent fill left several colors", spec, seed);
	CHECK(game_status(s) == +1, "%s/%d: final status %d", spec, seed, game_status(s));
	CHECK(steps + leniency == limit, "%s/%d: %d steps + %d spare != limit %d",
	    spec, seed, steps, leniency, limit);
	CHECK(s->moves == steps, "%s/%d: move counter %d != %d", spec, seed, s->moves, steps);

	/* A finished game accepts no more moves. */
	mv = interpret_move(s, ui, NULL, 0, 0, CURSOR_SELECT);
	CHECK(mv == MOVE_NO_EFFECT, "%s/%d: select on a finished game", spec, seed);
	free_ui(ui);
	free_game(s);
	free_game(s0);
	sfree(sol);
	sfree(desc);
	sfree(aux);
	free_params(p);
	random_free(rs);
}

/* Out of moves: playing the colors in turn without progress ends in defeat. */
static void
lose_one(void)
{
	game_params *p = default_params();
	random_state *rs = seeded("flood-lose", 1);
	char *aux = NULL, *desc, buf[16];
	game_state *s, *t;
	int i;

	decode_params(p, "6x6c4m0");
	desc = new_game_desc(p, rs, &aux, false);
	s = new_game(NULL, p, desc);
	for (i = 0; i < 400 && game_status(s) == 0; i++) {
		sprintf(buf, "M%d", (s->grid[0] + 1) % s->colours);
		t = execute_move(s, buf);
		CHECK(t != NULL, "lose: move %s refused", buf);
		if (!t)
			break;
		free_game(s);
		s = t;
	}
	CHECK(game_status(s) != 0, "lose: game never ended");
	CHECK(game_status(s) == -1 || s->complete, "lose: status %d", game_status(s));
	free_game(s);
	sfree(desc);
	sfree(aux);
	free_params(p);
	random_free(rs);
}

static void
refusals(void)
{
	game_params *p = default_params();
	game_state *s, *t;
	char *e;
	static const char *const bad_moves[] = {
		"", "M", "Mx", "M-1", "M99", "M6", "Q1", "S", "S9", "S6,0", "S0,0"
	};
	size_t i;

	CHECK(validate_desc(p, "0123") != NULL, "short description accepted");
	CHECK(validate_desc(p, "") != NULL, "empty description accepted");
	decode_params(p, "2x2c3m0");
	CHECK(validate_desc(p, "01a2,5") != NULL, "lower case color accepted");
	CHECK(validate_desc(p, "0120;5") != NULL, "missing comma accepted");
	CHECK(validate_desc(p, "0120,x") != NULL, "bad move limit accepted");
	CHECK(validate_desc(p, "0120,5") == NULL, "good description refused");
	s = new_game(NULL, p, "0120,5");
	for (i = 0; i < sizeof bad_moves / sizeof bad_moves[0]; i++) {
		t = execute_move(s, bad_moves[i]);
		CHECK(t == NULL, "bad move \"%s\" accepted", bad_moves[i]);
		if (t)
			free_game(t);
	}
	t = execute_move(s, "M0");
	CHECK(t == NULL, "move to the current color accepted");
	t = execute_move(s, "M1");
	CHECK(t != NULL && t->moves == 1, "good move refused");
	if (t) {
		free_game(t);
	}
	free_game(s);
	decode_params(p, "5x5c2m0");
	CHECK(validate_params(p, true) != NULL, "2 colors accepted");
	decode_params(p, "5x5c11m0");
	CHECK(validate_params(p, true) != NULL, "11 colors accepted");
	decode_params(p, "5x5c4m-1");
	CHECK(validate_params(p, true) != NULL, "negative leniency accepted");
	decode_params(p, "1x1c3m0");
	CHECK(validate_params(p, true) != NULL, "1x1 accepted");
	e = encode_params(p, true);
	CHECK(strcmp(e, "1x1c3m0") == 0, "encode_params: %s", e);
	sfree(e);
	free_params(p);
}

static void
run(void)
{
	size_t i;
	int seed;

	for (i = 0; i < sizeof specs / sizeof specs[0]; i++)
		for (seed = 0; seed < NSEEDS; seed++)
			play_one(specs[i], seed);
	lose_one();
	refusals();
}

RUN_PUZZLE_TEST("flood")
