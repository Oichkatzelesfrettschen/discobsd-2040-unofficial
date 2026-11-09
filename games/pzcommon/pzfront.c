/*
 * Text front end for the puzzles of games/puzzlebox; see pzfront.h.
 *
 * Screen (1-based rows, 2 terminal columns per board cell): row 1 holds the
 * title and the puzzle's status, row 2 the top border, the board follows
 * from row 3 between walls in columns 2 and 3 + 2 * w, then the bottom
 * border, a blank row, the result line and three help lines. The frame is
 * at most 24 rows by 52 columns, so it fits an 80 by 24 terminal and the
 * PicoCalc's 53 by 32 console alike. Only cells that differ from the
 * previous frame are written.
 *
 * Testing hook: if GAMEBOX_TEST is set, the seed is fixed, a value of the
 * form "params:desc" selects that game, and the frame lists the game ID and,
 * when the puzzle can solve itself, the moves that reach the solution, each
 * in lines of PZ_TESTW characters starting "TEST id " and "TEST moves ". The
 * key '!' then reads one move string up to Enter and executes it, so a pty
 * test can play the printed solution. The key does nothing otherwise.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pzfront.h"
#include "../gameterm.h"

#define PZ_HIST		48	/* most states kept for undo and redo */
#define PZ_HIST_MIN	2	/* fewest: the current state and one undo */
/*
 * Heap the undo history may hold, in the bytes op->state_bytes() reports.
 * The process window is 196608 bytes for text, data, bss, heap and stack; the
 * linked puzzlebox image takes about 46 KB of them, leaving about 148 KB. The
 * generator and solver peak at about 8 KB on the largest board (24 by 16
 * Light Up), the deepest frames come to about 3 KB, and the allocator's
 * first-fit arena needs fragmentation slack, so 48 KB of the window goes to
 * history and the rest to those uses.
 */
#define PZ_HIST_BYTES	49152
#define PZ_CMD		160	/* longest test move */
#define PZ_TESTW	40	/* characters of the game ID or script per test line */

static const struct pz_ops *op;
static void *params;
static char *aux;
static void *ui;
static void *orig;		/* the puzzle as dealt; never part of the history */
static void *hist[PZ_HIST];
static int nhist, cur;
static int hist_max = PZ_HIST;	/* states the budget lets hist[] hold */
static int mono;
static int testmode;
static const char *base_seed;
static int game_no;
static int cmd_on;
static char cmd[PZ_CMD];
static int cmd_len;
static char *test_id, *test_script;
static int test_rows;
static char note[40];

static struct pz_canvas cv, shown;
static int shown_valid;

/*
 * Called by the imported sources on an unrecoverable error. The terminal is
 * restored first so the message lands on a normal screen.
 */
void
fatal(const char *fmt, ...)
{
	gt_leave();
	gt_put_fd(2, fmt, strlen(fmt));
	gt_put_fd(2, "\n", 1);
	exit(1);
}

/* ------------------------------------------------------------------ */
/* Game state                                                         */
/* ------------------------------------------------------------------ */

static void
history_clear(void)
{
	while (nhist > 0)
		op->free_game(hist[--nhist]);
	cur = 0;
	if (orig)
		op->free_game(orig);
	orig = NULL;
}

static void
push(void *ns)
{
	void *old = hist[cur];

	while (nhist > cur + 1)
		op->free_game(hist[--nhist]);
	if (nhist == hist_max) {
		op->free_game(hist[0]);
		memmove(&hist[0], &hist[1], (hist_max - 1) * sizeof hist[0]);
		nhist--;
	}
	hist[nhist++] = ns;
	cur = nhist - 1;
	op->changed(ui, old, ns);
}

static void
apply(char *m)
{
	void *ns;

	if (m == MOVE_UI_UPDATE || m == MOVE_NO_EFFECT || m == MOVE_UNUSED)
		return;
	ns = op->execute(hist[cur], m);
	sfree(m);
	if (ns)
		push(ns);
	else
		strcpy(note, "move refused");
}

/* Build the game for the current parameters from desc or from a fresh draw. */
static int
start_game(const char *desc, const char **err)
{
	char *seed, *d;
	random_state *rs;
	void *s;

	history_clear();
	sfree(aux);
	aux = NULL;
	if (desc) {
		d = dupstr(desc);
	} else {
		/* The seed text is the user's -s argument, a '.', the decimal
		 * game number (at most 11 characters) and the terminator. */
		seed = snewn(strlen(base_seed) + 13, char);
		sprintf(seed, "%s.%d", base_seed, game_no++);
		rs = random_new(seed, (int)strlen(seed));
		d = op->new_desc(params, rs, &aux);
		random_free(rs);
		sfree(seed);
	}
	*err = op->check_desc(params, d);
	if (*err) {
		sfree(d);
		return 0;
	}
	s = op->new_game(params, d);
	if (ui)
		op->free_ui(ui);	/* the old interface belongs to the old game */
	ui = op->new_ui(s);
	orig = op->dup_game(s);
	/*
	 * The dealt copy and the state a move builds before push() evicts the
	 * oldest one stand outside the ring, so they come off the budget.
	 */
	hist_max = (int)(PZ_HIST_BYTES / op->state_bytes(s)) - 2;
	if (hist_max > PZ_HIST)
		hist_max = PZ_HIST;
	if (hist_max < PZ_HIST_MIN)
		hist_max = PZ_HIST_MIN;
	hist[nhist++] = s;
	op->changed(ui, NULL, s);
	sfree(test_id);
	sfree(test_script);
	test_id = test_script = NULL;
	if (testmode) {
		char *e = op->encode(params), *sol;

		test_id = snewn(strlen(e) + strlen(d) + 2, char);
		sprintf(test_id, "%s:%s", e, d);
		sfree(e);
		sol = op->solve ? op->solve(s, s, aux) : NULL;
		if (sol) {
			test_script = op->script(s, sol);
			sfree(sol);
		}
	}
	sfree(d);
	note[0] = 0;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Screen                                                             */
/* ------------------------------------------------------------------ */

#define WALL_L		2
#define CELL_COL	(WALL_L + 1)
#define TOP_ROW		2
#define CELL_ROW	(TOP_ROW + 1)

static void
put_title(void)
{
	const char *p;
	char c;

	for (p = op->name; *p; p++) {
		c = (*p >= 'a' && *p <= 'z') ? (char)(*p - 'a' + 'A') : *p;
		gt_put(&c, 1);
	}
}

static void
draw_static(void)
{
	int r, c;

	gt_str("\033[0m\033[2J");
	gt_goto(TOP_ROW, WALL_L);
	gt_put(".", 1);
	for (c = 0; c < 2 * cv.w; c++)
		gt_put("-", 1);
	gt_put(".", 1);
	for (r = 0; r < cv.h; r++) {
		gt_goto(CELL_ROW + r, WALL_L);
		gt_put("|", 1);
		gt_goto(CELL_ROW + r, CELL_COL + 2 * cv.w);
		gt_put("|", 1);
	}
	gt_goto(CELL_ROW + cv.h, WALL_L);
	gt_put("'", 1);
	for (c = 0; c < 2 * cv.w; c++)
		gt_put("-", 1);
	gt_put("'", 1);
	gt_goto(CELL_ROW + cv.h + 3, 1);
	gt_str("arrows move  Enter select  u undo  y redo");
	gt_goto(CELL_ROW + cv.h + 4, 1);
	gt_str(op->keys);
	gt_goto(CELL_ROW + cv.h + 5, 1);
	gt_str(op->solve ? "n new game  S solve  q quit" : "n new game  q quit");
}

static void
put_cell(const struct pz_cell *c)
{
	gt_str("\033[0");
	if (c->attr & PZ_BOLD)
		gt_str(";1");
	if (c->attr & PZ_DIM)
		gt_str(";2");
	if (c->attr & PZ_REV)
		gt_str(";7");
	if (!mono && c->fg) {
		gt_str(";3");
		gt_num((unsigned long)(c->fg - 1));
	}
	if (!mono && c->bg) {
		gt_str(";4");
		gt_num((unsigned long)(c->bg - 1));
	}
	gt_put("m", 1);
	gt_put(c->c, 2);
	gt_str("\033[0m");
}

static int
cell_same(const struct pz_cell *a, const struct pz_cell *b)
{
	return a->c[0] == b->c[0] && a->c[1] == b->c[1] && a->fg == b->fg &&
	    a->bg == b->bg && a->attr == b->attr;
}

static void
frame(int full)
{
	const void *s = hist[cur];
	int r, c, nr = -1, nc = -1, st;
	struct pz_cell here;

	memset(&cv, 0, sizeof cv);
	cv.cx = cv.cy = -1;
	op->render(s, ui, &cv);
	st = op->status(s);
	if (st > 0)
		strcpy(cv.message, "SOLVED  n: new game");
	else if (st < 0)
		strcpy(cv.message, "OUT OF MOVES  u: undo  n: new game");
	if (note[0] && !cv.message[0])
		strcpy(cv.message, note);
	if (cv.cx >= 0 && cv.cx < cv.w && cv.cy >= 0 && cv.cy < cv.h)
		cv.cell[cv.cy][cv.cx].attr |= PZ_REV;
	if (full || !shown_valid) {
		draw_static();
		shown_valid = 1;
		memset(&shown, 0, sizeof shown);
		shown.w = -1;
		shown.status[0] = shown.message[0] = '\1';
		test_rows = 0;
	}
	for (r = 0; r < cv.h; r++)
		for (c = 0; c < cv.w; c++) {
			here = cv.cell[r][c];
			if (shown.w >= 0 && cell_same(&here, &shown.cell[r][c]))
				continue;
			if (r != nr || c != nc)
				gt_goto(CELL_ROW + r, CELL_COL + 2 * c);
			put_cell(&here);
			nr = r;
			nc = c + 1;
		}
	if (strcmp(cv.status, shown.status) != 0) {
		gt_goto(1, 1);
		put_title();
		gt_str("  ");
		gt_str(cv.status);
		gt_str("\033[K");
	}
	if (strcmp(cv.message, shown.message) != 0) {
		gt_goto(CELL_ROW + cv.h + 2, 1);
		gt_str(cv.message);
		gt_str("\033[K");
	}
	if (testmode && test_rows == 0) {
		const char *p;
		int row = CELL_ROW + cv.h + 7, n;

		for (p = test_id; *p && row <= 31; p += n) {
			n = (int)strlen(p) > PZ_TESTW ? PZ_TESTW : (int)strlen(p);
			gt_goto(row++, 1);
			gt_str("TEST id ");
			gt_put(p, n);
		}
		for (p = test_script; p && *p && row <= 31; p += n) {
			n = (int)strlen(p) > PZ_TESTW ? PZ_TESTW : (int)strlen(p);
			gt_goto(row++, 1);
			gt_str("TEST moves ");
			gt_put(p, n);
		}
		test_rows = 1;
	}
	shown = cv;
	shown.w = cv.w;
	gt_goto(CELL_ROW + cv.h + 2, 1);
	gt_flush();
}

/* ------------------------------------------------------------------ */
/* Keys                                                               */
/* ------------------------------------------------------------------ */

static void
new_game(void)
{
	const char *err;

	if (!start_game(NULL, &err))
		strcpy(note, "cannot start a game");
	shown_valid = 0;
}

static void
run_test_move(void)
{
	void *ns;

	cmd[cmd_len] = 0;
	ns = op->execute(hist[cur], cmd);
	if (ns)
		push(ns);
	else
		strcpy(note, "move refused");
	cmd_on = 0;
}

static int
event(int key, unsigned long now, int expired_ms)
{
	char *m;
	void *old;

	(void)now;
	(void)expired_ms;
	if (key == GT_NONE)
		return 0;
	if (cmd_on) {
		if (key == '\r' || key == '\n')
			run_test_move();
		else if (key >= ' ' && key < 0x7f && cmd_len < PZ_CMD - 1)
			cmd[cmd_len++] = (char)key;
		return 0;
	}
	note[0] = 0;
	switch (key) {
	case '!':
		if (testmode) {
			cmd_on = 1;
			cmd_len = 0;
		}
		return 0;
	case 'u':
		if (cur > 0) {
			old = hist[cur];
			cur--;
			op->changed(ui, old, hist[cur]);
		}
		return 0;
	case 'y':
		if (cur + 1 < nhist) {
			old = hist[cur];
			cur++;
			op->changed(ui, old, hist[cur]);
		}
		return 0;
	case 'n':
		new_game();
		return 0;
	case 'S':
		if (!op->solve)
			return 0;
		m = op->solve(orig, hist[cur], aux);
		if (m)
			apply(m);
		else
			strcpy(note, "nothing to solve");
		return 0;
	}
	m = op->interpret(hist[cur], ui, key);
	if (m)
		apply(m);
	return 0;
}

static int
deadline(void)
{
	return -1;
}

static int
usage(void)
{
	static const char a[] = "usage: ", b[] = " [-m] [-s seed] [params | params:desc]\n";

	gt_put_fd(2, a, sizeof a - 1);
	gt_put_fd(2, op->name, strlen(op->name));
	gt_put_fd(2, b, sizeof b - 1);
	return 2;
}

static void
complain(const char *what)
{
	gt_put_fd(2, op->name, strlen(op->name));
	gt_put_fd(2, ": ", 2);
	gt_put_fd(2, what, strlen(what));
	gt_put_fd(2, "\n", 1);
}

int
pz_main(const struct pz_ops *o, int argc, char **argv)
{
	static const struct gt_app app = { deadline, event, frame };
	const char *id = NULL, *desc = NULL, *err, *env;
	char *colon, *idcopy = NULL, defseed[48];
	int ai = 1, w, h, status;

	op = o;
	while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
		if (strcmp(argv[ai], "-m") == 0) {
			mono = 1;
		} else if (strcmp(argv[ai], "-s") == 0 && ai + 1 < argc) {
			base_seed = argv[++ai];
		} else {
			return usage();
		}
		ai++;
	}
	if (ai < argc)
		id = argv[ai++];
	if (ai < argc)
		return usage();
	env = getenv("GAMEBOX_TEST");
	testmode = env != NULL;
	if (testmode) {
		base_seed = "gamebox-test";
		if (!id && strchr(env, ':'))
			id = env;
	}
	if (!base_seed) {
		sprintf(defseed, "%lu.%d", (unsigned long)time(NULL), (int)getpid());
		base_seed = defseed;
	}
	if (id) {
		idcopy = dupstr(id);
		colon = strchr(idcopy, ':');
		if (colon) {
			*colon = 0;
			desc = colon + 1;
		}
	}
	params = op->params(idcopy);
	err = op->check(params, &w, &h);
	if (!err && (w > PZ_MAXW || h > PZ_MAXH))
		err = "the board does not fit the console";
	if (err) {
		complain(err);
		return 2;
	}
	if (!start_game(desc, &err)) {
		complain(err);
		return 2;
	}
	sfree(idcopy);
	status = gt_loop(&app);
	return status;
}
