/*
 * lightup -- Simon Tatham's Light Up puzzle on a text console: place lamps
 * on the white cells so that every white cell is lit, no two lamps light
 * each other, and every numbered black cell touches exactly that many
 * lamps. The rules are the collection's lightup.c, imported unchanged and
 * driven through games/pzcommon/pzfront.c; this file draws the board and
 * maps keys.
 *
 * usage: lightup [-m] [-s seed] [params | params:desc]
 *
 * params are WxH, then an optional bN (percent black), sN (symmetry) and
 * dN (difficulty 0 to 2), as in the collection: 7x7b20s4d0. Keys: arrows
 * move the cursor, Enter or Space places or removes a lamp, m or i marks a
 * cell as holding no lamp, u undoes, y redoes, S solves, n starts a new game
 * and q quits. Lit cells are yellow, lamps are bold, two lamps that see
 * each other and a number that cannot be met are red. A black cell is ##, or
 * its number, so the walls stay visible without color (-m).
 */
#define NO_TGMATH_H

#include <stdio.h>
#include <string.h>

#include "../pzcommon/pzfront.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "../pzcommon/tatham/lightup.c"
#pragma GCC diagnostic pop

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
	*w = p->w;
	*h = p->h;
	return NULL;
}

static char *
w_encode(void *p)
{
	return encode_params(p, true);
}

static char *
w_new_desc(void *p, random_state *rs, char **aux)
{
	return new_game_desc(p, rs, aux, false);
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

	u->cur_visible = true;
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
	((game_ui *)u)->cur_visible = true;
}

static char *
w_interpret(const void *s, void *u, int key)
{
	int c = pz_button(key);

	if (key == 'i')
		c = CURSOR_SELECT2;
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

/* "S;L1,2;I3,4" becomes "L1,2;I3,4". */
static char *
w_script(const void *s, const char *sol)
{
	(void)s;
	return dupstr(sol[0] == 'S' && sol[1] == ';' ? sol + 2 : sol);
}

static void
w_render(const void *sv, const void *uv, struct pz_canvas *cv)
{
	const game_state *s = sv;
	const game_ui *u = uv;
	int x, y, lamps = 0, n;
	unsigned f;
	struct pz_cell *c;

	cv->w = s->w;
	cv->h = s->h;
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			f = GRID(s, flags, x, y);
			n = GRID(s, lights, x, y);
			c = &cv->cell[y][x];
			if (f & F_BLACK) {
				c->bg = PZ_BLACK;
				c->fg = PZ_WHITE;
				/* A glyph, not only the black background: with -m
				 * the colors are dropped and a wall would read as
				 * a lit white cell. */
				c->c[0] = c->c[1] = '#';
				if (f & F_NUMBERED) {
					c->c[1] = ' ';
					c->c[0] = (char)('0' + n);
					c->attr = PZ_BOLD;
					if (number_wrong(s, x, y))
						c->fg = PZ_RED;
				}
				continue;
			}
			if (n > 0) {
				c->bg = PZ_YELLOW;
				c->fg = PZ_BLACK;
			}
			c->c[0] = c->c[1] = ' ';
			if (f & F_LIGHT) {
				lamps++;
				c->c[0] = '<';
				c->c[1] = '>';
				c->attr = PZ_BOLD;
				if (n > 1) {
					c->fg = PZ_RED;
					c->bg = n > 0 ? PZ_YELLOW : 0;
				}
			} else if (f & F_IMPOSSIBLE) {
				c->c[0] = 'x';
				c->attr = PZ_DIM;
			} else if (n == 0) {
				c->c[0] = '.';
				c->attr = PZ_DIM;
			}
		}
	cv->cx = u->cur_x;
	cv->cy = u->cur_y;
	sprintf(cv->status, "lamps %d", lamps);
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
	unsigned n = (unsigned)(s->w * s->h);

	return pz_heap(sizeof(game_state)) + pz_heap(n * sizeof(int)) +
	    pz_heap(n * sizeof(unsigned int));
}

static const struct pz_ops lightup_ops = {
	"lightup",
	"Enter lamp  m mark no lamp  S solve",
	w_params, w_check, w_encode, w_new_desc, w_check_desc, w_new_game,
	w_dup, w_free, w_new_ui, w_free_ui, w_changed, w_interpret, w_execute,
	w_solve, w_script, w_render, w_status, w_bytes
};

int
main(int argc, char **argv)
{
	return pz_main(&lightup_ops, argc, argv);
}
