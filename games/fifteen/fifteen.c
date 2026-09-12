/*
 * fifteen -- the 15-puzzle, ASCII front end for the DiscoBSD RP2040
 * port. Novel implementation; no code taken from Simon Tatham's
 * Portable Puzzle Collection fifteen.c (MIT, LICENCE in that tree).
 * That program's puzzles.h/midend drawing API is built for a GUI
 * front end and is far larger than this box's 30 KB budget, so only
 * the well-known shuffle-by-legal-moves technique is reused here (it
 * predates that codebase and is not an expression original to it):
 * start from the solved board and apply a run of random legal slides,
 * which by construction leaves a solvable position, and needs no
 * separate solver to prove it.
 *
 * usage: fifteen [seed]
 *
 * Board is 4x4, cells 1..15 and one blank. Cursor keys move the
 * blank itself one cell in the direction pressed, sliding whatever
 * tile was there into the blank's old place; q quits.
 *
 * Testing hook: if the environment variable GAMEBOX_TEST is set, the
 * program prints the shuffle move list as single letters (u/d/l/r,
 * the direction each slide moved the blank) on one line before the
 * board, so a host test can compute the exact inverse sequence that
 * solves the puzzle without depending on any particular rand()
 * implementation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "../gametty.h"

#define N	4
#define CELLS	(N * N)
#define SHUFFLE	200

static int board[CELLS];	/* board[r*N+c], 0 is the blank */
static int blank;		/* index of the blank cell */
static int moves;		/* player move count */

static void
placeblank(int i)
{
	board[blank] = board[i];
	board[i] = 0;
	blank = i;
}

/* Slide the blank one step in direction d ('u','d','l','r' meaning
 * the blank itself moves up/down/left/right); returns 0 if that
 * would leave the board, else 1. */
static int
slide(int d)
{
	int r, c, nr, nc;

	r = blank / N;
	c = blank % N;
	nr = r;
	nc = c;
	switch (d) {
	case 'u': nr = r - 1; break;
	case 'd': nr = r + 1; break;
	case 'l': nc = c - 1; break;
	case 'r': nc = c + 1; break;
	}
	if (nr < 0 || nr >= N || nc < 0 || nc >= N)
		return 0;
	placeblank(nr * N + nc);
	return 1;
}

static int
opposite(int d)
{
	switch (d) {
	case 'u': return 'd';
	case 'd': return 'u';
	case 'l': return 'r';
	default:  return 'l';
	}
}

static void
shuffle(void)
{
	static const int dirs[4] = { 'u', 'd', 'l', 'r' };
	int made, last, d;
	char log[SHUFFLE + 1];

	last = 0;
	made = 0;
	while (made < SHUFFLE) {
		d = dirs[rand() % 4];
		if (d == last)
			continue;
		if (!slide(d))
			continue;
		log[made] = d;
		made++;
		last = opposite(d);
	}
	log[made] = 0;
	if (getenv("GAMEBOX_TEST") != 0) {
		write(1, log, made);
		write(1, "\n", 1);
	}
}

static int
solved(void)
{
	int i;

	for (i = 0; i < CELLS - 1; i++)
		if (board[i] != i + 1)
			return 0;
	return board[CELLS - 1] == 0;
}

static void
draw(void)
{
	int r, c, v;
	char line[64];
	int n;

	gtty_home();
	printf("fifteen -- moves %d\r\n\r\n", moves);
	for (r = 0; r < N; r++) {
		n = 0;
		for (c = 0; c < N; c++) {
			v = board[r * N + c];
			if (v == 0)
				n += sprintf(line + n, "   .");
			else
				n += sprintf(line + n, "%4d", v);
		}
		line[n++] = '\r';
		line[n++] = '\n';
		write(1, line, n);
	}
	write(1, "\r\narrows slide, q quits\r\n", 25);
}

int
main(int argc, char **argv)
{
	int i, k;

	for (i = 0; i < CELLS; i++)
		board[i] = (i == CELLS - 1) ? 0 : i + 1;
	blank = CELLS - 1;

	srand(argc > 1 ? (unsigned)atoi(argv[1]) : (unsigned)time(NULL));
	shuffle();
	moves = 0;

	gtty_raw();
	gtty_clear();
	for (;;) {
		draw();
		if (solved()) {
			write(1, "\r\nsolved!\r\n", 11);
			break;
		}
		k = gtty_getkey();
		if (k == 'q' || k < 0)
			break;
		if (k == GK_UP || k == GK_DOWN ||
		    k == GK_LEFT || k == GK_RIGHT) {
			if (slide(k == GK_UP ? 'u' :
			          k == GK_DOWN ? 'd' :
			          k == GK_LEFT ? 'l' : 'r'))
				moves++;
		}
	}
	gtty_restore();
	return 0;
}
