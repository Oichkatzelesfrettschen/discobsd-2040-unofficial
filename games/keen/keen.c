/*
 * keen -- a small KenKen/Keen generator and ASCII front end for the
 * DiscoBSD RP2040 port. Novel implementation; no code taken from
 * Simon Tatham's Portable Puzzle Collection keen.c (MIT, LICENCE in
 * that tree) or from KeenClassik/KeenKenning/rustykeen, which were
 * surveyed for algorithm ideas only. Two simplifications keep this
 * generator small enough for the box:
 *
 *   - The Latin square is built by shuffling the rows, columns and
 *     symbol labels of a cyclic base square (a standard construction:
 *     permuting a Latin square's rows, columns or symbols yields
 *     another Latin square). This needs no backtracking search.
 *   - Cages are grown by random adjacent unions with no uniqueness
 *     check, so a puzzle can (rarely) admit solutions besides the one
 *     generated. "Solved" therefore means "matches the generated
 *     grid", not "satisfies every clue", which is the same shortcut
 *     most small ASCII KenKen clones take. Tatham's keen.c instead
 *     runs a constraint solver over every candidate grid to guarantee
 *     a unique solution; that solver and its latin.c helper are
 *     bigger than this whole box and are not reused here.
 *
 * usage: keen [size 3..6] [seed]
 *
 * Cursor keys move the selection, digits 1..size enter a value, 0 or
 * backspace clears a cell, q quits. A clue reads like "6+", "12*",
 * "3-" or "2/"; a lone number is a one-cell cage with no operation.
 *
 * Testing hook: if GAMEBOX_TEST is set, the generated solution grid
 * is printed as one line of digits (row-major, no separators) before
 * play starts, so a host test can fill the grid correctly without
 * depending on any particular rand() implementation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "../gametty.h"

#define MAXN	6
#define MAXCAGE	4
#define NONE	0
#define ADD	1
#define SUB	2
#define MUL	3
#define DIV	4

static int n;				/* grid size, 3..6 */
static int sol[MAXN * MAXN];		/* solution, 0-based values */
static int entry[MAXN * MAXN];		/* player's entries, 0 = empty */
static int cage[MAXN * MAXN];		/* cage id (union-find root) per cell */
static int clueop[MAXN * MAXN];	/* op for the cage rooted here */
static int clueval[MAXN * MAXN];	/* clue number for the cage rooted here */
static int cluecell[MAXN * MAXN];	/* is this cell the labeled one */
static int cx, cy;			/* cursor column, row */

static int
find(int *uf, int x)
{
	while (uf[x] != x)
		x = uf[x];
	return x;
}

static void
munion(int *uf, int *sz, int a, int b)
{
	a = find(uf, a);
	b = find(uf, b);
	if (a == b)
		return;
	if (sz[a] < sz[b]) {
		int t = a; a = b; b = t;
	}
	uf[b] = a;
	sz[a] += sz[b];
}

static void
shuffle(int *a, int len)
{
	int i, j, t;

	for (i = len - 1; i > 0; i--) {
		j = rand() % (i + 1);
		t = a[i]; a[i] = a[j]; a[j] = t;
	}
}

static void
gen_latin(void)
{
	int rowperm[MAXN], colperm[MAXN], symperm[MAXN];
	int r, c, base[MAXN * MAXN];

	for (c = 0; c < n; c++)
		rowperm[c] = colperm[c] = symperm[c] = c;
	shuffle(rowperm, n);
	shuffle(colperm, n);
	shuffle(symperm, n);

	for (r = 0; r < n; r++)
		for (c = 0; c < n; c++)
			base[r * n + c] = (c + r) % n;

	for (r = 0; r < n; r++)
		for (c = 0; c < n; c++)
			sol[r * n + c] =
			    symperm[base[rowperm[r] * n + colperm[c]]];
}

static void
gen_cages(void)
{
	int uf[MAXN * MAXN], sz[MAXN * MAXN];
	int i, tries, a, dir, ar, ac, br, bc, b;
	int cells, ncells;

	ncells = n * n;
	for (i = 0; i < ncells; i++) {
		uf[i] = i;
		sz[i] = 1;
	}
	cells = ncells;
	for (tries = 0; tries < ncells * 6 && cells > ncells / 3; tries++) {
		a = rand() % ncells;
		dir = rand() % 4;
		ar = a / n; ac = a % n;
		br = ar; bc = ac;
		switch (dir) {
		case 0: br--; break;
		case 1: br++; break;
		case 2: bc--; break;
		default: bc++; break;
		}
		if (br < 0 || br >= n || bc < 0 || bc >= n)
			continue;
		b = br * n + bc;
		if (find(uf, a) == find(uf, b))
			continue;
		if (sz[find(uf, a)] + sz[find(uf, b)] > MAXCAGE)
			continue;
		if (rand() % 100 >= 55)
			continue;
		munion(uf, sz, a, b);
		cells--;
	}
	for (i = 0; i < ncells; i++)
		cage[i] = find(uf, i);
}

static void
gen_clues(void)
{
	int i, r, members[MAXN * MAXN], nm;
	int v, sum, prod, lo, hi;

	for (i = 0; i < n * n; i++)
		cluecell[i] = 0;
	/*
	 * gen_cages roots each cage at its union-find representative, and
	 * draw() reads a cage's clue from that same root cell (cage[i] == i).
	 * A representative is not always the cage's lowest cell index, so the
	 * clue must be assigned per representative here; keying off the lowest
	 * index instead leaves every cage whose root is not its lowest cell at
	 * the zero-initialized NONE/0 clue and makes the puzzle unsolvable.
	 */
	for (r = 0; r < n * n; r++) {
		if (cage[r] != r)
			continue;
		nm = 0;
		for (i = 0; i < n * n; i++)
			if (cage[i] == r)
				members[nm++] = i;
		cluecell[r] = 1;
		if (nm == 1) {
			clueop[r] = NONE;
			clueval[r] = sol[members[0]] + 1;
			continue;
		}
		sum = 0; prod = 1;
		for (i = 0; i < nm; i++) {
			v = sol[members[i]] + 1;
			sum += v;
			prod *= v;
		}
		if (nm == 2) {
			int v0 = sol[members[0]] + 1, v1 = sol[members[1]] + 1;
			int pick = rand() % 4;
			lo = v0 < v1 ? v0 : v1;
			hi = v0 < v1 ? v1 : v0;
			if (pick == 3 && hi % lo != 0)
				pick = rand() % 3;
			switch (pick) {
			case 0: clueop[r] = ADD; clueval[r] = sum; break;
			case 1: clueop[r] = SUB; clueval[r] = hi - lo; break;
			case 2: clueop[r] = MUL; clueval[r] = prod; break;
			default: clueop[r] = DIV; clueval[r] = hi / lo; break;
			}
		} else {
			if (rand() % 2) {
				clueop[r] = ADD;
				clueval[r] = sum;
			} else {
				clueop[r] = MUL;
				clueval[r] = prod;
			}
		}
	}
}

static int
solved(void)
{
	int i;

	for (i = 0; i < n * n; i++)
		if (entry[i] != sol[i] + 1)
			return 0;
	return 1;
}

static void
dumpsolution(void)
{
	char line[MAXN * MAXN + 2];
	int i;

	for (i = 0; i < n * n; i++)
		line[i] = '1' + sol[i];
	line[n * n] = '\n';
	write(1, line, n * n + 1);
}

static void
draw(void)
{
	int r, c, root;
	char buf[512];
	int p, i, bnd;
	char clue[8];

	gtty_home();
	p = 0;
	p += sprintf(buf + p, "keen %dx%d\r\n\r\n", n, n);
	write(1, buf, p);

	for (r = 0; r < n; r++) {
		/*
		 * The separator above row r closes every cage: it is solid at
		 * the outer edge (r == 0) and between two cells in different
		 * cages, and blank inside a cage so the cage reads as one
		 * region. A junction character sits at every corner so the
		 * grid stays aligned whether or not the segments are drawn.
		 */
		p = 0;
		buf[p++] = '+';
		for (c = 0; c < n; c++) {
			bnd = (r == 0) ||
			    (cage[(r - 1) * n + c] != cage[r * n + c]);
			for (i = 0; i < 4; i++)
				buf[p++] = bnd ? '-' : ' ';
			buf[p++] = '+';
		}
		buf[p++] = '\r'; buf[p++] = '\n';

		/* Clue line: the cage's target in its labeled cell, padded to
		 * the four-column cell so a two-digit clue never overflows. */
		buf[p++] = '|';
		for (c = 0; c < n; c++) {
			clue[0] = clue[1] = clue[2] = clue[3] = 0;
			root = cage[r * n + c];
			/* The clue belongs to the cage's root cell alone,
			 * so a cage shows its target once, not in every
			 * cell that shares the root. The whole four-column
			 * field is cleared first: a shorter clue must not
			 * leave a previous cell's digits in the tail. */
			if (r * n + c == root) {
				char op = clueop[root] == ADD ? '+' :
				          clueop[root] == SUB ? '-' :
				          clueop[root] == MUL ? '*' :
				          clueop[root] == DIV ? '/' : 0;
				if (op)
					sprintf(clue, "%d%c", clueval[root], op);
				else
					sprintf(clue, "%d", clueval[root]);
			}
			for (i = 0; i < 4; i++)
				buf[p++] = clue[i] ? clue[i] : ' ';
			bnd = (c == n - 1) ||
			    (cage[r * n + c] != cage[r * n + c + 1]);
			buf[p++] = bnd ? '|' : ' ';
		}
		buf[p++] = '\r'; buf[p++] = '\n';

		/* Value line: the entry, or a dot, with the cursor cell in
		 * brackets. The bracket and dot keep the cell four wide. */
		buf[p++] = '|';
		for (c = 0; c < n; c++) {
			int v = entry[r * n + c];
			int cur = (r == cy && c == cx);
			buf[p++] = cur ? '[' : ' ';
			buf[p++] = v ? '0' + v : '.';
			buf[p++] = cur ? ']' : ' ';
			buf[p++] = ' ';
			bnd = (c == n - 1) ||
			    (cage[r * n + c] != cage[r * n + c + 1]);
			buf[p++] = bnd ? '|' : ' ';
		}
		buf[p++] = '\r'; buf[p++] = '\n';
		write(1, buf, p);
	}

	/* Bottom border closes the last row. */
	p = 0;
	buf[p++] = '+';
	for (c = 0; c < n; c++) {
		for (i = 0; i < 4; i++)
			buf[p++] = '-';
		buf[p++] = '+';
	}
	buf[p++] = '\r'; buf[p++] = '\n';
	write(1, buf, p);

	write(1, "\r\narrows move, 1-9 fill, 0 clears, q quits\r\n", 45);
}

int
main(int argc, char **argv)
{
	int k;

	n = argc > 1 ? atoi(argv[1]) : 5;
	if (n < 3) n = 3;
	if (n > MAXN) n = MAXN;
	srand(argc > 2 ? (unsigned)atoi(argv[2]) : (unsigned)time(NULL));

	gen_latin();
	gen_cages();
	gen_clues();
	if (getenv("GAMEBOX_TEST") != 0)
		dumpsolution();

	cx = cy = 0;
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
		if (k == GK_UP && cy > 0) cy--;
		else if (k == GK_DOWN && cy < n - 1) cy++;
		else if (k == GK_LEFT && cx > 0) cx--;
		else if (k == GK_RIGHT && cx < n - 1) cx++;
		else if (k >= '1' && k <= '0' + n)
			entry[cy * n + cx] = k - '0';
		else if (k == '0' || k == 010 || k == 0177)
			entry[cy * n + cx] = 0;
	}
	gtty_restore();
	return 0;
}
