/*
 * unruly -- Simon Tatham's Unruly puzzle on a text console: fill the grid
 * with ones and zeros so that every row and column holds equally many of
 * each and no three equal digits touch in a line. The rules are the
 * collection's unruly.c, imported unchanged and driven through
 * games/pzcommon/pzfront.c; this file draws the board and maps keys.
 *
 * usage: unruly [-m] [-s seed] [params | params:desc]
 *
 * params are WxH (even, at least 6), then an optional u for unique rows and
 * columns and t, e or n for the difficulty, as in the collection: 8x8n.
 * Keys: arrows move the cursor, Enter or Space cycles a cell through one,
 * zero and empty, m cycles the other way, 1, 0 and x set a cell directly,
 * u undoes, y redoes, S solves, n starts a new game and q quits. Clues are
 * bold; digits that break a rule are red.
 */
#define NO_TGMATH_H

#include <stdio.h>
#include <string.h>

#include "../pzcommon/pzfront.h"
#include "../pzcommon/pzscope.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "../pzcommon/tatham/unruly.c"
#pragma GCC diagnostic pop

/* interpret_move reads the draw state's tile size before it looks at the
 * button, so the keyboard path needs one that divides safely. */
static struct game_drawstate dummy_ds = { .tilesize = 1 };

static void *
w_params(const char *text)
{
	game_params *p = default_params();

	if (text && *text)
		decode_params(p, text);
	return p;
}

static const char *
w_check(void *pv, int *w, int *h)
{
	const game_params *p = pv;
	const char *e = validate_params(p, true);

	if (e)
		return e;
	*w = p->w2;
	*h = p->h2;
	return NULL;
}

static char *
w_encode(void *p)
{
	return encode_params(p, true);
}

/*
 * The generator abandons a candidate game state, and the blocks it owns, each
 * time the candidate is too easy for the difficulty (the "de" and "dn"
 * settings), and nothing frees them. pzscope.h redirects the generator's
 * allocator calls so the bracket reclaims them, which keeps repeated games
 * from growing the process window.
 */
static char *
w_new_desc(void *p, random_state *rs, char **aux)
{
	return PZ_SCOPED_DESC(new_game_desc, p, rs, aux);
}

static const char *
w_check_desc(void *p, const char *desc)
{
	return validate_desc(p, desc);
}

static void *
w_new_game(void *p, const char *desc)
{
	return new_game(NULL, p, desc);
}

static void *
w_dup(const void *s)
{
	return dup_game(s);
}

static void
w_free(void *s)
{
	free_game(s);
}

static void *
w_new_ui(const void *s)
{
	game_ui *u = new_ui(s);

	u->cursor = true;
	return u;
}

static void
w_free_ui(void *u)
{
	free_ui(u);
}

static void
w_changed(void *u, const void *o, const void *n)
{
	game_changed_state(u, o, n);
}

static char *
w_interpret(const void *s, void *u, int key)
{
	int c = pz_button(key);

	if (key == '0' || key == '1')
		c = key;
	else if (key == 'x' || key == '\b' || key == 0x7f)
		c = '\b';
	return c ? interpret_move(s, u, &dummy_ds, 0, 0, c) : MOVE_UNUSED;
}

static void *
w_execute(const void *s, const char *m)
{
	return execute_move(s, m);
}

static char *
w_solve(const void *orig, const void *curr, const char *aux)
{
	const char *err = NULL;

	return solve_game(orig, curr, aux, &err);
}

/* "S0110..." becomes "P0,0,0;P1,1,0;..." for every cell the clues leave. */
static char *
w_script(const void *sv, const char *sol)
{
	const game_state *s = sv;
	char *out = snewn((size_t)s->w2 * s->h2 * 12 + 2, char), *o = out;
	int i;

	for (i = 0; i < s->w2 * s->h2; i++)
		if (!s->common->immutable[i])
			o += sprintf(o, "P%c,%d,%d;", sol[1 + i], i % s->w2, i / s->w2);
	if (o > out)
		o[-1] = 0;
	else
		*o = 0;
	return out;
}

static void
w_render(const void *sv, const void *uv, struct pz_canvas *cv)
{
	const game_state *s = sv;
	const game_ui *u = uv;
	int w2 = s->w2, h2 = s->h2, x, y, i, v;
	int cell_err[PZ_MAXW * PZ_MAXH];
	bool count_err[2 * (PZ_MAXW + PZ_MAXH)];
	struct pz_cell *c;
	int bad;

	cv->w = w2;
	cv->h = h2;
	memset(cell_err, 0, sizeof cell_err);
	memset(count_err, 0, sizeof count_err);
	(void)unruly_validate_all_rows(s, cell_err);
	(void)unruly_validate_counts(s, NULL, count_err);
	for (y = 0; y < h2; y++)
		for (x = 0; x < w2; x++) {
			i = y * w2 + x;
			v = s->grid[i];
			c = &cv->cell[y][x];
			bad = cell_err[i] != 0;
			if (v == N_ONE)
				bad |= count_err[y] || count_err[2 * h2 + x];
			else if (v == N_ZERO)
				bad |= count_err[h2 + y] || count_err[2 * h2 + w2 + x];
			if (v == EMPTY) {
				c->c[0] = '.';
				c->c[1] = ' ';
				c->attr = PZ_DIM;
			} else {
				c->c[0] = c->c[1] = v == N_ONE ? '1' : '0';
				c->bg = v == N_ONE ? PZ_BLACK : PZ_WHITE;
				c->fg = bad ? PZ_RED : v == N_ONE ? PZ_WHITE : PZ_BLACK;
				c->attr = s->common->immutable[i] ? PZ_BOLD : 0;
			}
		}
	cv->cx = u->cx;
	cv->cy = u->cy;
}

static int
w_status(const void *s)
{
	return game_status(s);
}

static unsigned
w_bytes(const void *sv)
{
	const game_state *s = sv;

	return pz_heap(sizeof(game_state)) + pz_heap((unsigned)(s->w2 * s->h2));
}

static const struct pz_ops unruly_ops = {
	"unruly",
	"1 0 set  Enter m cycle  x clear  S solve",
	w_params, w_check, w_encode, w_new_desc, w_check_desc, w_new_game,
	w_dup, w_free, w_new_ui, w_free_ui, w_changed, w_interpret, w_execute,
	w_solve, w_script, w_render, w_status, w_bytes
};

int
main(int argc, char **argv)
{
	return pz_main(&unruly_ops, argc, argv);
}
