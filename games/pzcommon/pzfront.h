/*
 * Text front end shared by the puzzles of games/puzzlebox (flood, samegame,
 * unruly, lightup). Each puzzle is one of Simon Tatham's portable puzzles,
 * imported unchanged under games/pzcommon/tatham/ and driven here without
 * the collection's midend: the front end calls its generator,
 * interpret_move and execute_move directly through the pz_ops table the
 * puzzle's own wrapper fills in, because only that wrapper, which includes
 * the puzzle source, can read the puzzle's private state.
 *
 * The front end owns the game ID, the undo history, the keyboard, the
 * console and the exit paths. A puzzle supplies its rules through
 * pz_ops and draws its board into a pz_canvas of two-character cells.
 */
#ifndef GAMES_PZFRONT_H
#define GAMES_PZFRONT_H

#include "tatham/puzzles.h"

#define PZ_MAXW		24	/* cells across: 2 columns each plus borders fit 53 */
#define PZ_MAXH		16	/* cells down: the frame fits 24 rows */

/* Colors are 0 for the terminal default, else 1 black, 2 red, 3 green,
 * 4 yellow, 5 blue, 6 magenta, 7 cyan, 8 white (SGR 30 or 40 plus n - 1). */
#define PZ_BLACK	1
#define PZ_RED		2
#define PZ_GREEN	3
#define PZ_YELLOW	4
#define PZ_BLUE		5
#define PZ_MAGENTA	6
#define PZ_CYAN		7
#define PZ_WHITE	8

#define PZ_BOLD		1
#define PZ_DIM		2
#define PZ_REV		4

struct pz_cell {
	char c[2];
	unsigned char fg, bg, attr;
};

struct pz_canvas {
	int w, h;			/* board size in cells */
	int cx, cy;			/* cursor cell, or -1 for none */
	struct pz_cell cell[PZ_MAXH][PZ_MAXW];
	char status[48];		/* after the title on the first line */
	char message[40];		/* result line; empty while playing */
};

/*
 * One puzzle. States, parameters and the interface are void pointers
 * because the puzzle's types exist only in its wrapper. A function that
 * returns a string returns memory from dupstr() that the front end frees
 * with sfree(); one that returns an error returns a static string.
 */
struct pz_ops {
	const char *name;		/* dispatch name and title */
	const char *keys;		/* the puzzle's own keys, one help line */
	/* Default parameters with the text decoded over them. */
	void *(*params)(const char *text);
	/* Reject unusable parameters; give the board size in cells. */
	const char *(*check)(void *params, int *w, int *h);
	char *(*encode)(void *params);
	char *(*new_desc)(void *params, random_state *rs, char **aux);
	const char *(*check_desc)(void *params, const char *desc);
	void *(*new_game)(void *params, const char *desc);
	void *(*dup_game)(const void *state);
	void (*free_game)(void *state);
	void *(*new_ui)(const void *state);
	void (*free_ui)(void *ui);
	/* The state changed from old (NULL for a new game) to new. */
	void (*changed)(void *ui, const void *oldstate, const void *newstate);
	/* A move string, MOVE_UI_UPDATE, MOVE_NO_EFFECT or MOVE_UNUSED. */
	char *(*interpret)(const void *state, void *ui, int key);
	void *(*execute)(const void *state, const char *move);
	/* The puzzle's solution move, or NULL when it cannot solve. */
	char *(*solve)(const void *orig, const void *cur, const char *aux);
	/* Moves, ';' apart, that reach the solution from the given state. */
	char *(*script)(const void *state, const char *solution);
	void (*render)(const void *state, const void *ui, struct pz_canvas *cv);
	int (*status)(const void *state);	/* +1 solved, -1 lost, 0 playing */
};

/*
 * Run one puzzle: usage is "name [-m] [-s seed] [params | params:desc]".
 * Returns the process exit status.
 */
int pz_main(const struct pz_ops *op, int argc, char **argv);

/* The keys every puzzle shares: the cursor and the two select buttons. */
static inline int
pz_button(int key)
{
	switch (key) {
	case 'h': return CURSOR_LEFT;
	case 'j': return CURSOR_DOWN;
	case 'k': return CURSOR_UP;
	case 'l': return CURSOR_RIGHT;
	case ' ':
	case '\r':
	case '\n': return CURSOR_SELECT;
	case 'm': return CURSOR_SELECT2;
	}
	return 0;
}

/* Text color that reads on background color bg. */
static inline unsigned char
pz_contrast(unsigned bg)
{
	return bg == PZ_BLACK || bg == PZ_RED || bg == PZ_BLUE ||
	    bg == PZ_MAGENTA ? PZ_WHITE : PZ_BLACK;
}

#endif /* GAMES_PZFRONT_H */
