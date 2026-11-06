/*
 * flood -- Simon Tatham's Flood puzzle on a text console: fill the grid with
 * one color by repeatedly flood-filling the top left corner, within a move
 * limit. The rules are the collection's flood.c, imported unchanged and
 * driven through games/pzcommon/pzfront.c; this file draws the board and
 * maps keys.
 *
 * usage: flood [-m] [-s seed] [params | params:desc]
 *
 * params are WxH, optionally followed by cN (colors, 3 to 8) and mN (spare
 * moves), as in the collection: 12x12c6m5. Keys: arrows move the cursor,
 * Enter or Space fills with the color under the cursor, 1 to 8 fill with
 * that color, u undoes, y redoes, S shows a solution that m then plays one
 * step at a time, n starts a new game and q quits. The region the fill
 * point controls is bold.
 *
 * The collection's presentation functions are compiled but unreachable from
 * main (-fwhole-program makes the puzzle table static), so the link drops
 * them.
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
#include "../pzcommon/tatham/flood.c"
#pragma GCC diagnostic pop

#define MAXPLAY 8	/* colors the console draws */

/* Background of each color: red, green, yellow, blue, magenta, cyan, white,
 * black. */
static const unsigned char fl_bg[MAXPLAY] = {
	PZ_RED, PZ_GREEN, PZ_YELLOW, PZ_BLUE, PZ_MAGENTA, PZ_CYAN, PZ_WHITE, PZ_BLACK
};

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
	if (p->colours > MAXPLAY)
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

	u->cursor_visible = true;
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
w_interpret(const void *sv, void *uv, int key)
{
	const game_state *s = sv;
	char buf[16];
	int c;

	if (key >= '1' && key < '1' + s->colours) {
		c = key - '1';
		if (s->complete || c == s->grid[0])
			return MOVE_NO_EFFECT;
		sprintf(buf, "M%d", c);
		return dupstr(buf);
	}
	c = pz_button(key);
	return c ? interpret_move(s, uv, NULL, 0, 0, c) : MOVE_UNUSED;
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

/* "S3,1,2" becomes "M3;M1;M2". */
static char *
w_script(const void *s, const char *sol)
{
	char *out = snewn(strlen(sol) * 3 + 2, char), *o = out;

	(void)s;
	for (sol++; *sol; sol++) {
		if (sol[-1] == 'S' || sol[-1] == ',')
			*o++ = 'M';
		*o++ = *sol == ',' ? ';' : *sol;
	}
	*o = 0;
	return out;
}

static void
w_render(const void *sv, const void *uv, struct pz_canvas *cv)
{
	const game_state *s = sv;
	const game_ui *u = uv;
	int stack[PZ_MAXW * PZ_MAXH], reach[PZ_MAXW * PZ_MAXH];
	int sp = 0, x, y, i, k, c, nx, ny;
	static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
	struct pz_cell *cell;

	cv->w = s->w;
	cv->h = s->h;
	memset(reach, 0, sizeof reach);
	stack[sp++] = 0;
	reach[0] = 1;
	while (sp > 0) {
		i = stack[--sp];
		for (k = 0; k < 4; k++) {
			nx = i % s->w + dx[k];
			ny = i / s->w + dy[k];
			if (nx < 0 || nx >= s->w || ny < 0 || ny >= s->h)
				continue;
			if (reach[ny * s->w + nx] || s->grid[ny * s->w + nx] != s->grid[0])
				continue;
			reach[ny * s->w + nx] = 1;
			stack[sp++] = ny * s->w + nx;
		}
	}
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			c = s->grid[y * s->w + x];
			cell = &cv->cell[y][x];
			cell->c[0] = cell->c[1] = (char)('1' + c);
			cell->bg = fl_bg[c];
			cell->fg = pz_contrast(fl_bg[c]);
			cell->attr = reach[y * s->w + x] ? PZ_BOLD : 0;
		}
	cv->cx = u->cx;
	cv->cy = u->cy;
	sprintf(cv->status, "moves %d/%d", s->moves, s->movelimit);
}

static int
w_status(const void *s)
{
	return game_status(s);
}

static const struct pz_ops flood_ops = {
	"flood",
	"1-8 fill  S solve, then m steps through it",
	w_params, w_check, w_encode, w_new_desc, w_check_desc, w_new_game,
	w_dup, w_free, w_new_ui, w_free_ui, w_changed, w_interpret, w_execute,
	w_solve, w_script, w_render, w_status
};

int
main(int argc, char **argv)
{
	return pz_main(&flood_ops, argc, argv);
}
