/*
 * fourfall -- a one-player falling-block puzzle for the PicoCalc console
 * of the DiscoBSD RP2040 port (53 columns by 32 rows, ANSI escapes).
 * Original implementation written from a behavioral description; the
 * piece shapes are derived from one cell table by the rotation rule
 * below rather than listed per orientation.
 *
 * usage: fourfall [-cgm] [start-level [seed]]
 *
 *   -c           color: each piece type takes its own foreground hue
 *   -g           plumb line on: dots below the piece in each column
 *   -m           monochrome letters: a cell prints its piece letter twice
 *   start-level  1..9, default 1
 *   seed         random seed, default time(NULL) and getpid()
 *
 * Without -c or -m a cell is a two-character glyph texture of its piece
 * type: bright for the falling piece, dim for settled cells, no hue.
 * With -m and -c together, -m wins.
 *
 * Keys: Left/Right (h, l) move, Up (k, x) rotate clockwise, z rotate
 * counterclockwise, Down (j) soft drop, Space or Enter hard drop, p, F1
 * or Esc pause, g toggle the plumb line, Ctrl-L repaint, q quit.
 *
 * The file has two halves. The rules (board, pieces, rotation with
 * kicks, collision, locking, line clear, scoring, 7-bag generator,
 * gravity and lock timers) perform no I/O and read no clock: time
 * enters through ff_advance() as a millisecond count. Defining
 * FOURFALL_LOGIC compiles that half alone so tests/logic_test.c can
 * include this file. The second half is the terminal front end.
 *
 * The loop waits in select for input, the next gravity or lock
 * deadline, or a stalled escape sequence, charges the elapsed time to
 * the game with ff_tick() before it applies a key, and leaves through
 * term_leave() on q, end of input, a select failure or a signal. On
 * the target the sgtty RAW flag delivers ^C as the byte 3, which no key
 * acts on, so q is the keyboard quit; SIGINT, SIGTERM, SIGHUP and
 * SIGQUIT come from kill(1) or a hangup and end the game the same way.
 *
 * Testing hook: if GAMEBOX_TEST is set, gravity and the lock timer are
 * off and the piece queue is the letters of its value (when it holds
 * only characters from IOTSZJL) repeated cyclically, else IOTSZJL
 * repeated; the seed is ignored.
 */
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Rules                                                              */
/* ------------------------------------------------------------------ */

#define FF_ROWS		24	/* rows 0 and 1 are hidden spawn rows */
#define FF_COLS		12
#define FF_HIDDEN	2
#define FF_LOCK_MS	500
#define FF_LOCK_RESETS	10
#define FF_SCORE_MAX	99999999UL
#define FF_LEVEL_MAX	20
#define FF_QMAX		16

enum { FF_I = 1, FF_O, FF_T, FF_S, FF_Z, FF_J, FF_L };
#define FF_NTYPES	7
#define FF_LETTERS	"IOTSZJL"	/* index + 1 is the type number */

#define FF_CW		1
#define FF_CCW		(-1)

typedef unsigned char ff_board[FF_ROWS][FF_COLS];

struct ff_piece {
	int type;		/* 1..7, or 0 for no piece */
	int rot;		/* 0..3 */
	int row, col;		/* board position of the box's top-left corner */
};

struct ff_game {
	ff_board board;
	struct ff_piece cur;
	int queue[FF_QMAX];	/* upcoming types, queue[0] spawns next */
	int qlen;
	const char *script;	/* cyclic letters, or NULL to draw bags */
	int spos;
	int (*draw)(int);	/* draw(m) returns 0..m-1 */
	int start_level, level, lines;
	unsigned long score;
	int over;
	int locks;		/* pieces locked since ff_new */
	int grav_ms;		/* time since the last gravity step */
	int lock_on;		/* the 500 ms lock timer runs */
	int lock_ms;
	int lock_resets;	/* restarts used by the current piece */
	unsigned long flash_mask; /* bit r set: row r cleared by the last lock */
	ff_board flash_board;	/* the board just before that clear */
};

/*
 * Orientation-0 cells (row, col) inside the piece's square box, listed
 * last row first and right to left within a row. Every rotated
 * orientation inherits this order cell by cell.
 */
static const signed char ff_base[FF_NTYPES + 1][4][2] = {
	{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
	{ { 1, 3 }, { 1, 2 }, { 1, 1 }, { 1, 0 } },	/* I */
	{ { 1, 1 }, { 0, 0 }, { 1, 0 }, { 0, 1 } },	/* O, fixed order */
	{ { 1, 2 }, { 1, 1 }, { 1, 0 }, { 0, 1 } },	/* T */
	{ { 1, 1 }, { 1, 0 }, { 0, 2 }, { 0, 1 } },	/* S */
	{ { 1, 2 }, { 1, 1 }, { 0, 1 }, { 0, 0 } },	/* Z */
	{ { 1, 2 }, { 1, 1 }, { 1, 0 }, { 0, 0 } },	/* J */
	{ { 1, 2 }, { 1, 1 }, { 1, 0 }, { 0, 2 } },	/* L */
};
static const signed char ff_box[FF_NTYPES + 1] = { 0, 4, 2, 3, 3, 3, 3, 3 };

/* Kick offsets (drow, dcol), tried in order after a rotation. */
static const signed char ff_kick_i[7][2] = {
	{ 0, 0 }, { 0, -1 }, { 0, 1 }, { 0, -2 }, { 0, 2 }, { -1, 0 }, { -2, 0 }
};
static const signed char ff_kick_std[4][2] = {
	{ 0, 0 }, { 0, -1 }, { 0, 1 }, { -1, 0 }
};

/* Gravity interval in ms for levels 1..13; level 14 and above is 64. */
static const short ff_grav[13] = {
	800, 700, 610, 530, 450, 380, 320, 260, 210, 170, 135, 105, 84
};

/* Type number of a letter in FF_LETTERS, or 0. */
static int
ff_type_of_letter(int ch)
{
	const char *p;

	if (ch == 0)
		return 0;
	p = strchr(FF_LETTERS, ch);
	return p ? (int)(p - FF_LETTERS) + 1 : 0;
}

/*
 * The four cells of a piece type at orientation rot, as (row, col)
 * inside its box. One clockwise quarter turn maps (r, c) to
 * (c, n - 1 - r); orientation k is orientation 0 turned k times.
 */
static void
ff_cells(int type, int rot, int cell[4][2])
{
	int n = ff_box[type], i, k, r, c, t;

	if (type == FF_O)
		rot = 0;	/* the square maps onto itself: fixed enumeration */
	for (i = 0; i < 4; i++) {
		r = ff_base[type][i][0];
		c = ff_base[type][i][1];
		for (k = 0; k < rot; k++) {
			t = r;
			r = c;
			c = n - 1 - t;
		}
		cell[i][0] = r;
		cell[i][1] = c;
	}
}

/*
 * Spawn state for a type: orientation 0 at the top, with the box
 * centered, col = (FF_COLS - box side) / 2. On the 12-column well the
 * I box (side 4) leaves 4 columns on each side, the O box (side 2) leaves
 * 5, and a 3-wide box leaves 4 on the left and 5 on the right.
 */
static struct ff_piece
ff_spawn_state(int type)
{
	struct ff_piece p;

	p.type = type;
	p.rot = 0;
	p.row = 0;
	p.col = (FF_COLS - ff_box[type]) / 2;
	return p;
}

/* 1 when a cell of the piece is off the board or on a filled cell. */
static int
ff_collides(ff_board b, const struct ff_piece *p)
{
	int cell[4][2], i, r, c;

	ff_cells(p->type, p->rot, cell);
	for (i = 0; i < 4; i++) {
		r = p->row + cell[i][0];
		c = p->col + cell[i][1];
		if (c < 0 || c >= FF_COLS || r < 0 || r >= FF_ROWS || b[r][c])
			return 1;
	}
	return 0;
}

/*
 * Rotate p by one quarter turn (dir FF_CW or FF_CCW), trying the kick
 * offsets in order. Returns 1 and updates *p on success; returns 0 and
 * leaves *p untouched when every offset collides. The O piece turns in
 * place with no collision test.
 */
static int
ff_rotate_piece(ff_board b, struct ff_piece *p, int dir)
{
	struct ff_piece t = *p;
	const signed char (*kick)[2];
	int nk, i;

	t.rot = (p->rot + (dir > 0 ? 1 : 3)) & 3;
	if (p->type == FF_O) {
		p->rot = t.rot;
		return 1;
	}
	if (p->type == FF_I) {
		kick = ff_kick_i;
		nk = 7;
	} else {
		kick = ff_kick_std;
		nk = 4;
	}
	for (i = 0; i < nk; i++) {
		t.row = p->row + kick[i][0];
		t.col = p->col + kick[i][1];
		if (!ff_collides(b, &t)) {
			*p = t;
			return 1;
		}
	}
	return 0;
}

/* Rows the piece can fall before the first collision. */
static int
ff_drop_distance(ff_board b, const struct ff_piece *p)
{
	struct ff_piece t = *p;
	int d;

	for (d = 0; d < FF_ROWS; d++) {
		t.row = p->row + d + 1;
		if (ff_collides(b, &t))
			break;
	}
	return d;
}

/*
 * Plumb line of board column col: the empty cells straight below the
 * piece's lowest cell in that column, down to the first filled cell or
 * the floor. Returns the count and stores the first row in *first. A
 * column the piece does not occupy has count 0.
 */
static int
ff_plumb(ff_board b, const struct ff_piece *p, int col, int *first)
{
	int cell[4][2], i, r, low = -1, n = 0;

	ff_cells(p->type, p->rot, cell);
	for (i = 0; i < 4; i++)
		if (p->col + cell[i][1] == col && p->row + cell[i][0] > low)
			low = p->row + cell[i][0];
	*first = low + 1;
	if (low < 0)
		return 0;
	for (r = low + 1; r < FF_ROWS && !b[r][col]; r++)
		n++;
	return n;
}

/*
 * Write the piece's type into the board. Returns 1 (lock-out) when all
 * four cells lie in the hidden rows.
 */
static int
ff_lock_cells(ff_board b, const struct ff_piece *p)
{
	int cell[4][2], i, r, visible = 0;

	ff_cells(p->type, p->rot, cell);
	for (i = 0; i < 4; i++) {
		r = p->row + cell[i][0];
		b[r][p->col + cell[i][1]] = (unsigned char)p->type;
		if (r >= FF_HIDDEN)
			visible = 1;
	}
	return !visible;
}

static int
ff_rowfull(const unsigned char *row)
{
	int c;

	for (c = 0; c < FF_COLS; c++)
		if (!row[c])
			return 0;
	return 1;
}

/* Bit r of the result is set when board row r is full. */
static unsigned long
ff_fullrows(ff_board b)
{
	unsigned long m = 0;
	int r;

	for (r = 0; r < FF_ROWS; r++)
		if (ff_rowfull(b[r]))
			m |= 1UL << r;
	return m;
}

/*
 * Remove every full row of the FF_ROWS. Rows above a removed row move down
 * by the count removed beneath them; empty rows enter at the top.
 * Returns the count removed.
 */
static int
ff_clear(ff_board b)
{
	int r, w = FF_ROWS - 1, n = 0;

	for (r = FF_ROWS - 1; r >= 0; r--) {
		if (ff_rowfull(b[r])) {
			n++;
			continue;
		}
		if (w != r)
			memcpy(b[w], b[r], FF_COLS);
		w--;
	}
	for (; w >= 0; w--)
		memset(b[w], 0, FF_COLS);
	return n;
}

/* Points for clearing n rows at a level: level * 100 * n(n+1)/2. */
static unsigned long
ff_points(int level, int n)
{
	return (unsigned long)level * 100 * n * (n + 1) / 2;
}

static int
ff_level_of(int start, int lines)
{
	int l = start + lines / 10;

	return l > FF_LEVEL_MAX ? FF_LEVEL_MAX : l;
}

static int
ff_interval(int level)
{
	if (level < 1)
		level = 1;
	return level > 13 ? 64 : ff_grav[level - 1];
}

/*
 * Fisher-Yates over I O T S Z J L: for i = 6 down to 1, swap
 * position i with position draw(i + 1).
 */
static void
ff_bag(int out[7], int (*draw)(int))
{
	int i, j, t;

	for (i = 0; i < 7; i++)
		out[i] = i + 1;
	for (i = 6; i >= 1; i--) {
		j = draw(i + 1);
		t = out[i];
		out[i] = out[j];
		out[j] = t;
	}
}

/*
 * (rand() * m) >> 15 takes the high bits of the 15-bit libc generator;
 * where rand() returns more bits the top 15 are used the same way.
 */
static int
ff_rnd(int m)
{
	long v = rand();

#if RAND_MAX > 0x7fff
	v >>= 16;
	v &= 0x7fff;
#endif
	return (int)((v * m) >> 15);
}

/*
 * Two-character glyph texture of each piece type, index 0 for an empty
 * cell: I "[]", O "##", T "<>", S "//", Z "\\" (two backslashes),
 * J "()", L "{}".
 */
static const char ff_glyph[FF_NTYPES + 1][3] = {
	"  ", "[]", "##", "<>", "//", "\\\\", "()", "{}"
};

#define FF_WELL_COL	3	/* terminal column of board column 0 */
#define FF_PANEL_COL	30	/* left edge of the right-hand panel */
#define FF_NEXT_SLOTS	2	/* upcoming pieces shown */
#define FF_NEXT_ROW	9	/* terminal row of the first preview box */
#define FF_NEXT_PITCH	3	/* rows from one preview box to the next */

/* Map a board cell to its 1-based terminal position; 0 when hidden. */
static int
ff_board_pos(int r, int c, int *trow, int *tcol)
{
	if (r < FF_HIDDEN || r >= FF_ROWS || c < 0 || c >= FF_COLS)
		return 0;
	*trow = r;
	*tcol = FF_WELL_COL + 2 * c;
	return 1;
}

/*
 * Map a cell (r, c) of the preview box of upcoming piece slot (0 is the
 * next piece) to its terminal position. Every orientation-0 piece lies
 * in box rows 0 and 1 and columns 0 to 3, so a box is two rows tall.
 */
static void
ff_next_pos(int slot, int r, int c, int *trow, int *tcol)
{
	*trow = FF_NEXT_ROW + FF_NEXT_PITCH * slot + r;
	*tcol = FF_PANEL_COL + 2 * c;
}

static void
ff_add_score(struct ff_game *g, unsigned long pts)
{
	g->score += pts;
	if (g->score > FF_SCORE_MAX)
		g->score = FF_SCORE_MAX;
}

/* Keep the queue at two or more pieces. */
static void
ff_fill(struct ff_game *g)
{
	int bag[7], i;

	while (g->qlen < 2) {
		if (g->script) {
			if (g->script[g->spos] == 0)
				g->spos = 0;
			g->queue[g->qlen++] =
			    ff_type_of_letter(g->script[g->spos++]);
		} else {
			ff_bag(bag, g->draw);
			for (i = 0; i < 7; i++)
				g->queue[g->qlen++] = bag[i];
		}
	}
}

/*
 * Update the lock timer after the piece changed state. A grounded
 * piece starts the timer; an adjustment (a successful player move or
 * rotation) that leaves a running timer grounded restarts it, up to
 * FF_LOCK_RESETS times per piece. An ungrounded piece stops the timer.
 */
static void
ff_settle(struct ff_game *g, int adjusted)
{
	struct ff_piece t = g->cur;

	t.row++;
	if (!ff_collides(g->board, &t)) {
		g->lock_on = 0;
		return;
	}
	if (!g->lock_on) {
		g->lock_on = 1;
		g->lock_ms = 0;
	} else if (adjusted && g->lock_resets < FF_LOCK_RESETS) {
		g->lock_ms = 0;
		g->lock_resets++;
	}
}

/* Take the next piece from the queue. A colliding spawn ends the game. */
static void
ff_spawn(struct ff_game *g)
{
	int i;

	g->cur = ff_spawn_state(g->queue[0]);
	for (i = 1; i < g->qlen; i++)
		g->queue[i - 1] = g->queue[i];
	g->qlen--;
	ff_fill(g);
	g->grav_ms = 0;
	g->lock_on = 0;
	g->lock_ms = 0;
	g->lock_resets = 0;
	if (ff_collides(g->board, &g->cur)) {
		g->over = 1;
		g->cur.type = 0;
		return;
	}
	ff_settle(g, 0);
}

/* Lock the current piece, clear rows, score, level, spawn the next. */
static void
ff_lock(struct ff_game *g)
{
	unsigned long full;
	int n;

	g->locks++;
	if (ff_lock_cells(g->board, &g->cur)) {
		g->over = 1;
		g->cur.type = 0;
		return;
	}
	g->cur.type = 0;
	full = ff_fullrows(g->board);
	if (full) {
		g->flash_mask = full;
		memcpy(g->flash_board, g->board, sizeof g->board);
	}
	n = ff_clear(g->board);
	ff_add_score(g, ff_points(g->level, n));
	g->lines += n;
	g->level = ff_level_of(g->start_level, g->lines);
	ff_spawn(g);
}

static void
ff_new(struct ff_game *g, int start_level, const char *script,
    int (*draw)(int))
{
	memset(g, 0, sizeof *g);
	g->script = script;
	g->draw = draw;
	g->start_level = start_level;
	g->level = ff_level_of(start_level, 0);
	ff_fill(g);
	ff_spawn(g);
}

/* Shift the piece by dc columns. Returns 1 when it moved. */
static int
ff_move(struct ff_game *g, int dc)
{
	struct ff_piece t = g->cur;

	if (g->over || !t.type)
		return 0;
	t.col += dc;
	if (ff_collides(g->board, &t))
		return 0;
	g->cur = t;
	ff_settle(g, 1);
	return 1;
}

/* Rotate the piece (FF_CW or FF_CCW) with kicks. Returns 1 on success. */
static int
ff_rotate(struct ff_game *g, int dir)
{
	if (g->over || !g->cur.type)
		return 0;
	if (!ff_rotate_piece(g->board, &g->cur, dir))
		return 0;
	ff_settle(g, 1);
	return 1;
}

/* Down: one row for a point, or lock at once when grounded. */
static void
ff_soft_drop(struct ff_game *g)
{
	struct ff_piece t = g->cur;

	if (g->over || !t.type)
		return;
	t.row++;
	if (ff_collides(g->board, &t)) {
		ff_lock(g);
		return;
	}
	g->cur = t;
	ff_add_score(g, 1);
	ff_settle(g, 0);
}

/* Space or Enter: fall as far as possible, two points a row, lock. */
static void
ff_hard_drop(struct ff_game *g)
{
	int d;

	if (g->over || !g->cur.type)
		return;
	d = ff_drop_distance(g->board, &g->cur);
	g->cur.row += d;
	ff_add_score(g, 2UL * d);
	ff_lock(g);
}

/* Milliseconds until the next gravity step or lock expiry. */
static int
ff_deadline(const struct ff_game *g)
{
	int d = ff_interval(g->level) - g->grav_ms;

	if (g->lock_on && FF_LOCK_MS - g->lock_ms < d)
		d = FF_LOCK_MS - g->lock_ms;
	return d < 1 ? 1 : d;
}

/*
 * Advance gravity and the lock timer by ms milliseconds. The loop
 * steps to each deadline in turn, so a long interval gives the same
 * result as the equivalent run of short ones.
 */
static void
ff_advance(struct ff_game *g, int ms)
{
	int step, due;

	while (ms > 0 && !g->over && g->cur.type) {
		step = ms;
		due = ff_interval(g->level) - g->grav_ms;
		if (step > due)
			step = due;
		if (g->lock_on && step > FF_LOCK_MS - g->lock_ms)
			step = FF_LOCK_MS - g->lock_ms;
		if (step < 1)
			step = 1;
		ms -= step;
		g->grav_ms += step;
		if (g->lock_on) {
			g->lock_ms += step;
			if (g->lock_ms >= FF_LOCK_MS) {
				ff_lock(g);
				continue;
			}
		}
		if (g->grav_ms >= ff_interval(g->level)) {
			struct ff_piece t = g->cur;

			g->grav_ms = 0;
			t.row++;
			if (!ff_collides(g->board, &t)) {
				g->cur = t;
				ff_settle(g, 0);
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Loop step                                                          */
/* ------------------------------------------------------------------ */

/*
 * The front end is in one of three modes. The game clock runs in S_PLAY
 * only; a wait that ends in S_PAUSE or S_OVER charges nothing.
 */
enum { S_PLAY, S_PAUSE, S_OVER };

/* Longest span ff_tick charges to the game in one step, in ms. */
#define FF_DT_MAX	1000

/* Key codes the rules act on; the front end maps terminal input onto them. */
#define FF_KEY_LEFT	'h'
#define FF_KEY_DOWN	'j'
#define FF_KEY_UP	'k'
#define FF_KEY_RIGHT	'l'
#define FF_KEY_PLUMB	'g'

/*
 * Apply key k to the game in mode *mode: moves, rotations and drops in
 * S_PLAY, resume in S_PAUSE, restart in S_OVER. Returns 1 when the
 * plumb-line key was accepted in S_PLAY, a toggle the front end owns.
 */
static int
ff_key(struct ff_game *g, int *mode, int k)
{
	if (*mode == S_OVER) {
		if (k == ' ' || k == '\r' || k == '\n') {
			ff_new(g, g->start_level, g->script, g->draw);
			*mode = S_PLAY;
		}
		return 0;
	}
	if (*mode == S_PAUSE) {
		if (k == 'p' || k == ' ' || k == '\r' || k == '\n')
			*mode = S_PLAY;
		return 0;
	}
	switch (k) {
	case FF_KEY_LEFT:
		ff_move(g, -1);
		break;
	case FF_KEY_RIGHT:
		ff_move(g, 1);
		break;
	case FF_KEY_UP:
	case 'x':
		ff_rotate(g, FF_CW);
		break;
	case 'z':
		ff_rotate(g, FF_CCW);
		break;
	case FF_KEY_DOWN:
		ff_soft_drop(g);
		break;
	case ' ':
	case '\r':
	case '\n':
		ff_hard_drop(g);
		break;
	case 'p':
		*mode = S_PAUSE;
		break;
	case FF_KEY_PLUMB:
		return 1;
	}
	if (g->over)
		*mode = S_OVER;
	return 0;
}

/*
 * One pass of the front end's loop after a wait ended at clock reading
 * now (ms). The time since *last is charged to the game first, against
 * the mode the wait ran in, and key (0 for none) is applied after it:
 * a move that restarts the lock timer then leaves the full FF_LOCK_MS,
 * and the key that resumes a pause finds the pause span uncharged.
 * timed_out_ms is the length of a wait that expired without input, or 0;
 * the expiry is trusted over a coarse clock, and a wait that long was
 * real, so it is charged whole. clock_on is 0 in the test
 * mode that freezes gravity and the lock timer. Returns what ff_key
 * returns.
 */
static int
ff_tick(struct ff_game *g, int *mode, int clock_on, unsigned long *last,
    unsigned long now, int timed_out_ms, int key)
{
	unsigned long span = now >= *last ? now - *last : 0;
	int dt = span > (unsigned long)FF_DT_MAX ? FF_DT_MAX : (int)span;

	*last = now;
	if (timed_out_ms > 0 && dt < timed_out_ms)
		dt = timed_out_ms;
	if (*mode == S_PLAY && clock_on) {
		ff_advance(g, dt);
		if (g->over)
			*mode = S_OVER;
	}
	return key ? ff_key(g, mode, key) : 0;
}

#ifndef FOURFALL_LOGIC

/* ------------------------------------------------------------------ */
/* Terminal front end                                                 */
/* ------------------------------------------------------------------ */

#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
/* gametty.h serves several games; this one reads keys itself. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../gametty.h"
#pragma GCC diagnostic pop

_Static_assert(GK_UP == FF_KEY_UP && GK_DOWN == FF_KEY_DOWN &&
    GK_LEFT == FF_KEY_LEFT && GK_RIGHT == FF_KEY_RIGHT,
    "the terminal arrow codes are the rules' key codes");

#define OBUF		6144
#define PANEL_COL	FF_PANEL_COL
#define VAL_COL		(PANEL_COL + 7)
#define WELL_W		(2 * FF_COLS)	/* terminal columns inside the walls */
#define WALL_L		(FF_WELL_COL - 1)
#define WALL_R		(FF_WELL_COL + WELL_W)
#define FLOOR_ROW	FF_ROWS
#define STATUS_ROW	(FF_ROWS + 2)
#define VIEW_ROWS	(FF_ROWS - FF_HIDDEN)
#define UNKNOWN		0xff	/* shadow value that matches no view cell */
#define FLASH_MS	150	/* cleared rows show "~~" this long */

/*
 * View codes: 0 empty, 1..7 a settled cell of that type, VC_FALL + t the
 * falling piece of type t, VC_PLUMB a plumb-line dot, VC_FLASH a cell of
 * a row being cleared.
 */
#define VC_FALL		8
#define VC_PLUMB	16
#define VC_FLASH	17

/* Background color of each piece type (I white, O blue, T green,
 * S magenta, Z cyan, J red, L yellow); type 0 is empty. -c prints the
 * glyph in the matching foreground color (30 plus the last digit). */
static const unsigned char ff_color[FF_NTYPES + 1] = {
	40, 47, 44, 42, 45, 46, 41, 43
};

static struct ff_game game;
static int state = S_PLAY;
static int testmode;
static int mono;
static int color;
static int plumb_on;
static const char *const help[7] = {
	"Left/Right  move",
	"Up/x   rotate cw",
	"z      rotate ccw",
	"Down   soft drop",
	"Space  hard drop",
	"p/Esc  pause  g plumb",
	"q  quit  ^L repaint",
};

static char obuf[OBUF];
static int olen;

/* What the terminal shows: board cells, panel numbers, preview, state. */
static unsigned char shadow[VIEW_ROWS][FF_COLS];
static long p_score, p_lines, p_level;
static int p_next[FF_NEXT_SLOTS];
static int drawn;		/* state last drawn, or -1 */
static int drawn_locks;		/* lock count of the status line */
static unsigned char view[VIEW_ROWS][FF_COLS];

static void
ob_flush(void)
{
	if (olen > 0)
		(void)write(1, obuf, (size_t)olen);
	olen = 0;
}

static void
ob_put(const char *s, int n)
{
	if (olen + n > OBUF)
		ob_flush();
	memcpy(obuf + olen, s, (size_t)n);
	olen += n;
}

static void
ob_str(const char *s)
{
	ob_put(s, (int)strlen(s));
}

static void
ob_num(unsigned long v)
{
	char t[12];
	int n = 0;

	do {
		t[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n > 0)
		ob_put(&t[--n], 1);
}

/* Left-aligned in a field of w columns, padded with spaces. */
static void
ob_field(unsigned long v, int w)
{
	unsigned long t;
	int n = 1;

	for (t = v; t >= 10; t /= 10)
		n++;
	ob_num(v);
	for (; n < w; n++)
		ob_put(" ", 1);
}

static void
ob_goto(int row, int col)
{
	ob_str("\033[");
	ob_num((unsigned long)row);
	ob_put(";", 1);
	ob_num((unsigned long)col);
	ob_put("H", 1);
}

/*
 * The one place a cell is drawn; code is a view code (see VC_FALL).
 * Default: the type's glyph, bold white while falling and bright black
 * once settled. -c: the glyph in the type's foreground color, bold while
 * falling. -m: the type letter twice, no attributes.
 */
static void
ob_cell(int code)
{
	char lc[2];
	int type = code > VC_FALL ? code - VC_FALL : code;
	int falling = code > VC_FALL && code < VC_PLUMB;

	if (code == 0) {
		ob_put("  ", 2);
	} else if (code == VC_PLUMB) {
		if (mono)
			ob_put(". ", 2);
		else
			ob_str("\033[90m. \033[0m");
	} else if (code == VC_FLASH) {
		if (mono)
			ob_put("~~", 2);
		else
			ob_str("\033[1m~~\033[0m");
	} else if (mono) {
		lc[0] = lc[1] = FF_LETTERS[type - 1];
		ob_put(lc, 2);
	} else if (color) {
		ob_str(falling ? "\033[1;3" : "\033[3");
		ob_num(ff_color[type] % 10);
		ob_put("m", 1);
		ob_str(ff_glyph[type]);
		ob_str("\033[0m");
	} else {
		ob_str(falling ? "\033[1;37m" : "\033[90m");
		ob_str(ff_glyph[type]);
		ob_str("\033[0m");
	}
}

static void
ob_center(int row, const char *s)
{
	ob_goto(row, FF_WELL_COL + (WELL_W - (int)strlen(s)) / 2);
	ob_str(s);
}

/* Compose the board, the falling piece and the plumb line into view[][]. */
static void
compose(void)
{
	struct ff_piece p;
	int cell[4][2], i, r, c, n, first;

	for (r = FF_HIDDEN; r < FF_ROWS; r++)
		memcpy(view[r - FF_HIDDEN], game.board[r], FF_COLS);
	if (!game.cur.type)
		return;
	p = game.cur;
	ff_cells(p.type, p.rot, cell);
	for (i = 0; i < 4; i++) {
		r = p.row + cell[i][0];
		c = p.col + cell[i][1];
		if (r >= FF_HIDDEN)
			view[r - FF_HIDDEN][c] = (unsigned char)(VC_FALL + p.type);
	}
	if (!plumb_on)
		return;
	for (i = 0; i < 4; i++) {
		c = p.col + cell[i][1];
		n = ff_plumb(game.board, &p, c, &first);
		for (r = first; n > 0; r++, n--)
			if (r >= FF_HIDDEN && !view[r - FF_HIDDEN][c])
				view[r - FF_HIDDEN][c] = VC_PLUMB;
	}
}

/* Write the cells of view[][] that differ from the shadow. */
static void
draw_cells(void)
{
	int r, c, tr, tc, nr = -1, nc = -1;

	for (r = FF_HIDDEN; r < FF_ROWS; r++)
		for (c = 0; c < FF_COLS; c++) {
			if (shadow[r - FF_HIDDEN][c] == view[r - FF_HIDDEN][c])
				continue;
			(void)ff_board_pos(r, c, &tr, &tc);
			if (tr != nr || tc != nc)
				ob_goto(tr, tc);
			ob_cell(view[r - FF_HIDDEN][c]);
			shadow[r - FF_HIDDEN][c] = view[r - FF_HIDDEN][c];
			nr = tr;
			nc = tc + 2;
		}
}

static void
draw_next(int slot, int type)
{
	int cell[4][2], box[2][4], i, r, c, tr, tc;

	memset(box, 0, sizeof box);
	if (type) {
		ff_cells(type, 0, cell);
		for (i = 0; i < 4; i++)
			box[cell[i][0]][cell[i][1]] = type;
	}
	for (r = 0; r < 2; r++) {
		ff_next_pos(slot, r, 0, &tr, &tc);
		ob_goto(tr, tc);
		for (c = 0; c < 4; c++)
			ob_cell(box[r][c]);
	}
	p_next[slot] = type;
}

static void
draw_panel(void)
{
	int slot;

	if (p_score != (long)game.score) {
		ob_goto(2, VAL_COL);
		ob_field(game.score, 8);
		p_score = (long)game.score;
	}
	if (p_level != game.level) {
		ob_goto(3, VAL_COL);
		ob_field((unsigned long)game.level, 8);
		p_level = game.level;
	}
	if (p_lines != game.lines) {
		ob_goto(4, VAL_COL);
		ob_field((unsigned long)game.lines, 8);
		p_lines = game.lines;
	}
	for (slot = 0; slot < FF_NEXT_SLOTS; slot++)
		if (p_next[slot] != game.queue[slot])
			draw_next(slot, game.queue[slot]);
}

static void
draw_status(void)
{
	ob_goto(STATUS_ROW, 1);
	ob_str("score ");
	ob_num(game.score);
	ob_str(" lines ");
	ob_num((unsigned long)game.lines);
	ob_str(" level ");
	ob_num((unsigned long)game.level);
	ob_str("\033[K");
	drawn_locks = game.locks;
}

/* The fixed parts of the screen: frame, title, labels, key help. */
static void
draw_static(void)
{
	int r;

	ob_str("\033[0m\033[2J");
	ob_goto(1, FF_WELL_COL + (WELL_W - 8) / 2);
	ob_str("FOURFALL");
	for (r = FF_HIDDEN; r < FF_ROWS; r++) {
		ob_goto(r, WALL_L);
		ob_put("!", 1);
		ob_goto(r, WALL_R);
		ob_put("!", 1);
	}
	ob_goto(FLOOR_ROW, WALL_L);
	for (r = WALL_L; r <= WALL_R; r++)
		ob_put("=", 1);
	ob_goto(2, PANEL_COL);
	ob_str("SCORE");
	ob_goto(3, PANEL_COL);
	ob_str("LEVEL");
	ob_goto(4, PANEL_COL);
	ob_str("LINES");
	ob_goto(7, PANEL_COL);
	ob_str("NEXT");
	for (r = 0; r < 7; r++) {
		ob_goto(16 + r, PANEL_COL);
		ob_str(help[r]);
	}
}

static void
blank_board(void)
{
	int r, c;

	for (r = FF_HIDDEN; r < FF_ROWS; r++) {
		ob_goto(r, FF_WELL_COL);
		for (c = 0; c < WELL_W; c++)
			ob_put(" ", 1);
	}
}

/*
 * Show the rows the last lock cleared as "~~" for FLASH_MS, drawn over
 * the board as it stood before the collapse. The rules half has already
 * collapsed the board; this is presentation only. Input stays queued in
 * the tty during the wait.
 */
static void
flash_cleared(void)
{
	int r, c;

	for (r = FF_HIDDEN; r < FF_ROWS; r++)
		for (c = 0; c < FF_COLS; c++)
			view[r - FF_HIDDEN][c] = (game.flash_mask >> r) & 1UL ?
			    VC_FLASH : game.flash_board[r][c];
	draw_cells();
	ob_goto(31, 1);
	ob_flush();
	usleep(FLASH_MS * 1000L);
	game.flash_mask = 0;
}

/*
 * Bring the terminal in line with the game state, in one write (two
 * when a cleared row flashes). Returns 1 when the call waited for a
 * flash, so the caller can leave that wait out of the game clock.
 */
static int
frame(int full)
{
	int flashed = 0;

	if (full) {
		draw_static();
		drawn = -1;
		drawn_locks = -1;
		p_score = p_lines = p_level = -1;
		p_next[0] = p_next[1] = -1;
		memset(shadow, UNKNOWN, sizeof shadow);
	}
	if (state == S_PAUSE) {
		if (drawn != S_PAUSE) {
			blank_board();
			draw_next(0, 0);
			draw_next(1, 0);
			ob_center(FF_ROWS / 2 + 1, "PAUSED");
		}
	} else {
		if (state == S_PLAY && drawn != S_PLAY) {
			memset(shadow, UNKNOWN, sizeof shadow);
			p_next[0] = p_next[1] = -1;
		}
		if (game.flash_mask) {
			flash_cleared();
			flashed = 1;
		}
		if (state == S_PLAY || drawn != S_OVER) {
			compose();
			draw_cells();
			draw_panel();
		}
		if (state == S_OVER && drawn != S_OVER) {
			ob_center(FF_ROWS / 2, "GAME OVER");
			ob_center(FF_ROWS / 2 + 2, "Enter  play again");
			ob_center(FF_ROWS / 2 + 3, "q  quit");
			drawn_locks = -1;
		}
	}
	if (drawn_locks != game.locks)
		draw_status();
	drawn = state;
	ob_goto(31, 1);
	ob_flush();
	return flashed;
}

static unsigned long
now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, 0);
	return (unsigned long)tv.tv_sec * 1000UL + (unsigned long)tv.tv_usec / 1000UL;
}

/*
 * The longest wait when nothing else bounds it (a pause, a game over).
 * A signal that lands between the loop's flag test and select cannot
 * interrupt that select; the bound makes the loop see the flag within
 * this many ms. Without pselect in the target libc the window cannot
 * be closed atomically.
 */
#define SIGNAL_POLL_MS	500

/* Wait for input for ms milliseconds, or without limit when ms < 0. */
static int
wait_input(int ms)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(0, &fds);
	if (ms < 0)
		return select(1, &fds, 0, 0, 0);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000L;
	return select(1, &fds, 0, 0, &tv);
}

static int
pending(void)
{
	long avail = 0;

	return ioctl(0, FIONREAD, &avail) == 0 && avail > 0;
}

#define K_NONE	0	/* nothing to act on */
#define K_MORE	(-2)	/* an escape sequence is incomplete */

/*
 * The longest silence inside an escape sequence, in ms. A terminal
 * emits a sequence back to back: the slowest link the console meets,
 * 1200 baud, spaces bytes 8.3 ms apart, and a USB packet carries the
 * whole sequence at once. A human pressing Esc and then another key
 * takes well over 100 ms. 40 ms sits between the two; it is also the
 * delay before a bare Esc pauses.
 */
#define ESC_MS		40

/* Longest CSI parameter and intermediate run read before giving up. */
#define CSI_MAX		8

enum { E_IDLE, E_ESC, E_SS3, E_CSI };

static int esc_state = E_IDLE;
static int esc_num;		/* the first numeric parameter of a CSI */
static int esc_len;		/* bytes consumed inside the CSI */
static unsigned long esc_at;	/* clock reading at the last sequence byte */

/*
 * Feed one input byte to the escape parser. Returns the key (arrows
 * come back as GK_UP and its siblings; F1 as ESC O P or ESC [ 11 ~, and
 * a bare Esc, come back as p), K_NONE for a byte or a finished sequence
 * with no meaning, or K_MORE while a sequence is incomplete. The parser
 * never reads; the caller supplies bytes as they arrive and ends a
 * stalled sequence with esc_abandon().
 */
static int
esc_feed(int c)
{
	switch (esc_state) {
	case E_IDLE:
		if (c != 033)
			return c;
		esc_state = E_ESC;
		return K_MORE;
	case E_ESC:
		if (c == 'O') {
			esc_state = E_SS3;
			return K_MORE;
		}
		if (c == '[') {
			esc_state = E_CSI;
			esc_num = 0;
			esc_len = 0;
			return K_MORE;
		}
		esc_state = E_IDLE;
		return K_NONE;
	case E_SS3:
		esc_state = E_IDLE;
		return c == 'P' ? 'p' : K_NONE;
	default:
		break;
	}
	if (c >= '0' && c <= '9') {
		esc_num = esc_num * 10 + (c - '0');
	} else if (c != ';') {
		switch (c) {
		case 'A': esc_state = E_IDLE; return GK_UP;
		case 'B': esc_state = E_IDLE; return GK_DOWN;
		case 'C': esc_state = E_IDLE; return GK_RIGHT;
		case 'D': esc_state = E_IDLE; return GK_LEFT;
		case '~':
			esc_state = E_IDLE;
			return esc_num == 11 ? 'p' : K_NONE;
		default:
			if (c >= 0x40 && c <= 0x7e) {
				esc_state = E_IDLE;
				return K_NONE;
			}
		}
	}
	if (++esc_len >= CSI_MAX) {
		esc_state = E_IDLE;
		return K_NONE;
	}
	return K_MORE;
}

/*
 * End a sequence that stalled for ESC_MS. A lone Esc is a key press
 * and pauses; a longer fragment is dropped.
 */
static int
esc_abandon(void)
{
	int k = esc_state == E_ESC ? 'p' : K_NONE;

	esc_state = E_IDLE;
	return k;
}

/*
 * Milliseconds left before the pending sequence stalls, at clock
 * reading now.
 */
static int
esc_left(unsigned long now)
{
	unsigned long gone = now >= esc_at ? now - esc_at : 0;

	return gone >= ESC_MS ? 0 : ESC_MS - (int)gone;
}

static void
new_game(void)
{
	ff_new(&game, game.start_level, game.script, ff_rnd);
	state = S_PLAY;
}

/*
 * Terminal ownership. raw_on is 1 while the tty is in the game's raw
 * mode. Every way out of the loop ends in term_leave(), which restores
 * the tty, resets the character attributes and clears the screen with
 * the cursor home, whether the player quit, the input ended, select
 * failed or a signal asked for the exit.
 *
 * SIGINT, SIGTERM, SIGHUP and SIGQUIT handlers only record the signal
 * number in got_sig; the loop leaves after select returns EINTR, and
 * term_leave() runs in ordinary context. On the target the sgtty RAW
 * flag delivers ^C as the byte 3, which no key acts on, so q is the
 * keyboard quit and these signals come from kill(1) or a hangup. On a
 * host termios keeps ISIG on, so ^C and ^\ raise them.
 *
 * SIGTSTP is the one handler that acts: the stop happens inside the
 * kill() in on_stop(), so the tty must be restored before it, from the
 * handler. tcsetattr is async-signal-safe (POSIX XSH 2.4.3) and the
 * target's ioctl is a direct trap. The handler marks raw_on clear; the
 * loop re-enters raw mode and repaints when it next runs, which is
 * after SIGCONT. A SIGCONT alone (after SIGSTOP, say) sets got_cont and
 * only repaints.
 */
static volatile sig_atomic_t got_sig;
static volatile sig_atomic_t got_cont;
static volatile sig_atomic_t raw_on;

static const char leave_seq[] = "\033[0m\033[2J\033[H";
static const char park_seq[] = "\033[0m\033[31;1H\r\n";

static void
on_exit_signal(int sig)
{
	if (!got_sig)
		got_sig = sig;
}

static void
on_cont(int sig)
{
	(void)sig;
	got_cont = 1;
}

static void
on_stop(int sig)
{
	sigset_t set;

	(void)sig;
	if (raw_on) {
		gtty_restore();
		raw_on = 0;
	}
	(void)write(1, park_seq, sizeof park_seq - 1);
	(void)signal(SIGTSTP, SIG_DFL);
	(void)sigemptyset(&set);
	(void)sigaddset(&set, SIGTSTP);
	(void)sigprocmask(SIG_UNBLOCK, &set, 0);
	(void)kill(getpid(), SIGTSTP);
	(void)signal(SIGTSTP, on_stop);
}

/* Enter raw mode with the handled signals held, so a stop cannot see a
 * half-set tty and raw_on never trails the mode it describes. */
static void
term_enter(void)
{
	sigset_t all, old;

	(void)sigemptyset(&all);
	(void)sigaddset(&all, SIGINT);
	(void)sigaddset(&all, SIGTERM);
	(void)sigaddset(&all, SIGHUP);
	(void)sigaddset(&all, SIGQUIT);
	(void)sigaddset(&all, SIGTSTP);
	(void)sigprocmask(SIG_BLOCK, &all, &old);
	gtty_raw();
	raw_on = 1;
	(void)sigprocmask(SIG_SETMASK, &old, 0);
}

/* The single exit path of the terminal. */
static void
term_leave(void)
{
	if (raw_on) {
		gtty_restore();
		raw_on = 0;
	}
	(void)write(1, leave_seq, sizeof leave_seq - 1);
}

static int
script_ok(const char *s)
{
	if (*s == 0)
		return 0;
	for (; *s; s++)
		if (!ff_type_of_letter(*s))
			return 0;
	return 1;
}

static int
usage(void)
{
	static const char msg[] =
	    "usage: fourfall [-cgm] [start-level [seed]]\n";

	(void)write(2, msg, sizeof msg - 1);
	return 2;
}

int
main(int argc, char **argv)
{
	const char *env;
	unsigned long last, now;
	unsigned seed;
	unsigned char c = 0;
	int level = 1, k, r, tmo, left, idle, status = 0, ai = 1;
	ssize_t nread;
	const char *o, *err = 0;

	for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++)
		for (o = argv[ai] + 1; *o; o++) {
			if (*o == 'c')
				color = 1;
			else if (*o == 'g')
				plumb_on = 1;
			else if (*o == 'm')
				mono = 1;
			else
				return usage();
		}
	if (ai < argc) {
		level = atoi(argv[ai++]);
		if (level < 1)
			level = 1;
		if (level > 9)
			level = 9;
	}
	seed = (unsigned)time(0) * 31U + (unsigned)getpid();
	if (ai < argc)
		seed = (unsigned)atoi(argv[ai++]);
	if (ai < argc) {
		return usage();
	}
	srand(seed);

	env = getenv("GAMEBOX_TEST");
	testmode = env != 0;
	game.start_level = level;
	game.script = 0;
	if (testmode)
		game.script = script_ok(env) ? env : FF_LETTERS;
	new_game();

	signal(SIGINT, on_exit_signal);
	signal(SIGTERM, on_exit_signal);
	signal(SIGHUP, on_exit_signal);
	signal(SIGQUIT, on_exit_signal);
	signal(SIGTSTP, on_stop);
	signal(SIGCONT, on_cont);
	term_enter();
	last = now_ms();
	frame(1);
	for (;;) {
		if (got_sig)
			break;
		if (!raw_on) {
			term_enter();	/* resumed after SIGTSTP */
			got_cont = 1;
		}
		if (got_cont) {
			got_cont = 0;
			frame(1);
			last = now_ms();
		}
		tmo = -1;
		if (state == S_PLAY && !testmode)
			tmo = ff_deadline(&game);
		if (esc_state != E_IDLE) {
			left = esc_left(now_ms());
			if (tmo < 0 || left < tmo)
				tmo = left;
		}
		idle = tmo < 0;
		if (idle)
			tmo = SIGNAL_POLL_MS;
		r = wait_input(tmo);
		if (got_sig)
			break;
		if (r < 0) {
			if (errno == EINTR)
				continue;
			err = "fourfall: select failed\n";
			status = 1;
			break;
		}
		now = now_ms();
		k = K_NONE;
		if (r > 0) {
			do {
				nread = read(0, &c, 1);
				if (nread != 1)
					break;
				k = esc_feed(c);
				if (k == K_MORE)
					esc_at = now;
			} while (k == K_MORE && pending());
			if (nread == 0 || (nread < 0 && errno != EINTR))
				break;
			if (nread < 0 || k == K_MORE)
				k = K_NONE;
		} else if (esc_state != E_IDLE && esc_left(now) == 0) {
			k = esc_abandon();
		}
		if (r == 0 && idle && k == K_NONE)
			continue;
		if (k == 'q')
			break;
		if (k == 014) {
			frame(1);
			k = K_NONE;
		}
		if (ff_tick(&game, &state, !testmode, &last, now,
		    r == 0 ? tmo : 0, k))
			plumb_on = !plumb_on;
		if (frame(0))
			last = now_ms();
	}
	term_leave();
	if (err)
		(void)write(2, err, strlen(err));
	if (got_sig)
		return 128 + (int)got_sig;
	return status;
}

#endif /* !FOURFALL_LOGIC */
