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
 *   - Cages are grown by random adjacent unions, then the clues are
 *     checked by a small backtracking solver and the cages are regrown
 *     until exactly one grid satisfies them. Without that check a
 *     puzzle often admits several grids, and a player who has deduced
 *     everything the clues give still faces a guess between them; a
 *     KenKen is only a puzzle when its clues force one answer.
 *     "Solved" means every row and column is a permutation and every
 *     cage meets its clue, which the uniqueness check makes the same
 *     thing as matching the generated grid.
 *
 * usage: keen [size 3..6] [seed]
 *
 * The host build (-DHOSTBUILD) adds two modes for the tests:
 *   keen --dump [size] [seed]   print the generated puzzle as text
 *   keen --count FILE [limit]   count the grids that satisfy a puzzle
 *                               read from FILE, stopping at limit
 * The text format is "size N", a "cages" block of N rows of letters
 * (a-z, then A-Z: a 6x6 has up to 36 cages) naming each cell's cage, a
 * "clues" block of "<letter> <target><op>" lines, and an optional
 * "entries" block of N rows of digits with
 * "." for an empty cell that the count must honor. KEEN_NOUNIQUE=1
 * in the environment skips the uniqueness loop, which reproduces the
 * generator as it was before the check and is how the ambiguous
 * fixture in tests/ was made.
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
#include <string.h>
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
#define GEN_TRIES	64		/* cage regrowths before a new square */

static int n;				/* grid size, 3..6 */
static int sol[MAXN * MAXN];		/* solution, 0-based values */
static int entry[MAXN * MAXN];		/* player's entries, 0 = empty */
static int cage[MAXN * MAXN];		/* cage id (union-find root) per cell */
static int clueop[MAXN * MAXN];	/* op for the cage rooted here */
static int clueval[MAXN * MAXN];	/* clue number for the cage rooted here */
static int cluecell[MAXN * MAXN];	/* is this cell the labeled one */
static int cmem[MAXN * MAXN][MAXCAGE];	/* member cells of the cage rooted here */
static int cnm[MAXN * MAXN];		/* how many members */
static int cx, cy;			/* cursor column, row */

/* Solver state: the grid being tried, fixed cells, and row/column masks. */
static int work[MAXN * MAXN];
static int fixedv[MAXN * MAXN];		/* imposed value per cell, 0 = free */
static int rowmask[MAXN], colmask[MAXN];
static int nfound, nlimit;

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
		cnm[r] = nm;
		for (i = 0; i < nm; i++)
			cmem[r][i] = members[i];
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

/*
 * Does the cage rooted at r accept the values in g? With every member
 * filled the clue must hold exactly; with some still zero a partial
 * check prunes what can no longer work: a sum already past its
 * target, a product that does not divide its target. Subtraction and
 * division cages have two cells and are judged only when both are in.
 */
static int
cage_ok(const int *g, int r)
{
	int i, v, sum = 0, prod = 1, filled = 0, lo = 0, hi = 0;

	for (i = 0; i < cnm[r]; i++) {
		v = g[cmem[r][i]];
		if (v == 0)
			continue;
		filled++;
		sum += v;
		prod *= v;
		if (lo == 0 || v < lo)
			lo = v;
		if (v > hi)
			hi = v;
	}
	switch (clueop[r]) {
	case NONE:
		return filled == 0 || sum == clueval[r];
	case ADD:
		if (sum > clueval[r])
			return 0;
		return filled < cnm[r] || sum == clueval[r];
	case MUL:
		if (clueval[r] % prod != 0)
			return 0;
		return filled < cnm[r] || prod == clueval[r];
	case SUB:
		return filled < 2 || hi - lo == clueval[r];
	default:
		return filled < 2 || (hi % lo == 0 && hi / lo == clueval[r]);
	}
}

/* Every row and column a permutation, every cage at its clue. */
static int
grid_ok(const int *g)
{
	int r, c, i, m;

	for (r = 0; r < n; r++) {
		m = 0;
		for (c = 0; c < n; c++)
			m |= 1 << g[r * n + c];
		if (m != ((2 << n) - 2))
			return 0;
	}
	for (c = 0; c < n; c++) {
		m = 0;
		for (r = 0; r < n; r++)
			m |= 1 << g[r * n + c];
		if (m != ((2 << n) - 2))
			return 0;
	}
	for (i = 0; i < n * n; i++)
		if (cage[i] == i && !cage_ok(g, i))
			return 0;
	return 1;
}

/*
 * Count the grids that satisfy every clue, honoring fixedv, until
 * nlimit are found. Cells are filled row-major; each placement is
 * checked against its row, its column, and its cage's partial clue,
 * which is enough pruning for a 6x6 with cages of at most four cells.
 */
static void
search(int i)
{
	int r, c, v, lo, hi;

	if (nfound >= nlimit)
		return;
	if (i == n * n) {
		nfound++;
		return;
	}
	r = i / n; c = i % n;
	lo = fixedv[i] ? fixedv[i] : 1;
	hi = fixedv[i] ? fixedv[i] : n;
	for (v = lo; v <= hi; v++) {
		if ((rowmask[r] | colmask[c]) & (1 << v))
			continue;
		work[i] = v;
		rowmask[r] |= 1 << v;
		colmask[c] |= 1 << v;
		if (cage_ok(work, cage[i]))
			search(i + 1);
		rowmask[r] &= ~(1 << v);
		colmask[c] &= ~(1 << v);
		work[i] = 0;
	}
}

static int
count_solutions(int limit)
{
	int i;

	for (i = 0; i < n * n; i++)
		work[i] = 0;
	for (i = 0; i < n; i++)
		rowmask[i] = colmask[i] = 0;
	nfound = 0;
	nlimit = limit;
	search(0);
	return nfound;
}

/*
 * Generate until the clues admit exactly one grid. The first attempt
 * is the same puzzle the generator produced before the check existed,
 * so a seed's puzzle only changes when that puzzle was ambiguous. The
 * Latin square is regrown after GEN_TRIES cage layouts fail, which
 * keeps a square that happens to resist unique cages from looping.
 */
static void
gen_puzzle(void)
{
	int tries = 0;

	gen_latin();
	for (;;) {
		gen_cages();
		gen_clues();
		if (count_solutions(2) == 1)
			return;
		if (++tries % GEN_TRIES == 0)
			gen_latin();
	}
}

static int
solved(void)
{
	int i;

	for (i = 0; i < n * n; i++)
		if (entry[i] == 0)
			return 0;
	return grid_ok(entry);
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

#ifdef HOSTBUILD
static const char labels[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

/* Print the puzzle in the text format the --count mode reads; cages
 * are lettered in the order of their root cells. */
static void
dumppuzzle(void)
{
	int r, c, i, root, k = 0, letter[MAXN * MAXN];
	char opc;

	for (i = 0; i < n * n; i++)
		if (cage[i] == i)
			letter[i] = labels[k++];
	printf("size %d\ncages\n", n);
	for (r = 0; r < n; r++) {
		for (c = 0; c < n; c++)
			putchar(letter[cage[r * n + c]]);
		putchar('\n');
	}
	printf("clues\n");
	for (i = 0; i < n * n; i++) {
		if (cage[i] != i)
			continue;
		root = i;
		opc = clueop[root] == ADD ? '+' : clueop[root] == SUB ? '-' :
		    clueop[root] == MUL ? '*' : clueop[root] == DIV ? '/' : 0;
		if (opc)
			printf("%c %d%c\n", letter[root], clueval[root], opc);
		else
			printf("%c %d\n", letter[root], clueval[root]);
	}
}

/*
 * Read a puzzle in that format. Cage letters are labels only: a cage's
 * root becomes its lowest cell, as gen_cages would make it. Returns 0
 * on success, otherwise the line number of the first fault, or -1 when
 * the file ends with a cage or clue missing.
 */
static int
loadpuzzle(FILE *f)
{
	char line[128];
	int label[MAXN * MAXN], rootof[128];
	int r, c, i, lineno = 0, root, val;
	char lab, opc;

	n = 0;
	for (i = 0; i < 128; i++)
		rootof[i] = -1;
	for (i = 0; i < MAXN * MAXN; i++) {
		fixedv[i] = 0;
		cnm[i] = 0;
		cage[i] = -1;
		clueop[i] = -1;
	}
	while (fgets(line, sizeof line, f) != NULL) {
		lineno++;
		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (sscanf(line, "size %d", &n) == 1) {
			if (n < 3 || n > MAXN)
				return lineno;
			continue;
		}
		if (n == 0)
			return lineno;
		if (strncmp(line, "cages", 5) == 0) {
			for (r = 0; r < n; r++) {
				if (fgets(line, sizeof line, f) == NULL)
					return -1;
				lineno++;
				for (c = 0; c < n; c++) {
					lab = line[c];
					if (strchr(labels, lab) == NULL || lab == 0)
						return lineno;
					label[r * n + c] = lab;
					if (rootof[(int)lab] < 0)
						rootof[(int)lab] = r * n + c;
				}
			}
			for (i = 0; i < n * n; i++) {
				root = rootof[label[i]];
				cage[i] = root;
				if (cnm[root] >= MAXCAGE)
					return lineno;
				cmem[root][cnm[root]++] = i;
			}
			continue;
		}
		if (strncmp(line, "clues", 5) == 0)
			continue;
		if (strncmp(line, "entries", 7) == 0) {
			for (r = 0; r < n; r++) {
				if (fgets(line, sizeof line, f) == NULL)
					return -1;
				lineno++;
				for (c = 0; c < n; c++) {
					if (line[c] >= '1' && line[c] <= '0' + n)
						fixedv[r * n + c] = line[c] - '0';
					else if (line[c] != '.')
						return lineno;
				}
			}
			continue;
		}
		opc = 0;
		if (sscanf(line, "%c %d%c", &lab, &val, &opc) < 2 ||
		    strchr(labels, lab) == NULL || rootof[(int)lab] < 0)
			return lineno;
		root = rootof[(int)lab];
		clueval[root] = val;
		clueop[root] = opc == '+' ? ADD : opc == '-' ? SUB :
		    opc == '*' ? MUL : opc == '/' ? DIV : NONE;
		if (opc != 0 && opc != '\n' && clueop[root] == NONE)
			return lineno;
		if (clueop[root] == NONE && cnm[root] != 1)
			return lineno;
		if ((clueop[root] == SUB || clueop[root] == DIV) &&
		    cnm[root] != 2)
			return lineno;
	}
	if (n == 0)
		return -1;
	for (i = 0; i < n * n; i++)
		if (cage[i] < 0 || (cage[i] == i && clueop[i] < 0))
			return -1;
	return 0;
}
#endif

int
main(int argc, char **argv)
{
	int k;

#ifdef HOSTBUILD
	if (argc > 1 && strcmp(argv[1], "--count") == 0) {
		FILE *f;
		int limit = argc > 3 ? atoi(argv[3]) : 1000, bad;

		if (argc < 3 || (f = fopen(argv[2], "r")) == NULL) {
			fprintf(stderr, "usage: keen --count FILE [limit]\n");
			return 2;
		}
		bad = loadpuzzle(f);
		fclose(f);
		if (bad) {
			fprintf(stderr, "%s: bad puzzle at line %d\n", argv[2], bad);
			return 2;
		}
		printf("solutions %d\n", count_solutions(limit));
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "--dump") == 0) {
		argv++;
		argc--;
		n = argc > 1 ? atoi(argv[1]) : 5;
		if (n < 3) n = 3;
		if (n > MAXN) n = MAXN;
		srand(argc > 2 ? (unsigned)atoi(argv[2]) : (unsigned)time(NULL));
		if (getenv("KEEN_NOUNIQUE") != NULL) {
			gen_latin();
			gen_cages();
			gen_clues();
		} else
			gen_puzzle();
		dumppuzzle();
		return 0;
	}
#endif

	n = argc > 1 ? atoi(argv[1]) : 5;
	if (n < 3) n = 3;
	if (n > MAXN) n = MAXN;
	srand(argc > 2 ? (unsigned)atoi(argv[2]) : (unsigned)time(NULL));

	gen_puzzle();
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
