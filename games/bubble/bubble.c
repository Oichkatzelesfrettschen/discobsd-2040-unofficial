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

static int grid[ROWS * COLS];	/* 0 empty, else color 1..NCOLORS */
static int aim;			/* aim column */
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
	for (r = 0; r < INITROWS; r++)
		for (c = 0; c < COLS; c++)
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

/* Fire into column c: the bubble attaches beneath the lowest bubble in
 * the pile. Returns 1 on a legal shot, 0 when the pile already reaches
 * the floor (game over). */
static int
fire(int c)
{
	int row, i, n;

	row = 0;
	while (row < ROWS && grid[row * COLS + c] != 0)
		row++;
	if (row >= ROWS)
		return 0;
	grid[row * COLS + c] = nextcolor;
	n = floodfill(row * COLS + c);
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
	int p, r, c;

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
		for (c = 0; c < COLS; c++)
			p += putcell(buf + p, r, grid[r * COLS + c]);
		p += sprintf(buf + p, "\033[36m|\033[0m\r\n");
	}
	p += sprintf(buf + p, "  \033[36m'-----------------'\033[0m\r\n");
	p += sprintf(buf + p, "    ");
	for (c = 0; c < COLS; c++)
		p += sprintf(buf + p, "%s ",
		    (c == aim) ? "\033[1;32m^\033[0m" : " ");
	p += sprintf(buf + p, "\r\n");
	write(1, buf, p);
	write(1, "\r\nleft/right aim, space fires, q quits\r\n", 40);
}

#ifdef BUBBLE_SELFTEST
/* Verifies the puzzle-bobble drop: a floating cluster falls, and popping a
 * supporting group strands the bubble beneath it, which then drops. */
static int
selftest(void)
{
	int i, d, fails = 0;

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

	/* B: firing the third red pops the row-0 trio and strands the blue that
	 * hung below the landing cell, which then falls. Score = 3*10 + 1*20. */
	for (i = 0; i < ROWS * COLS; i++)
		grid[i] = 0;
	grid[0 * COLS + 1] = 1;
	grid[0 * COLS + 2] = 1;
	grid[1 * COLS + 3] = 2;
	score = 0;
	nextcolor = 1;
	fire(3);
	if (grid[0*COLS+1] || grid[0*COLS+2] || grid[0*COLS+3]) { printf("B: trio not popped\n"); fails++; }
	if (grid[1 * COLS + 3]) { printf("B: stranded bubble not dropped\n"); fails++; }
	if (score != 3 * 10 + 1 * 20) { printf("B: score %ld, want 50\n", score); fails++; }

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
	aim = COLS / 2;
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
		if (k == GK_LEFT && aim > 0)
			aim--;
		else if (k == GK_RIGHT && aim < COLS - 1)
			aim++;
		else if (k == ' ') {
			if (!fire(aim)) {
				write(1, "\r\n\033[1;31mgame over\033[0m\r\n", 24);
				break;
			}
			shots++;
		}
	}
	gtty_restore();
	return 0;
}
