/*
 * bubble -- a small color-matching game for the DiscoBSD RP2040 port,
 * puzzle-bobble in feel: bubbles hang from the ceiling and the shooter
 * fires up from the floor. Written from the rules of the genre alone; no
 * code of another implementation was consulted.
 *
 * Square mode (the default). Each of COLS columns holds a pile that hangs
 * from the ceiling (row 0) and grows downward. A shot attaches beneath the
 * lowest bubble in its column; a 4-way-connected same-color group of 3 or
 * more pops, and every bubble left unsupported by the pop -- no longer
 * joined to the ceiling row -- then falls. The board starts with INITROWS
 * rows of random color at the ceiling; it is won by clearing the board and
 * lost when a column grows down to the floor and touches the shooter.
 *
 * Hexagonal mode (-x). Rows are staggered: a shifted row sits half a cell
 * (one terminal column) to the right of an unshifted one and holds COLS - 1
 * bubbles, so every bubble touches six others (two in its row, two in the
 * row above, two in the row below). Matching, anchoring and the shot's
 * resting cell use that adjacency. Every DESCENT shots that pop nothing the
 * whole pile moves down one row and a new full row appears at the ceiling;
 * the stagger parity flips with it so each bubble keeps its horizontal
 * position. The game is lost when a bubble reaches the bottom row.
 *
 * The console is ANSI/VT100 over USB CDC-ACM or the PicoCalc panel. Color
 * is SGR: each color carries both a distinct hue and a distinct glyph, so
 * the board reads on a monochrome terminal as well, and a top-to-bottom
 * bold/normal/dim gradient gives the pile depth. The frame is 18 rows by at
 * most 42 columns, inside both an 80x24 terminal and a 53x32 one.
 *
 * usage: bubble [-x] [seed]
 *
 * left/right aim, space fires, q quits.
 *
 * Testing hook: if GAMEBOX_TEST is set, the board starts empty and the
 * shot color holds for three shots before advancing (1,1,1,2,2,2,...)
 * regardless of seed, so a host test can fire three shots into one empty
 * column and see them pop without depending on any particular rand()
 * implementation. A value of two or more digits from 1 to 4 replaces the
 * cycle with those colors, repeated, and a descent row takes the colors
 * 1,2,3,4 repeated across it.
 *
 * The file has two halves. The rules (grid, adjacency, matching,
 * anchoring, the shot path, the descent) perform no I/O. Defining
 * BUBBLE_LOGIC compiles that half alone so tests/hex_test.c can include
 * this file. The second half is the terminal front end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define COLS		8
#define ROWS		10
#define INITROWS	3
#define NCOLORS		4
#define DESCENT		8

/* The shot travels from a launcher at the bottom-center up along a line
 * that reflects off the side walls, integer-only -- the M0+ has no FPU
 * and a float in a game binary would drag in ~10 KB of soft-float. SUB
 * fine units span one grid cell on each axis; the walk (see trajectory)
 * is a DDA that advances one fine unit at a time along whichever axis
 * lags the ideal line, so it enters every grid cell the line crosses and
 * cannot tunnel through a diagonally-adjacent bubble. Aim is an angle:
 * 0 straight up, sign gives left/right, magnitude indexes a slope table
 * of run:rise (sideways:upward) fine-unit ratios from steep to shallow. */
#define SUB		16
#define FW		(COLS * SUB)	/* fine board width  */
#define FH		(ROWS * SUB)	/* fine board height */
#define AIMMAX		6
static const int slope_run[AIMMAX + 1]  = { 0, 1, 1, 2, 1, 3, 2 };
static const int slope_rise[AIMMAX + 1] = { 1, 3, 2, 3, 1, 2, 1 };

static int grid[ROWS * COLS];	/* 0 empty, else color 1..NCOLORS */
static int pathmark[ROWS * COLS];	/* cells on the current aim preview */
static int pathseq[ROWS * COLS];	/* those cells in launch order */
static int pathlen;
static int aim;			/* shot angle: 0 up, -AIMMAX..AIMMAX */
static int nextcolor;
static long score;
static int testmode;
static const char *script;	/* test shot colors, or NULL for the cycle */
static int scriptpos;
static int cyclecolor = 1;
static int cyclecount;
static int hexmode;
static int stagger;		/* hex: row r is shifted when (r + stagger) is odd */
static int misses;		/* hex: shots since the last pop */

/* 1 when row r is shifted half a cell right (hexagonal mode only). */
static int
shifted(int r)
{
	return hexmode && ((r + stagger) & 1);
}

/* Bubbles in row r: a shifted row gives up its last cell. */
static int
rowlen(int r)
{
	return shifted(r) ? COLS - 1 : COLS;
}

/*
 * Cells adjacent to cell, written to out (room for 6); returns the count.
 * Square mode: the four orthogonal neighbors. Hexagonal mode: the row
 * neighbors plus the two cells of each adjacent row whose span overlaps
 * this cell's. A cell of a shifted row spans terminal columns 2c+1..2c+2
 * and a cell of an unshifted row 2c..2c+1, so a shifted cell c touches
 * columns c and c + 1 of the rows beside it, an unshifted cell c touches
 * columns c - 1 and c.
 */
static int
neighbors(int cell, int *out)
{
	int r = cell / COLS, c = cell % COLS, n = 0, dr, rr, c0, c1, k;

	if (c > 0)
		out[n++] = cell - 1;
	if (c < rowlen(r) - 1)
		out[n++] = cell + 1;
	for (dr = -1; dr <= 1; dr += 2) {
		rr = r + dr;
		if (rr < 0 || rr >= ROWS)
			continue;
		if (!hexmode) {
			out[n++] = rr * COLS + c;
			continue;
		}
		c0 = shifted(r) ? c : c - 1;
		c1 = c0 + 1;
		for (k = c0; k <= c1; k++)
			if (k >= 0 && k < rowlen(rr))
				out[n++] = rr * COLS + k;
	}
	return n;
}

/* A GAMEBOX_TEST value of two or more digits 1..NCOLORS is a color script. */
static int
script_ok(const char *s)
{
	size_t n = 0;

	for (; *s; s++, n++)
		if (*s < '1' || *s > '0' + NCOLORS)
			return 0;
	return n >= 2;
}

static int
pickcolor(void)
{
	if (script) {
		int c = script[scriptpos++] - '0';

		if (script[scriptpos] == 0)
			scriptpos = 0;
		return c;
	}
	if (testmode) {
		int c = cyclecolor;
		cyclecount++;
		if (cyclecount == 3) {
			cyclecount = 0;
			cyclecolor = cyclecolor % NCOLORS + 1;
		}
		return c;
	}
	return rand() % NCOLORS + 1;
}

static void
initboard(void)
{
	int r, c;

	for (r = 0; r < ROWS; r++)
		for (c = 0; c < COLS; c++)
			grid[r * COLS + c] = 0;
	if (testmode)
		return;
	if (hexmode) {
		for (r = 0; r < INITROWS; r++)
			for (c = 0; c < rowlen(r); c++)
				grid[r * COLS + c] = rand() % NCOLORS + 1;
		return;
	}
	/* Fill the interior columns only; the two outer columns start open
	 * so a shot bounced off a side wall can tuck under the block's edge
	 * from the first move, where a straight shot cannot reach. */
	for (r = 0; r < INITROWS; r++)
		for (c = 1; c < COLS - 1; c++)
			grid[r * COLS + c] = rand() % NCOLORS + 1;
}

/* After a pop, any bubble no longer joined to the ceiling row falls
 * away. Flood from every occupied cell in row 0 through occupied
 * neighbors; occupied cells left unvisited are unsupported and are
 * removed. Returns the count dropped. */
static int mark2[ROWS * COLS];

static int
anchor_and_drop(void)
{
	int stk[ROWS * COLS];
	int nb[6];
	int sp, i, c, cur, k, n, dropped;

	for (i = 0; i < ROWS * COLS; i++)
		mark2[i] = 0;
	sp = 0;
	for (c = 0; c < COLS; c++)
		if (grid[c] != 0 && !mark2[c]) {
			mark2[c] = 1;
			stk[sp++] = c;
		}
	while (sp > 0) {
		cur = stk[--sp];
		n = neighbors(cur, nb);
		for (k = 0; k < n; k++)
			if (grid[nb[k]] && !mark2[nb[k]]) {
				mark2[nb[k]] = 1;
				stk[sp++] = nb[k];
			}
	}
	dropped = 0;
	for (i = 0; i < ROWS * COLS; i++)
		if (grid[i] != 0 && !mark2[i]) {
			grid[i] = 0;
			dropped++;
		}
	return dropped;
}

static int mark[ROWS * COLS];
static int group[ROWS * COLS];

/* The same-color group containing start, in group[]; returns its size. */
static int
floodfill(int start)
{
	int stk[ROWS * COLS];
	int nb[6];
	int sp, n, cur, k, nn, color, i;

	for (i = 0; i < ROWS * COLS; i++)
		mark[i] = 0;
	color = grid[start];
	sp = 0;
	n = 0;
	stk[sp++] = start;
	mark[start] = 1;
	while (sp > 0) {
		cur = stk[--sp];
		group[n++] = cur;
		nn = neighbors(cur, nb);
		for (k = 0; k < nn; k++)
			if (!mark[nb[k]] && grid[nb[k]] == color) {
				mark[nb[k]] = 1;
				stk[sp++] = nb[k];
			}
	}
	return n;
}

/* The grid cell holding fine position (xf, yf). A shifted row's cells
 * begin half a cell in; a position over the margin beside it belongs to
 * the nearest cell. */
static int
cellat(int xf, int yf)
{
	int r = yf / SUB, c, n;

	c = (xf - (shifted(r) ? SUB / 2 : 0)) / SUB;
	n = rowlen(r);
	if (c >= n)
		c = n - 1;
	return r * COLS + c;
}

/* Walk the shot's path from the launcher (bottom-center) upward and
 * return the grid cell where it attaches, or -1 if the launch cell is
 * already occupied (the pile has reached the floor: game over). A DDA
 * advances one fine unit at a time along whichever axis lags the ideal
 * line given by slope_run:slope_rise for the current aim, reflecting the
 * sideways direction at the walls (edges of the board, 0 and FW). The
 * ball stops one cell short of the first occupied cell it would enter,
 * or at row 0 when it reaches the ceiling; the attach cell is the last
 * empty cell it occupied, always adjacent to the collision because
 * consecutive cells in the walk differ by one grid step. Records the
 * path in pathmark[]/pathseq[] for the aim preview; reads grid, never
 * writes it. */
static int
trajectory(void)
{
	int a, sx, run, rise, xf, yf, ax, ay, cell, last, i;

	for (i = 0; i < ROWS * COLS; i++)
		pathmark[i] = 0;
	pathlen = 0;

	a = aim < 0 ? -aim : aim;
	sx = aim < 0 ? -1 : 1;
	run = slope_run[a];
	rise = slope_rise[a];

	xf = FW / 2;
	yf = FH - 1;
	cell = cellat(xf, yf);
	if (grid[cell])
		return -1;
	last = cell;
	pathmark[cell] = 1;
	pathseq[pathlen++] = cell;

	ax = ay = 0;
	for (;;) {
		/* Step the axis that lags the ideal line ax/run == ay/rise;
		 * run == 0 (straight up) never steps sideways. */
		if (run != 0 && ax * rise <= ay * run) {
			xf += sx;
			ax++;
			if (xf < 0) {
				xf = -xf;
				sx = -sx;
			} else if (xf >= FW) {
				xf = 2 * FW - 2 - xf;
				sx = -sx;
			}
		} else {
			yf--;
			ay++;
			if (yf < 0)
				return last;	/* reached the ceiling */
		}
		cell = cellat(xf, yf);
		if (cell == last)
			continue;		/* still inside the same cell */
		last = cell;
		if (grid[cell])
			return pathseq[pathlen - 1];	/* attach next to it */
		pathmark[cell] = 1;
		pathseq[pathlen++] = cell;
	}
}

/* The color of cell c of a new ceiling row. */
static int
rowcolor(int c)
{
	return testmode ? c % NCOLORS + 1 : rand() % NCOLORS + 1;
}

/* Move the whole pile down one row and fill a new ceiling row. Flipping
 * the stagger parity with the shift leaves every bubble in its terminal
 * columns. The bottom row is empty when the descent runs, because
 * floor_reached() ends the game as soon as a bubble rests there. */
static void
descend(void)
{
	int r, c;

	for (r = ROWS - 1; r > 0; r--)
		for (c = 0; c < COLS; c++)
			grid[r * COLS + c] = grid[(r - 1) * COLS + c];
	stagger ^= 1;
	for (c = 0; c < COLS; c++)
		grid[c] = c < rowlen(0) ? rowcolor(c) : 0;
}

/* 1 when any bubble rests in the last row (hexagonal mode's floor). */
static int
floor_reached(void)
{
	int c;

	for (c = 0; c < COLS; c++)
		if (grid[(ROWS - 1) * COLS + c])
			return 1;
	return 0;
}

/* Attach a bubble of nextcolor at cell, pop a 3+ same-color group, drop
 * whatever the pop strands, and pick the next color. In hexagonal mode a
 * run of DESCENT shots without a pop lowers the ceiling. Always a legal
 * placement (the caller has resolved the target); returns 1, or 0 when
 * the hexagonal pile has reached the floor. */
static int
fire_at(int cell)
{
	int i, n, popped = 0;

	grid[cell] = nextcolor;
	n = floodfill(cell);
	if (n >= 3) {
		int dropped;

		for (i = 0; i < n; i++)
			grid[group[i]] = 0;
		score += n * 10;
		/* Popping the top of a pile can strand the bubbles that hung
		 * below it; anything no longer joined to the ceiling falls,
		 * and a dropped bubble is worth more than a popped one. */
		dropped = anchor_and_drop();
		score += dropped * 20;
		popped = 1;
	}
	nextcolor = pickcolor();
	if (!hexmode)
		return 1;
	if (popped)
		misses = 0;
	else if (++misses >= DESCENT) {
		misses = 0;
		descend();
	}
	return !floor_reached();
}

/* Fire along the current aim. Returns 1 on a legal shot, 0 when the shot
 * has nowhere to land (the pile reaches the floor: game over). */
static int
fire(void)
{
	int cell = trajectory();

	if (cell < 0)
		return 0;
	return fire_at(cell);
}

static int
boardempty(void)
{
	int i;

	for (i = 0; i < ROWS * COLS; i++)
		if (grid[i] != 0)
			return 0;
	return 1;
}

#ifndef BUBBLE_LOGIC

#include <unistd.h>
#include "../gametty.h"

/* Per color: a hue (ANSI 3x foreground digit) and a glyph. Index 0 is
 * the empty cell. The glyphs stay 7-bit ASCII; a UTF-8 byte would
 * desync the column math the same way it does in kilo and the tty. */
static const int   huedigit[NCOLORS + 1] = { 7, 1, 2, 4, 3 };
static const char  glyph[NCOLORS + 1]    = { '.', '@', '#', 'O', '*' };
static const char  letter[NCOLORS + 1]   = { '-', 'R', 'G', 'B', 'Y' };

/* One cell as an SGR run: attribute for depth, then the hue, the
 * glyph, and a reset. Rows near the ceiling are bold, the middle
 * normal, the floor dim, so a tall pile reads as a gradient. */
static int
putcell(char *b, int row, int v)
{
	int attr;

	if (v == 0)
		return sprintf(b, "\033[2;37m.\033[0m ");
	attr = (row < ROWS / 3) ? 1 : (row < 2 * ROWS / 3) ? 0 : 2;
	return sprintf(b, "\033[%d;3%dm%c\033[0m ", attr, huedigit[v], glyph[v]);
}

/*
 * write(2) for output whose failure leaves nothing to do. The result is
 * taken into a variable because glibc marks write warn_unused_result under
 * _FORTIFY_SOURCE, and a cast to void does not silence that.
 */
static void
put_str(int fd, const char *s)
{
	ssize_t r = write(fd, s, strlen(s));

	(void)r;
}

static void
draw(void)
{
	char buf[2048];
	const char *arr;
	int p, r, c, cell;

	trajectory();		/* fill pathmark[] for the aim preview */
	gtty_home();
	p = 0;
	p += sprintf(buf + p,
	    "\033[1;36mbubble%s\033[0m  score \033[1;33m%ld\033[0m  next ",
	    hexmode ? " hex" : "", score);
	p += putcell(buf + p, 0, nextcolor);
	p += sprintf(buf + p, "(%c)", letter[nextcolor]);
	if (hexmode)
		p += sprintf(buf + p, "  drop in %d", DESCENT - misses);
	p += sprintf(buf + p, "\033[K\r\n\r\n");

	p += sprintf(buf + p, "  \033[36m.==== ceiling ====.\033[0m\r\n");
	for (r = 0; r < ROWS; r++) {
		p += sprintf(buf + p, "  \033[36m|\033[0m ");
		if (shifted(r))
			p += sprintf(buf + p, " ");
		for (c = 0; c < rowlen(r); c++) {
			cell = r * COLS + c;
			/* An empty cell on the shot's path shows a faint
			 * trail dot so the bounce is visible before firing. */
			if (grid[cell] == 0 && pathmark[cell])
				p += sprintf(buf + p, "\033[2;32m:\033[0m ");
			else
				p += putcell(buf + p, r, grid[cell]);
		}
		if (shifted(r))
			p += sprintf(buf + p, " ");
		p += sprintf(buf + p, "\033[36m|\033[0m\r\n");
	}
	p += sprintf(buf + p, "  \033[36m'-----------------'\033[0m\r\n");
	/* Launcher under the center column; the glyph leans the way the
	 * shot leaves -- up-left, straight, or up-right. */
	arr = aim < 0 ? "\033[1;32m\\\033[0m"
	    : aim > 0 ? "\033[1;32m/\033[0m"
	    : "\033[1;32m|\033[0m";
	p += sprintf(buf + p, "  \033[36m|\033[0m ");
	for (c = 0; c < COLS; c++)
		p += sprintf(buf + p, "%s ", (c == COLS / 2) ? arr : " ");
	p += sprintf(buf + p, "\033[36m|\033[0m\r\n");
	p += sprintf(buf + p, "  aim \033[1;32m%+d\033[0m\r\n", aim);
	buf[p] = 0;
	put_str(1, buf);
	put_str(1, "\r\nleft/right aim, space fires, q quits\r\n");
}

int
main(int argc, char **argv)
{
	const char *env;
	int k, shots, ai = 1;

	if (ai < argc && strcmp(argv[ai], "-x") == 0) {
		hexmode = 1;
		ai++;
	}
	if (ai + 1 < argc || (ai < argc && argv[ai][0] == '-')) {
		put_str(2, "usage: bubble [-x] [seed]\n");
		return 2;
	}
	env = getenv("GAMEBOX_TEST");
	testmode = env != 0;
	if (env && script_ok(env))
		script = env;
	srand(ai < argc ? (unsigned)atoi(argv[ai]) : (unsigned)time(NULL));
	aim = 0;			/* straight up */
	nextcolor = pickcolor();
	initboard();
	shots = 0;

	gtty_raw();
	gtty_clear();
	for (;;) {
		draw();
		/* An empty board only means "cleared" after at least one
		 * shot; GAMEBOX_TEST starts with an empty board on purpose. */
		if (shots > 0 && boardempty()) {
			put_str(1, "\r\n\033[1;32mcleared!\033[0m\r\n");
			break;
		}
		k = gtty_getkey();
		if (k == 'q' || k < 0)
			break;
		if (k == GK_LEFT && aim > -AIMMAX)
			aim--;
		else if (k == GK_RIGHT && aim < AIMMAX)
			aim++;
		else if (k == ' ') {
			if (!fire()) {
				draw();
				put_str(1, "\r\n\033[1;31mgame over\033[0m\r\n");
				break;
			}
			shots++;
		}
	}
	gtty_restore();
	return 0;
}

#endif /* !BUBBLE_LOGIC */
