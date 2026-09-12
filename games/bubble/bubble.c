/*
 * bubble -- a small puzzle-bobble-style matching game, ASCII front
 * end for the DiscoBSD RP2040 port. Clean-room implementation; the
 * pb_core / gororoba_puzzle code in ~/Github/puzzles is GPL or
 * unlicensed (see that repo's AGENTS.md and LICENSE) and was used as
 * behavior reference only ("aim, shoot, pop groups of three"), not
 * as a source of code. No shape here is copied from it.
 *
 * Model, chosen to fit a few hundred lines: bubbles rest on the
 * floor in COLS independent columns, like Connect Four, rather than
 * hanging from a ceiling with floating pieces dropping off after a
 * pop (the classic puzzle-bobble physics); this needs no separate
 * "is this bubble still connected to the top" pass. A shot lands on
 * top of its column's pile; a same-color group of 3 or more
 * connected 4-way pops, and every column then settles (gravity) to
 * close the gaps left behind. The board starts with INITROWS rows of
 * random color already on the floor; the game is won by clearing the
 * board and lost by a column filling to the ceiling.
 *
 * usage: bubble [seed]
 *
 * left/right aim, space fires, q quits.
 *
 * Testing hook: if GAMEBOX_TEST is set, the board starts empty and
 * the shot color holds for three shots before advancing (1,1,1,2,2,2,
 * ...) regardless of seed, so a host test can fire three shots into
 * one empty column and see them pop without depending on any
 * particular rand() implementation.
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

static char
colorchar(int c)
{
	static const char letters[NCOLORS + 2] = "-RGBY";
	return letters[c];
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
	for (r = ROWS - INITROWS; r < ROWS; r++)
		for (c = 0; c < COLS; c++)
			grid[r * COLS + c] = rand() % NCOLORS + 1;
}

static void
settle(void)
{
	int c, r, w;
	int tmp[ROWS];

	for (c = 0; c < COLS; c++) {
		w = ROWS;
		for (r = ROWS - 1; r >= 0; r--)
			if (grid[r * COLS + c] != 0)
				tmp[--w] = grid[r * COLS + c];
		for (r = 0; r < ROWS; r++)
			grid[r * COLS + c] = (r >= w) ? tmp[r] : 0;
	}
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

/* Fire into column c; returns 1 on a legal shot, 0 if the column is
 * already full to the ceiling (game over). */
static int
fire(int c)
{
	int top, row, i, n;

	top = ROWS;
	for (row = 0; row < ROWS; row++)
		if (grid[row * COLS + c] != 0) {
			top = row;
			break;
		}
	if (top == 0)
		return 0;
	row = (top == ROWS) ? ROWS - 1 : top - 1;
	grid[row * COLS + c] = nextcolor;
	n = floodfill(row * COLS + c);
	if (n >= 3) {
		for (i = 0; i < n; i++)
			grid[group[i]] = 0;
		score += n * 10;
		settle();
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
	char buf[512];
	int p, r, c;

	gtty_home();
	p = 0;
	p += sprintf(buf + p, "bubble -- score %ld  next %c\r\n\r\n",
	    score, colorchar(nextcolor));
	for (r = 0; r < ROWS; r++) {
		for (c = 0; c < COLS; c++) {
			int v = grid[r * COLS + c];
			buf[p++] = v ? colorchar(v) : '.';
			buf[p++] = ' ';
		}
		buf[p++] = '\r'; buf[p++] = '\n';
	}
	for (c = 0; c < COLS; c++) {
		buf[p++] = (c == aim) ? '^' : ' ';
		buf[p++] = ' ';
	}
	buf[p++] = '\r'; buf[p++] = '\n';
	write(1, buf, p);
	write(1, "\r\nleft/right aim, space fires, q quits\r\n", 41);
}

int
main(int argc, char **argv)
{
	int k, shots;

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
			write(1, "\r\ncleared!\r\n", 12);
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
				write(1, "\r\ngame over\r\n", 14);
				break;
			}
			shots++;
		}
	}
	gtty_restore();
	return 0;
}
