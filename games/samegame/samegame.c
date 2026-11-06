/*
 * samegame -- Simon Tatham's Same Game puzzle on a text console: clear the
 * grid by removing groups of two or more touching cells of one color, which
 * drops the cells above into the gap and slides empty columns together. The
 * rules are the collection's samegame.c, imported unchanged and driven
 * through games/pzcommon/pzfront.c; this file draws the board and maps keys.
 *
 * usage: samegame [-m] [-s seed] [params | params:desc]
 *
 * params are WxH, then cN (colors, 3 to 8) and sN (scoring, 1 or 2) and an
 * optional r for a grid that need not be soluble, as in the collection:
 * 5x5c3s2. Keys: arrows move the cursor, Enter or Space selects the group
 * under it and, on a selected group, removes it, m or c clears the
 * selection, u undoes, y redoes, n starts a new game and q quits. A
 * selected group prints as (); the score of removing it shows in the status
 * line.
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
#include "../pzcommon/tatham/samegame.c"
#pragma GCC diagnostic pop

#define MAXPLAY 8	/* colors the console draws */

static const unsigned char sg_bg[MAXPLAY + 1] = {
	0, PZ_RED, PZ_GREEN, PZ_BLUE, PZ_YELLOW, PZ_MAGENTA, PZ_CYAN, PZ_WHITE, PZ_BLACK
};

static struct game_drawstate dummy_ds = { .tileinner = 1 };

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
	if (p->ncols > MAXPLAY)
		return "at most 8 colors fit the console";
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

	u->displaysel = true;
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
	int c = key == 'c' ? CURSOR_SELECT2 : pz_button(key);

	return c ? interpret_move(s, u, &dummy_ds, 0, 0, c) : MOVE_UNUSED;
}

static void *
w_execute(const void *s, const char *m)
{
	return execute_move(s, m);
}

static void
w_render(const void *sv, const void *uv, struct pz_canvas *cv)
{
	const game_state *s = sv;
	const game_ui *u = uv;
	int x, y, col;
	struct pz_cell *c;

	cv->w = s->params.w;
	cv->h = s->params.h;
	for (y = 0; y < cv->h; y++)
		for (x = 0; x < cv->w; x++) {
			col = COL(s, x, y);
			c = &cv->cell[y][x];
			if (col == 0) {
				c->c[0] = c->c[1] = ' ';
				continue;
			}
			c->bg = sg_bg[col];
			c->fg = pz_contrast(sg_bg[col]);
			if (u->tiles[y * s->params.w + x] & TILE_SELECTED) {
				c->c[0] = '(';
				c->c[1] = ')';
				c->attr = PZ_BOLD;
			} else {
				c->c[0] = c->c[1] = (char)('0' + col);
			}
		}
	cv->cx = u->xsel;
	cv->cy = u->ysel;
	if (u->nselected > 0)
		sprintf(cv->status, "score %d  group %d (+%d)", s->score,
		    u->nselected, npoints(&s->params, u->nselected));
	else
		sprintf(cv->status, "score %d", s->score);
	if (s->impossible && !s->complete)
		strcpy(cv->message, "NO MOVES LEFT  u: undo  n: new game");
}

static int
w_status(const void *s)
{
	return game_status(s);
}

static const struct pz_ops samegame_ops = {
	"samegame",
	"Enter select, then remove  m c clear",
	w_params, w_check, w_encode, w_new_desc, w_check_desc, w_new_game,
	w_dup, w_free, w_new_ui, w_free_ui, w_changed, w_interpret, w_execute,
	NULL, NULL, w_render, w_status
};

int
main(int argc, char **argv)
{
	return pz_main(&samegame_ops, argc, argv);
}
