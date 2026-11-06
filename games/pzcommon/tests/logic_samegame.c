/*
 * Host rule test of samegame.c (games/pzcommon/tatham). Same Game has no
 * solver of its own, so this test brings one: a depth-first search written
 * here over the groups of touching equal colors, with a memo of the boards
 * already refuted, which clears each generated grid (the generator
 * promises a grid that can be cleared). The groups it found are then played
 * back through the puzzle's own interface, one CURSOR_SELECT to select a
 * group and one more to remove it, and the finished game must report
 * completion, an emptied board and the score the scoring rule gives for the
 * groups removed. Bad parameters, descriptions and moves must be refused.
 */
#include "pztest.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "../tatham/samegame.c"
#pragma GCC diagnostic pop

#define NSEEDS 8
#define MAXN 400
#define MAXDEPTH 200
#define MEMO 65536
#define NODE_CAP 400000

static const char *const specs[] = { "5x5c3s2", "4x4c3s2", "6x4c3s1", "5x5c4s2", "3x3c3s2", "8x5c3s2" };

static unsigned long long memo[MEMO];
static int nodes;
/* The group removed at each depth of the solution found: its first cell. */
static int path_cell[MAXDEPTH];
static int path_size[MAXDEPTH];

static unsigned long long
board_hash(const game_state *s)
{
	unsigned long long h = 1469598103934665603ULL;
	int i;

	for (i = 0; i < s->n; i++)
		h = (h ^ (unsigned long long)(s->tiles[i] + 1)) * 1099511628211ULL;
	return h | 1ULL;
}

static int
memo_seen(unsigned long long h)
{
	unsigned i = (unsigned)(h % MEMO);

	while (memo[i]) {
		if (memo[i] == h)
			return 1;
		i = (i + 1) % MEMO;
	}
	memo[i] = h;
	return 0;
}

/* Collect the group of cell start into list; returns its size. */
static int
group_of(const game_state *s, int start, int *list, char *seen)
{
	int w = s->params.w, h = s->params.h, n = 0, head = 0, i, x, y, nb[4], k;
	int col = s->tiles[start];

	list[n++] = start;
	seen[start] = 1;
	while (head < n) {
		i = list[head++];
		x = i % w;
		y = i / w;
		nb[0] = x > 0 ? i - 1 : -1;
		nb[1] = x + 1 < w ? i + 1 : -1;
		nb[2] = y > 0 ? i - w : -1;
		nb[3] = y + 1 < h ? i + w : -1;
		for (k = 0; k < 4; k++)
			if (nb[k] >= 0 && !seen[nb[k]] && s->tiles[nb[k]] == col) {
				seen[nb[k]] = 1;
				list[n++] = nb[k];
			}
	}
	return n;
}

static int
board_empty(const game_state *s)
{
	int i;

	for (i = 0; i < s->n; i++)
		if (s->tiles[i])
			return 0;
	return 1;
}

static int
dfs(const game_state *s, int depth)
{
	char seen[MAXN];
	int list[MAXN], i, n, k;
	char mv[MAXN * 4];
	game_state *t;

	if (board_empty(s))
		return depth + 1;
	if (depth >= MAXDEPTH || ++nodes > NODE_CAP || memo_seen(board_hash(s)))
		return 0;
	memset(seen, 0, sizeof seen);
	for (i = 0; i < s->n; i++) {
		if (!s->tiles[i] || seen[i])
			continue;
		n = group_of(s, i, list, seen);
		if (n < 2)
			continue;
		strcpy(mv, "M");
		for (k = 0; k < n; k++)
			sprintf(mv + strlen(mv), "%s%d", k ? "," : "", list[k]);
		t = execute_move(s, mv);
		if (!t)
			continue;
		path_cell[depth] = i;
		path_size[depth] = n;
		k = dfs(t, depth + 1);
		free_game(t);
		if (k)
			return k;
	}
	return 0;
}

static void
play_one(const char *spec, int seed)
{
	game_params *p = default_params();
	random_state *rs = seeded("samegame", seed);
	char *aux = NULL, *desc, *mv;
	game_state *s0, *s, *t;
	game_ui *ui;
	int depth, d, expect = 0, x, w;

	decode_params(p, spec);
	CHECK(validate_params(p, true) == NULL, "%s: params", spec);
	w = p->w;
	desc = new_game_desc(p, rs, &aux, false);
	CHECK(validate_desc(p, desc) == NULL, "%s/%d: generated description", spec, seed);
	s0 = new_game(NULL, p, desc);
	CHECK(!s0->complete && game_status(s0) == 0, "%s/%d: fresh game complete", spec, seed);

	memset(memo, 0, sizeof memo);
	nodes = 0;
	depth = dfs(s0, 0) - 1;	/* dfs returns the move count plus one on success */
	CHECK(depth >= 0, "%s/%d: no clearing found in %d nodes", spec, seed, nodes);
	if (depth > 0) {
		s = dup_game(s0);
		ui = new_ui(s);
		for (d = 0; d < depth; d++) {
			ui->xsel = path_cell[d] % w;
			ui->ysel = path_cell[d] / w;
			mv = interpret_move(s, ui, NULL, 0, 0, CURSOR_SELECT);
			CHECK(mv == MOVE_UI_UPDATE && ui->nselected == path_size[d],
			    "%s/%d: select %d gave %d cells, want %d", spec, seed, d,
			    ui->nselected, path_size[d]);
			mv = interpret_move(s, ui, NULL, 0, 0, CURSOR_SELECT);
			CHECK(mv != MOVE_UI_UPDATE && mv != MOVE_NO_EFFECT && mv != MOVE_UNUSED && mv[0] == 'M',
			    "%s/%d: remove %d gave no move", spec, seed, d);
			if (mv[0] != 'M')
				break;
			t = execute_move(s, mv);
			sfree(mv);
			CHECK(t != NULL, "%s/%d: removal %d refused", spec, seed, d);
			if (!t)
				break;
			x = path_size[d] - p->scoresub;
			expect += x > 0 ? x * x : 0;
			free_game(s);
			s = t;
			game_changed_state(ui, NULL, s);
		}
		CHECK(s->complete && board_empty(s) && game_status(s) == +1,
		    "%s/%d: board not cleared", spec, seed);
		CHECK(s->score == expect, "%s/%d: score %d, want %d", spec, seed, s->score, expect);
		free_ui(ui);
		free_game(s);
	}
	free_game(s0);
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
	game_ui *ui;
	char *mv;
	size_t i;
	static const char *const bad[] = { "", "Mx", "M99", "M-1", "Q1", "M1,x", "S" };

	decode_params(p, "2x2c3s2");
	CHECK(validate_params(p, true) == NULL, "2x2c3 refused");
	CHECK(validate_desc(p, "1,1,2,2") == NULL, "good description refused");
	CHECK(validate_desc(p, "1,1,2") != NULL, "short description accepted");
	CHECK(validate_desc(p, "1,1,2,x") != NULL, "bad number accepted");
	CHECK(validate_desc(p, "1,1,2,9") != NULL, "color past the limit accepted");
	s = new_game(NULL, p, "1,1,2,2");
	for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		t = execute_move(s, bad[i]);
		CHECK(t == NULL, "bad move \"%s\" accepted", bad[i]);
		if (t)
			free_game(t);
	}
	/* Cells 0 and 1 form a group; 2 and 3 form another. */
	ui = new_ui(s);
	ui->xsel = 0;
	ui->ysel = 0;
	mv = interpret_move(s, ui, NULL, 0, 0, CURSOR_SELECT);
	CHECK(mv == MOVE_UI_UPDATE && ui->nselected == 2, "select a pair");
	mv = interpret_move(s, ui, NULL, 0, 0, CURSOR_SELECT2);
	CHECK(mv == MOVE_UI_UPDATE && ui->nselected == 0, "select2 on a selected pair clears it");
	mv = interpret_move(s, ui, NULL, 0, 0, 'z');
	CHECK(mv == MOVE_UNUSED, "unknown key used");
	free_ui(ui);
	free_game(s);
	decode_params(p, "5x5c2s2");
	CHECK(validate_params(p, true) != NULL, "2 colors accepted for a soluble grid");
	decode_params(p, "5x5c10s2");
	CHECK(validate_params(p, true) != NULL, "10 colors accepted");
	decode_params(p, "5x5c3s3");
	CHECK(validate_params(p, true) != NULL, "scoring 3 accepted");
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
	refusals();
}

RUN_PUZZLE_TEST("samegame")
