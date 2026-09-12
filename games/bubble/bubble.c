/*
 * bubble -- a small color-matching game for the DiscoBSD RP2040 port,
 * puzzle-bobble in feel: bubbles hang from the ceiling and the shooter
 * fires up from the floor. Clean-room implementation; the pb_core /
 * gororoba_puzzle code in ~/Github/puzzles is GPL or unlicensed (see
 * that repo's AGENTS.md and LICENSE) and served as behavior reference
 * only ("aim, shoot, pop groups of three"), not as a source of code.
 * No shape here is copied from it.
 *
 * Model, chosen to fit a few hundred lines: each of COLS columns holds
 * a pile that hangs from the ceiling (row 0) and grows downward. A shot
 * attaches beneath the lowest bubble in its column; a 4-way-connected
 * same-color group of 3 or more pops, and every bubble left
 * unsupported by the pop -- no longer joined to the
 * ceiling row -- then falls, the defining puzzle-bobble rule. The board
 * starts with INITROWS rows of random color at the ceiling; it is won by
 * clearing the board and lost when a column grows down to the floor and
 * touches the shooter.
 *
 * The console is 80x24 ANSI/VT100 over USB CDC-ACM. Color is SGR: each
 * color carries both a distinct hue and a distinct glyph, so the board
 * reads on a monochrome terminal as well, and a top-to-bottom
 * bold/normal/dim gradient gives the pile depth.
 *
 * usage: bubble [seed]
 *
 * left/right aim, space fires, q quits.
 *
 * Testing hook: if GAMEBOX_TEST is set, the board starts empty and the
 * shot color holds for three shots before advancing (1,1,1,2,2,2,...)
 * regardless of seed, so a host test can fire three shots into one
 * empty column and see them pop without depending on any particular
 * rand() implementation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "../gametty.h"

#define COLS		8
#define ROWS		10
#define INITROWS	3
#define NCOLORS		4

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
static int cyclecolor = 1;
static int cyclecount;

/* Per color: a hue (ANSI 3x foreground digit) and a glyph. Index 0 is
 * the empty cell. The glyphs stay 7-bit ASCII; a UTF-8 byte would
 * desync the column math the same way it does in kilo and the tty. */
static const int   huedigit[NCOLORS + 1] = { 7, 1, 2, 4, 3 };
static const char  glyph[NCOLORS + 1]    = { '.', '@', '#', 'O', '*' };
static const char  letter[NCOLORS + 1]   = { '-', 'R', 'G', 'B', 'Y' };

static char
colorchar(int c)
{
	return letter[c];
}

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

static int
pickcolor(void)
{
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
	/* Fill the interior columns only; the two outer columns start open
	 * so a shot bounced off a side wall can tuck under the block's edge
	 * from the first move, where a straight shot cannot reach. */
	for (r = 0; r < INITROWS; r++)
		for (c = 1; c < COLS - 1; c++)
			grid[r * COLS + c] = rand() % NCOLORS + 1;
}

/* Puzzle-bobble drop: after a pop, any bubble no longer joined 4-way to
 * the ceiling row falls away. Flood from every occupied cell in row 0
 * through occupied neighbors; occupied cells left unvisited are
 * unsupported and are removed. Returns the count dropped. */
static int mark2[ROWS * COLS];

static int
anchor_and_drop(void)
{
	int stk[ROWS * COLS];
	int sp, i, c, cur, r, cc, dropped;

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
		r = cur / COLS;
		cc = cur % COLS;
		if (r > 0 && grid[cur - COLS] && !mark2[cur - COLS]) {
			mark2[cur - COLS] = 1; stk[sp++] = cur - COLS;
		}
		if (r < ROWS - 1 && grid[cur + COLS] && !mark2[cur + COLS]) {
			mark2[cur + COLS] = 1; stk[sp++] = cur + COLS;
		}
		if (cc > 0 && grid[cur - 1] && !mark2[cur - 1]) {
			mark2[cur - 1] = 1; stk[sp++] = cur - 1;
		}
		if (cc < COLS - 1 && grid[cur + 1] && !mark2[cur + 1]) {
			mark2[cur + 1] = 1; stk[sp++] = cur + 1;
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

static int
floodfill(int start)
{
	int stk[ROWS * COLS];
	int sp, n, cur, r, c, color, i;

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
		r = cur / COLS;
		c = cur % COLS;
		if (r > 0 && !mark[cur - COLS] && grid[cur - COLS] == color) {
			mark[cur - COLS] = 1;
			stk[sp++] = cur - COLS;
		}
		if (r < ROWS - 1 && !mark[cur + COLS] && grid[cur + COLS] == color) {
			mark[cur + COLS] = 1;
			stk[sp++] = cur + COLS;
		}
		if (c > 0 && !mark[cur - 1] && grid[cur - 1] == color) {
			mark[cur - 1] = 1;
			stk[sp++] = cur - 1;
		}
		if (c < COLS - 1 && !mark[cur + 1] && grid[cur + 1] == color) {
			mark[cur + 1] = 1;
			stk[sp++] = cur + 1;
		}
	}
	return n;
}

/* Walk the shot's path from the launcher (bottom-center) upward and
 * return the grid cell where it attaches, or -1 if the launch cell is
 * already occupied (the pile has reached the floor: game over). A DDA
 * advances one fine unit at a time along whichever axis lags the ideal
 * line given by slope_run:slope_rise for the current aim, reflecting the
 * sideways direction at the walls (edges of columns 0 and COLS-1). The
 * ball stops one cell short of the first occupied cell it would enter,
 * or at row 0 when it reaches the ceiling; the attach cell is the last
 * empty cell it occupied, always 4-connected to the collision because
 * consecutive cells in the walk differ by one grid step. Records the
 * path in pathmark[]/pathseq[] for the aim preview; reads grid, never
 * writes it. */
static int
trajectory(void)
{
	int a, sx, run, rise, xf, yf, ax, ay, col, row, cell, last, i;

	for (i = 0; i < ROWS * COLS; i++)
		pathmark[i] = 0;
	pathlen = 0;

	a = aim < 0 ? -aim : aim;
	sx = aim < 0 ? -1 : 1;
	run = slope_run[a];
	rise = slope_rise[a];

	xf = FW / 2;
	yf = FH - 1;
	col = xf / SUB;
	row = yf / SUB;
	cell = row * COLS + col;
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
		col = xf / SUB;
		row = yf / SUB;
		cell = row * COLS + col;
		if (cell == last)
			continue;		/* still inside the same cell */
		last = cell;
		if (grid[cell])
			return pathseq[pathlen - 1];	/* attach below it */
		pathmark[cell] = 1;
		pathseq[pathlen++] = cell;
	}
}

/* Attach a bubble of nextcolor at cell, pop a 3+ same-color group, drop
 * whatever the pop strands, and pick the next color. Always a legal
 * placement (the caller has resolved the target); returns 1. */
static int
fire_at(int cell)
{
	int i, n;

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
	}
	nextcolor = pickcolor();
	return 1;
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
	    "\033[1;36mbubble\033[0m  score \033[1;33m%ld\033[0m  next ",
	    score);
	p += putcell(buf + p, 0, nextcolor);
	p += sprintf(buf + p, "(%c)\r\n\r\n", colorchar(nextcolor));

	p += sprintf(buf + p, "  \033[36m.==== ceiling ====.\033[0m\r\n");
	for (r = 0; r < ROWS; r++) {
		p += sprintf(buf + p, "  \033[36m|\033[0m ");
		for (c = 0; c < COLS; c++) {
			cell = r * COLS + c;
			/* An empty cell on the shot's path shows a faint
			 * trail dot so the bounce is visible before firing. */
			if (grid[cell] == 0 && pathmark[cell])
				p += sprintf(buf + p, "\033[2;32m:\033[0m ");
			else
				p += putcell(buf + p, r, grid[cell]);
		}
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
	write(1, buf, p);
	write(1, "\r\nleft/right aim, space fires, q quits\r\n", 40);
}

#ifdef BUBBLE_SELFTEST
/* Verifies the drop physics (A, B), which the trajectory does not touch,
 * and the ricochet geometry (C): a straight shot reaches the ceiling or
 * stops one cell below a blocker, and a wall-ward shot reflects and lands
 * on the far side of the launcher. */
static int
selftest(void)
{
	int i, d, cell, fails = 0;

	/* A: a cluster not joined to the ceiling falls; the anchored one stays. */
	for (i = 0; i < ROWS * COLS; i++)
		grid[i] = 0;
	grid[0 * COLS + 0] = 1;
	grid[1 * COLS + 0] = 1;
	grid[3 * COLS + 3] = 2;
	grid[4 * COLS + 3] = 2;
	d = anchor_and_drop();
	if (d != 2) { printf("A: dropped %d, want 2\n", d); fails++; }
	if (!grid[0 * COLS + 0] || !grid[1 * COLS + 0]) { printf("A: anchored removed\n"); fails++; }
	if (grid[3 * COLS + 3] || grid[4 * COLS + 3]) { printf("A: floating kept\n"); fails++; }

	/* B: attaching a red at (0,3) pops the row-0 trio and strands the blue
	 * that hung below it, which then falls. Score = 3*10 + 1*20. */
	for (i = 0; i < ROWS * COLS; i++)
		grid[i] = 0;
	grid[0 * COLS + 1] = 1;
	grid[0 * COLS + 2] = 1;
	grid[1 * COLS + 3] = 2;
	score = 0;
	nextcolor = 1;
	fire_at(0 * COLS + 3);
	if (grid[0*COLS+1] || grid[0*COLS+2] || grid[0*COLS+3]) { printf("B: trio not popped\n"); fails++; }
	if (grid[1 * COLS + 3]) { printf("B: stranded bubble not dropped\n"); fails++; }
	if (score != 3 * 10 + 1 * 20) { printf("B: score %ld, want 50\n", score); fails++; }

	/* C1: straight up an empty column reaches the ceiling, center column. */
	for (i = 0; i < ROWS * COLS; i++)
		grid[i] = 0;
	aim = 0;
	cell = trajectory();
	if (cell != 0 * COLS + COLS / 2) { printf("C1: landed %d, want %d\n", cell, COLS / 2); fails++; }

	/* C2: straight up beneath an occupied ceiling cell stops one row below. */
	grid[0 * COLS + COLS / 2] = 1;
	aim = 0;
	cell = trajectory();
	if (cell != 1 * COLS + COLS / 2) { printf("C2: landed %d, want %d\n", cell, 1 * COLS + COLS / 2); fails++; }

	/* C3: a shallow right shot must reach the right wall and reflect --
	 * a later path cell sits left of the wall it just touched. */
	for (i = 0; i < ROWS * COLS; i++)
		grid[i] = 0;
	aim = AIMMAX;
	trajectory();
	{
		int j, hitwall = 0, reflected = 0;
		for (j = 0; j < pathlen; j++) {
			int pc = pathseq[j] % COLS;
			if (pc == COLS - 1) hitwall = 1;
			else if (hitwall) reflected = 1;
		}
		if (!hitwall) { printf("C3: never reached the wall\n"); fails++; }
		if (!reflected) { printf("C3: did not reflect off the wall\n"); fails++; }
	}
	aim = 0;

	printf(fails ? "SELFTEST FAIL\n" : "SELFTEST OK\n");
	return fails ? 1 : 0;
}
#endif

int
main(int argc, char **argv)
{
	int k, shots;

#ifdef BUBBLE_SELFTEST
	(void) argc; (void) argv;
	return selftest();
#endif

	testmode = getenv("GAMEBOX_TEST") != 0;
	srand(argc > 1 ? (unsigned)atoi(argv[1]) : (unsigned)time(NULL));
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
			write(1, "\r\n\033[1;32mcleared!\033[0m\r\n", 22);
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
				write(1, "\r\n\033[1;31mgame over\033[0m\r\n", 24);
				break;
			}
			shots++;
		}
	}
	gtty_restore();
	return 0;
}
