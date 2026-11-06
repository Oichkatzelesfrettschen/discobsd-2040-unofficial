/*
 * Host test of the fourfall rules. Includes ../fourfall.c with
 * FOURFALL_LOGIC so the same text that ships is the text tested.
 *
 *   logic_test [vectors.txt]
 *
 * Runs every line of tests/vectors.txt (SHAPE, COLL, KICK, CLEAR, SCORE,
 * LEVEL, GRAV, BAG, DROP, TOPOUT, SPAWN, SCREEN, NEXT, GLYPH, PLUMB,
 * REPLAY), the six
 * property checks, and checks of the gravity schedule, the lock timer
 * and the queue that need no vector. Prints the counts and exits
 * nonzero on any mismatch, on an unknown vector line, or when fewer
 * than EXPECTED_VECTORS vectors ran.
 */
#define FOURFALL_LOGIC
#include "../fourfall.c"

#include <stdio.h>

#define EXPECTED_VECTORS 161

static int nvec, nvec_bad, nchk, nchk_bad;

#define CHECK(cond, ...) do { \
	nchk++; \
	if (!(cond)) { \
		nchk_bad++; \
		if (nchk_bad <= 30) { \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} \
} while (0)

static char curid[64];

/* Record one vector verdict. */
static void
vec(int ok, const char *what)
{
	nvec++;
	if (!ok) {
		nvec_bad++;
		printf("FAIL vector %s: %s\n", curid, what);
	}
}

/* ---- vector line parsing ----------------------------------------- */

/* The text after "key" in s when key starts s or follows a space. */
static const char *
kv(const char *s, const char *key)
{
	size_t n = strlen(key);
	const char *p = s;

	while ((p = strstr(p, key)) != NULL) {
		if (p == s || p[-1] == ' ' || p[-1] == '(')
			return p + n;
		p++;
	}
	return NULL;
}

static int
kv_int(const char *s, const char *key, int *out)
{
	const char *p = kv(s, key);
	char *e;
	long v;

	if (!p)
		return 0;
	v = strtol(p, &e, 10);
	if (e == p)
		return 0;
	*out = (int)v;
	return 1;
}

/* Board token: "-" or rows joined by '/', last row is board row FF_ROWS - 1. */
static int
parse_board(const char *s, ff_board b, const char **end)
{
	int rows = 0, r, c;
	const char *p = s, *q;

	memset(b, 0, sizeof(ff_board));
	if (*p == '-' && (p[1] == 0 || p[1] == ' ')) {
		if (end)
			*end = p + 1;
		return 0;
	}
	for (q = p; *q && *q != ' '; q++)
		if (*q == '/')
			rows++;
	rows++;
	if (rows > FF_ROWS)
		return -1;
	for (r = FF_ROWS - rows; r < FF_ROWS; r++) {
		for (c = 0; c < FF_COLS; c++, p++) {
			if (*p == '.')
				b[r][c] = 0;
			else if (*p >= '1' && *p <= '7')
				b[r][c] = (unsigned char)(*p - '0');
			else
				return -1;
		}
		if (r < FF_ROWS - 1) {
			if (*p != '/')
				return -1;
			p++;
		}
	}
	if (end)
		*end = p;
	return 0;
}

static int
board_after_arrow(const char *line, ff_board b)
{
	const char *p = kv(line, "board=");

	if (!p)
		return -1;
	return parse_board(p, b, NULL);
}

static int
type_of(const char *s, const char *key)
{
	const char *p = kv(s, key);

	return p ? ff_type_of_letter(*p) : 0;
}

static const char *
rhs_of(const char *line)
{
	const char *p = strstr(line, "->");

	return p ? p + 2 : NULL;
}

/* Read "type= rot= row= col=" from the left-hand side. */
static int
read_state(const char *lhs, struct ff_piece *p)
{
	p->type = type_of(lhs, "type=");
	return p->type && kv_int(lhs, "rot=", &p->rot) &&
	    kv_int(lhs, "row=", &p->row) && kv_int(lhs, "col=", &p->col);
}

static int
cmp_int(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

/* ---- vector handlers --------------------------------------------- */

static void
v_shape(const char *line)
{
	int type = type_of(line, "type="), rot, cell[4][2], want[4], got[4], i;
	const char *p = kv(line, "cells ");
	int r, c, n = 0;

	if (!type || !kv_int(line, "rot=", &rot) || !p) {
		vec(0, "unparsed");
		return;
	}
	while (n < 4 && sscanf(p, "%d,%d", &r, &c) == 2) {
		want[n++] = r * 4 + c;
		while (*p && *p != ' ')
			p++;
		while (*p == ' ')
			p++;
	}
	ff_cells(type, rot, cell);
	for (i = 0; i < 4; i++)
		got[i] = cell[i][0] * 4 + cell[i][1];
	qsort(want, 4, sizeof(int), cmp_int);
	qsort(got, 4, sizeof(int), cmp_int);
	vec(n == 4 && memcmp(want, got, sizeof want) == 0, "cell set differs");
}

static void
v_coll(const char *line)
{
	ff_board b;
	struct ff_piece p;
	int want;
	const char *r = rhs_of(line);

	if (board_after_arrow(line, b) || !read_state(line, &p) || !r ||
	    sscanf(r, "%d", &want) != 1) {
		vec(0, "unparsed");
		return;
	}
	vec(ff_collides(b, &p) == want, "collision differs");
}

static void
v_kick(const char *line)
{
	ff_board b;
	struct ff_piece p;
	const char *r = rhs_of(line), *d = kv(line, "dir=");
	int ok, wr, wrow, wcol;

	if (board_after_arrow(line, b) || !read_state(line, &p) || !r || !d) {
		vec(0, "unparsed");
		return;
	}
	ok = ff_rotate_piece(b, &p, strncmp(d, "cw", 2) == 0 ? FF_CW : FF_CCW);
	if (strstr(r, "reject")) {
		vec(!ok, "rotation accepted, expected reject");
		return;
	}
	if (!kv_int(r, "rot=", &wr) || !kv_int(r, "row=", &wrow) ||
	    !kv_int(r, "col=", &wcol)) {
		vec(0, "unparsed result");
		return;
	}
	vec(ok && p.rot == wr && p.row == wrow && p.col == wcol,
	    "kicked state differs");
}

static void
v_clear(const char *line)
{
	ff_board b, want;
	const char *r = rhs_of(line), *p;
	int n, wn, rows, x, y, ok;

	if (strncmp(curid, "CLEAR-09", 8) == 0) {
		int cr, cc, cv, ar, ac, av, er;

		/* "row 1 full, cell (2,3)=2, ..." -> "cleared=1 cell (3,3) after=0 ..." */
		p = strstr(line, "row ");
		if (!p || sscanf(p, "row %d full", &er) != 1 ||
		    !(p = strstr(line, "cell (")) ||
		    sscanf(p, "cell (%d,%d)=%d", &cr, &cc, &cv) != 3 ||
		    !r || !(p = strstr(r, "cell (")) ||
		    sscanf(p, "cell (%d,%d) after=%d", &ar, &ac, &av) != 3) {
			vec(0, "unparsed");
			return;
		}
		memset(b, 0, sizeof b);
		memset(b[er], 1, FF_COLS);
		b[cr][cc] = (unsigned char)cv;
		n = ff_clear(b);
		kv_int(r, "cleared=", &wn);
		ok = n == wn && b[ar][ac] == av && b[cr][cc] == cv;
		for (x = 0; x < FF_COLS; x++)
			if (b[er][x])
				ok = 0;
		vec(ok, "single-cell board differs");
		return;
	}
	if (board_after_arrow(line, b) || !r || !kv_int(r, "cleared=", &wn) ||
	    !(p = strstr(r, "bottom ")) || sscanf(p, "bottom %d rows", &rows) != 1 ||
	    !(p = strstr(r, "after=")) || parse_board(p + 6, want, NULL)) {
		vec(0, "unparsed");
		return;
	}
	n = ff_clear(b);
	ok = n == wn;
	for (y = 0; y < FF_ROWS; y++)
		for (x = 0; x < FF_COLS; x++)
			if (b[y][x] != want[y][x])
				ok = 0;
	/* want holds only the bottom rows; rows above must be empty. */
	(void)rows;
	vec(ok, "cleared count or board differs");
}

static void
v_score(const char *line)
{
	int level, n;
	long want;
	const char *r = rhs_of(line);

	if (!kv_int(line, "level=", &level) ||
	    !kv_int(line, "lines_cleared=", &n) || !r ||
	    sscanf(r, "%ld", &want) != 1) {
		vec(0, "unparsed");
		return;
	}
	vec((long)ff_points(level, n) == want, "points differ");
}

static void
v_level(const char *line)
{
	int start, lines, level, want;
	const char *r = rhs_of(line);

	if (strncmp(curid, "GRAV-", 5) == 0) {
		if (!kv_int(line, "level=", &level) || !r ||
		    sscanf(r, "%d ms", &want) != 1) {
			vec(0, "unparsed");
			return;
		}
		vec(ff_interval(level) == want, "interval differs");
		return;
	}
	if (!kv_int(line, "start=", &start) ||
	    !kv_int(line, "total_lines=", &lines) || !r ||
	    sscanf(r, " level %d", &want) != 1) {
		vec(0, "unparsed");
		return;
	}
	vec(ff_level_of(start, lines) == want, "level differs");
}

static int bag_script[8], bag_pos;

static int
scripted_draw(int m)
{
	int v = bag_script[bag_pos++];

	return v < m ? v : -1;	/* an out-of-range draw crashes the test visibly */
}

static void
v_bag(const char *line)
{
	const char *p = strstr(line, "(i=6..1)"), *r = rhs_of(line);
	int out[7], want[7], i, ok = 1, n = 0;

	if (!p || !r) {
		vec(0, "unparsed");
		return;
	}
	p += 8;
	while (n < 6) {
		char *e;
		long v = strtol(p, &e, 10);

		if (e == p)
			break;
		bag_script[n++] = (int)v;
		p = e;
	}
	for (i = 0, n = 0; r[i] && n < 7; i++)
		if (ff_type_of_letter(r[i]))
			want[n++] = ff_type_of_letter(r[i]);
	if (n != 7) {
		vec(0, "unparsed result");
		return;
	}
	bag_pos = 0;
	ff_bag(out, scripted_draw);
	for (i = 0; i < 7; i++)
		if (out[i] != want[i])
			ok = 0;
	vec(ok && bag_pos == 6, "bag order differs");
}

static void
v_drop(const char *line)
{
	ff_board b;
	struct ff_piece p;
	int want;
	const char *r = rhs_of(line);

	if (board_after_arrow(line, b) || !read_state(line, &p) || !r ||
	    !kv_int(r, "distance ", &want)) {
		vec(0, "unparsed");
		return;
	}
	vec(ff_drop_distance(b, &p) == want, "distance differs");
}

static void
v_topout(const char *line)
{
	const char *r = rhs_of(line), *p;
	ff_board b;
	struct ff_piece s;
	int cr, cc, want, type;

	if (!r) {
		vec(0, "unparsed");
		return;
	}
	if (strncmp(curid, "SPAWN-", 6) == 0) {
		int wrow, wcol;

		type = type_of(line, "type=");
		if (!type || !kv_int(r, "row=", &wrow) || !kv_int(r, "col=", &wcol)) {
			vec(0, "unparsed");
			return;
		}
		s = ff_spawn_state(type);
		vec(s.row == wrow && s.col == wcol && s.rot == 0 && s.type == type,
		    "spawn state differs");
		return;
	}
	if ((p = strstr(line, "occupied cell (")) != NULL) {
		if (sscanf(p, "occupied cell (%d,%d)", &cr, &cc) != 2 ||
		    !(p = strstr(line, "spawn ")) ||
		    !(type = ff_type_of_letter(p[6])) ||
		    !kv_int(r, "collides=", &want)) {
			vec(0, "unparsed");
			return;
		}
		memset(b, 0, sizeof b);
		b[cr][cc] = 1;
		s = ff_spawn_state(type);
		vec(ff_collides(b, &s) == want, "spawn collision differs");
		return;
	}
	if ((p = strstr(line, "lock ")) != NULL) {
		int rot, row, col;

		type = ff_type_of_letter(p[5]);
		if (!type || !kv_int(p, "rot=", &rot) || !kv_int(p, "row=", &row) ||
		    !kv_int(p, "col=", &col) || !kv_int(r, "lock_out=", &want)) {
			vec(0, "unparsed");
			return;
		}
		memset(b, 0, sizeof b);
		s.type = type;
		s.rot = rot;
		s.row = row;
		s.col = col;
		vec(ff_lock_cells(b, &s) == want, "lock-out flag differs");
		return;
	}
	vec(0, "unknown TOPOUT form");
}

static void
v_screen(const char *line)
{
	const char *r = rhs_of(line);
	int row, col, tr, tc, wr, wc0, wc1, wr1, ok;

	if (!r) {
		vec(0, "unparsed");
		return;
	}
	if (strstr(r, "not drawn")) {
		if (!kv_int(line, "row=", &row) || !kv_int(line, "col=", &col)) {
			vec(0, "unparsed");
			return;
		}
		vec(!ff_board_pos(row, col, &tr, &tc), "hidden cell drawn");
		return;
	}
	if (strncmp(curid, "SCREEN-", 7) == 0) {
		const char *p = strstr(r, "terminal row ");

		if (!kv_int(line, "row=", &row) || !kv_int(line, "col=", &col) ||
		    !p || sscanf(p, "terminal row %d, columns %d-%d", &wr, &wc0, &wc1) != 3) {
			vec(0, "unparsed");
			return;
		}
		ok = ff_board_pos(row, col, &tr, &tc);
		vec(ok && tr == wr && tc == wc0 && tc + 1 == wc1, "position differs");
		return;
	}
	{
		/* NEXT: bounding box of the rot-0 cells in a preview slot. */
		int type = type_of(line, "type="), cell[4][2], i, minr = 99, maxr = -1;
		int minc = 999, maxc = -1, slot;
		const char *p = strstr(r, "terminal row");

		if (!type || !p || !kv_int(line, "slot=", &slot)) {
			vec(0, "unparsed");
			return;
		}
		if (sscanf(p, "terminal rows %d-%d, columns %d-%d", &wr, &wr1, &wc0, &wc1) != 4) {
			if (sscanf(p, "terminal row %d, columns %d-%d", &wr, &wc0, &wc1) != 3) {
				vec(0, "unparsed result");
				return;
			}
			wr1 = wr;
		}
		ff_cells(type, 0, cell);
		for (i = 0; i < 4; i++) {
			ff_next_pos(slot, cell[i][0], cell[i][1], &tr, &tc);
			if (tr < minr)
				minr = tr;
			if (tr > maxr)
				maxr = tr;
			if (tc < minc)
				minc = tc;
			if (tc + 1 > maxc)
				maxc = tc + 1;
		}
		vec(minr == wr && maxr == wr1 && minc == wc0 && maxc == wc1,
		    "preview position differs");
	}
}

static void
v_glyph(const char *line)
{
	int type = type_of(line, "type=");
	const char *r = rhs_of(line);

	if (!type || !r) {
		vec(0, "unparsed");
		return;
	}
	while (*r == ' ')
		r++;
	vec(strlen(r) == 2 && memcmp(r, ff_glyph[type], 2) == 0 &&
	    ff_glyph[type][2] == 0, "glyph differs");
}

static void
v_plumb(const char *line)
{
	ff_board b;
	struct ff_piece p;
	const char *r = rhs_of(line);
	int col, want_n, want_first, n, first;

	if (board_after_arrow(line, b) || !read_state(line, &p) || !r ||
	    !kv_int(line, "plumb_col=", &col) || !kv_int(r, "count=", &want_n)) {
		vec(0, "unparsed");
		return;
	}
	n = ff_plumb(b, &p, col, &first);
	if (kv_int(r, "first=", &want_first) && n > 0)
		vec(n == want_n && first == want_first, "plumb line differs");
	else
		vec(n == want_n, "plumb count differs");
}

static void
press(struct ff_game *g, const char *name, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (strcmp(name, "left") == 0)
			ff_move(g, -1);
		else if (strcmp(name, "right") == 0)
			ff_move(g, 1);
		else if (strcmp(name, "up") == 0)
			ff_rotate(g, FF_CW);
		else if (strcmp(name, "down") == 0)
			ff_soft_drop(g);
		else if (strcmp(name, "space") == 0)
			ff_hard_drop(g);
		else {
			printf("FAIL vector %s: unknown key %s\n", curid, name);
			nvec_bad++;
		}
	}
}

static void
v_replay(const char *line)
{
	const char *p = strstr(line, "sequence "), *k = strstr(line, "key"), *r = rhs_of(line);
	char script[32], tok[32];
	struct ff_game g;
	int ns = 0, n, w, ok = 1;

	if (!p || !k || !r) {
		vec(0, "unparsed");
		return;
	}
	for (p += 9; *p && *p != ';'; p++)
		if (ff_type_of_letter(*p) && (p[1] == ' ' || p[1] == ';') && ns < 31)
			script[ns++] = *p;
	script[ns] = 0;
	k = strchr(k, ':');
	if (!k || !ns) {
		vec(0, "unparsed keys");
		return;
	}
	ff_new(&g, 1, script, NULL);
	for (k++; k < r - 2;) {
		int len = 0;

		while (*k == ' ' || *k == ',')
			k++;
		if (k >= r - 2)
			break;
		while (k < r - 2 && *k != ',' && *k != ' ' && len < 30)
			tok[len++] = *k++;
		tok[len] = 0;
		n = 1;
		while (*k == ' ')
			k++;
		if (*k == 'x') {
			n = atoi(k + 1);
			while (*k && *k != ',' && k < r - 2)
				k++;
		}
		press(&g, tok, n);
	}
	if (kv_int(r, "score ", &w))
		ok &= (int)g.score == w;
	if (kv_int(r, "lines ", &w))
		ok &= g.lines == w;
	if (kv_int(r, "level ", &w))
		ok &= g.level == w;
	if (kv_int(r, "game_over ", &w))
		ok &= g.over == w;
	if ((p = strstr(r, "bottom row after = ")) != NULL) {
		int c;

		p += 19;
		for (c = 0; c < FF_COLS; c++) {
			int want = p[c] == '.' ? 0 : p[c] - '0';

			ok &= g.board[FF_ROWS - 1][c] == want;
		}
	}
	if ((p = strstr(r, "rows [")) != NULL) {
		int col, rowsel[8], nr = 0, x, y, cnt = 0;
		const char *c = strstr(r, "column [");
		char *e;

		col = c ? atoi(c + 8) : -1;
		p += 6;
		while (nr < 8) {
			long v = strtol(p, &e, 10);

			if (e == p)
				break;
			rowsel[nr++] = (int)v;
			p = e;
			while (*p == ',' || *p == ' ')
				p++;
		}
		for (y = 0; y < FF_ROWS; y++)
			for (x = 0; x < FF_COLS; x++)
				if (g.board[y][x])
					cnt++;
		ok &= cnt == nr;
		for (y = 0; y < nr; y++)
			ok &= col >= 0 && g.board[rowsel[y]][col] != 0;
	}
	vec(ok, "replay result differs");
}

static void
run_vectors(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[512], *sp;

	if (!f) {
		printf("FAIL cannot open %s\n", path);
		nvec_bad++;
		return;
	}
	while (fgets(line, sizeof line, f)) {
		size_t n = strlen(line);

		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (!n || line[0] == '#')
			continue;
		sp = strchr(line, ' ');
		if (!sp || (size_t)(sp - line) >= sizeof curid) {
			vec(0, "malformed line");
			continue;
		}
		memcpy(curid, line, (size_t)(sp - line));
		curid[sp - line] = 0;
		while (*sp == ' ')
			sp++;
		if (!strncmp(curid, "SHAPE-", 6))
			v_shape(sp);
		else if (!strncmp(curid, "COLL-", 5))
			v_coll(sp);
		else if (!strncmp(curid, "KICK-", 5))
			v_kick(sp);
		else if (!strncmp(curid, "CLEAR-", 6))
			v_clear(sp);
		else if (!strncmp(curid, "SCORE-", 6))
			v_score(sp);
		else if (!strncmp(curid, "LEVEL-", 6) || !strncmp(curid, "GRAV-", 5))
			v_level(sp);
		else if (!strncmp(curid, "BAG-", 4))
			v_bag(sp);
		else if (!strncmp(curid, "DROP-", 5))
			v_drop(sp);
		else if (!strncmp(curid, "TOPOUT-", 7) || !strncmp(curid, "SPAWN-", 6))
			v_topout(sp);
		else if (!strncmp(curid, "SCREEN-", 7) || !strncmp(curid, "NEXT-", 5))
			v_screen(sp);
		else if (!strncmp(curid, "GLYPH-", 6))
			v_glyph(sp);
		else if (!strncmp(curid, "PLUMB-", 6))
			v_plumb(sp);
		else if (!strncmp(curid, "REPLAY-", 7))
			v_replay(sp);
		else
			vec(0, "unknown vector id");
	}
	fclose(f);
}

/* ---- property checks --------------------------------------------- */

static unsigned long rs = 88172645463325252UL;

static unsigned
xr(void)
{
	rs ^= rs << 13;
	rs ^= rs >> 7;
	rs ^= rs << 17;
	return (unsigned)(rs >> 16);
}

static int
nonzero(ff_board b)
{
	int r, c, n = 0;

	for (r = 0; r < FF_ROWS; r++)
		for (c = 0; c < FF_COLS; c++)
			n += b[r][c] != 0;
	return n;
}

static void
prop_rotation_and_shapes(void)
{
	int t, rot, i, j, k, cell[4][2], c0[4][2], n, r, c, tt;

	for (t = 1; t <= FF_NTYPES; t++) {
		n = ff_box[t];
		for (rot = 0; rot < 4; rot++) {
			ff_cells(t, rot, cell);
			for (i = 0; i < 4; i++) {
				CHECK(cell[i][0] >= 0 && cell[i][0] < n &&
				    cell[i][1] >= 0 && cell[i][1] < n,
				    "type %d rot %d cell outside box", t, rot);
				for (j = i + 1; j < 4; j++)
					CHECK(cell[i][0] != cell[j][0] ||
					    cell[i][1] != cell[j][1],
					    "type %d rot %d repeated cell", t, rot);
			}
		}
		/* property 1: four clockwise turns return orientation 0 */
		ff_cells(t, 0, c0);
		for (i = 0; i < 4; i++) {
			r = c0[i][0];
			c = c0[i][1];
			for (k = 0; k < 4; k++) {
				tt = r;
				r = c;
				c = n - 1 - tt;
			}
			CHECK(r == c0[i][0] && c == c0[i][1],
			    "type %d: four turns do not return", t);
		}
		/* clockwise then counterclockwise returns the start, any state */
		for (rot = 0; rot < 4; rot++) {
			ff_board b;
			struct ff_piece p;

			memset(b, 0, sizeof b);
			p.type = t;
			p.rot = rot;
			p.row = 8;
			p.col = 3;
			CHECK(ff_rotate_piece(b, &p, FF_CW) && p.rot == ((rot + 1) & 3),
			    "type %d cw from %d", t, rot);
			CHECK(ff_rotate_piece(b, &p, FF_CCW) && p.rot == rot &&
			    p.row == 8 && p.col == 3, "type %d cw then ccw from %d", t, rot);
		}
	}
}

/*
 * Every orientation-0 piece lies in box rows 0 and 1, so the two-row
 * preview boxes of the slots do not overlap, and each fits the panel.
 */
static void
prop_preview_boxes(void)
{
	int t, i, s, cell[4][2], tr, tc, rows[FF_NEXT_SLOTS][2];
	struct ff_game g;

	CHECK(FF_NEXT_SLOTS == 2, "%d preview slots, want 2", FF_NEXT_SLOTS);
	ff_new(&g, 1, NULL, ff_rnd);
	CHECK(g.qlen >= FF_NEXT_SLOTS, "queue holds %d, preview shows %d", g.qlen, FF_NEXT_SLOTS);
	for (s = 0; s < FF_NEXT_SLOTS; s++) {
		ff_next_pos(s, 0, 0, &tr, &tc);
		rows[s][0] = tr;
		ff_next_pos(s, 1, 0, &tr, &tc);
		rows[s][1] = tr;
		CHECK(rows[s][1] == rows[s][0] + 1, "slot %d box not two rows", s);
		if (s)
			CHECK(rows[s][0] > rows[s - 1][1], "slot %d overlaps slot %d", s, s - 1);
	}
	for (t = 1; t <= FF_NTYPES; t++) {
		ff_cells(t, 0, cell);
		for (i = 0; i < 4; i++) {
			CHECK(cell[i][0] >= 0 && cell[i][0] <= 1, "type %d spawn cell row %d", t, cell[i][0]);
			ff_next_pos(0, cell[i][0], cell[i][1], &tr, &tc);
			CHECK(tc + 1 <= 52, "type %d preview reaches column %d", t, tc + 1);
		}
		CHECK(strlen(ff_glyph[t]) == 2, "glyph of type %d is not two characters", t);
		for (s = 1; s < t; s++)
			CHECK(strcmp(ff_glyph[s], ff_glyph[t]) != 0, "glyphs %d and %d equal", s, t);
	}
}

/* A spawned piece is centered: the free columns left and right differ by at most one. */
static void
prop_spawn_centered(void)
{
	int t, i, cell[4][2], lo, hi;
	struct ff_piece p;

	for (t = 1; t <= FF_NTYPES; t++) {
		p = ff_spawn_state(t);
		ff_cells(t, 0, cell);
		lo = 99;
		hi = -1;
		for (i = 0; i < 4; i++) {
			if (p.col + cell[i][1] < lo)
				lo = p.col + cell[i][1];
			if (p.col + cell[i][1] > hi)
				hi = p.col + cell[i][1];
		}
		CHECK(lo - (FF_COLS - 1 - hi) <= 1 && (FF_COLS - 1 - hi) - lo <= 1,
		    "type %d spawn columns %d..%d not centered", t, lo, hi);
	}
}

static void
fill_random(ff_board b, unsigned density)
{
	int r, c;

	for (r = 0; r < FF_ROWS; r++)
		for (c = 0; c < FF_COLS; c++)
			b[r][c] = (xr() % 100 < density) ? (unsigned char)(1 + xr() % 7) : 0;
}

/* The O piece enumerates its cells in one fixed order in every orientation. */
static void
check_o_enumeration(void)
{
	static const int want[4][2] = { { 1, 1 }, { 0, 0 }, { 1, 0 }, { 0, 1 } };
	int rot, i, cell[4][2];

	for (rot = 0; rot < 4; rot++) {
		ff_cells(FF_O, rot, cell);
		for (i = 0; i < 4; i++)
			CHECK(cell[i][0] == want[i][0] && cell[i][1] == want[i][1],
			    "O rot %d cell %d is %d,%d", rot, i, cell[i][0], cell[i][1]);
	}
}

static void
prop_line_clear(void)
{
	int iter, r, c, w, n, before, full;
	ff_board b, a, kept;

	for (iter = 0; iter < 10000; iter++) {
		fill_random(b, 10 + xr() % 80);
		for (r = 0; r < FF_ROWS; r++)
			if (xr() % 3 == 0)
				for (c = 0; c < FF_COLS; c++)
					if (!b[r][c])
						b[r][c] = (unsigned char)(1 + xr() % 7);
		memcpy(a, b, sizeof a);
		before = nonzero(a);
		w = 0;
		memset(kept, 0, sizeof kept);
		for (r = 0; r < FF_ROWS; r++) {
			if (ff_rowfull(b[r]))
				continue;
			memcpy(kept[w++], b[r], FF_COLS);
		}
		n = ff_clear(a);
		CHECK(n == FF_ROWS - w, "iteration %d count %d vs %d", iter, n, FF_ROWS - w);
		CHECK(nonzero(a) == before - FF_COLS * n, "iteration %d cell count", iter);
		full = 0;
		for (r = 0; r < FF_ROWS; r++)
			full += ff_rowfull(a[r]);
		CHECK(full == 0, "iteration %d full row left", iter);
		/* surviving rows, in order, at the bottom; empty rows on top */
		CHECK(memcmp(a[FF_ROWS - w], kept, (size_t)w * FF_COLS) == 0,
		    "iteration %d row order", iter);
		for (r = 0; r < FF_ROWS - w; r++)
			for (c = 0; c < FF_COLS; c++)
				CHECK(a[r][c] == 0, "iteration %d top not empty", iter);
	}
}

static void
prop_hard_drop(void)
{
	int made = 0, d, steps;
	ff_board b;
	struct ff_piece p, t;

	while (made < 10000) {
		fill_random(b, xr() % 30);
		p.type = 1 + (int)(xr() % 7);
		p.rot = (int)(xr() % 4);
		p.row = (int)(xr() % (FF_ROWS + 1)) - 1;
		p.col = (int)(xr() % (FF_COLS + 3)) - 2;
		if (ff_collides(b, &p))
			continue;
		made++;
		steps = 0;
		t = p;
		for (;;) {
			t.row++;
			if (ff_collides(b, &t))
				break;
			steps++;
		}
		d = ff_drop_distance(b, &p);
		CHECK(d == steps, "type %d rot %d row %d col %d: %d vs %d",
		    p.type, p.rot, p.row, p.col, d, steps);
	}
}

static void
prop_bag(void)
{
	int seed, bag, i, t, pos[8], k, n, worst_gap = 0;
	int seq[7000 + 8], off;
	struct ff_game g;

	for (seed = 1; seed <= 20; seed++) {
		srand((unsigned)seed);
		ff_new(&g, 1, NULL, ff_rnd);
		n = 0;
		seq[n++] = g.cur.type;
		for (; n < 7000; n++) {
			CHECK(g.qlen >= 2, "seed %d: queue holds %d", seed, g.qlen);
			ff_spawn(&g);
			seq[n] = g.cur.type;
		}
		for (bag = 0; bag + 1 <= 1000; bag++) {
			int seen[8] = { 0 };

			for (i = 0; i < 7; i++)
				seen[seq[bag * 7 + i]]++;
			for (t = 1; t <= 7; t++)
				CHECK(seen[t] == 1, "seed %d bag %d type %d x%d", seed, bag, t, seen[t]);
		}
		for (t = 1; t <= 7; t++)
			pos[t] = -1;
		for (i = 0; i < 7000; i++) {
			t = seq[i];
			if (pos[t] >= 0) {
				k = i - pos[t];
				if (k > worst_gap)
					worst_gap = k;
				CHECK(k <= 13, "seed %d type %d gap %d", seed, t, k);
			}
			pos[t] = i;
		}
	}
	CHECK(worst_gap <= 13, "worst gap %d", worst_gap);
	printf("  bag: worst same-type gap over seeds 1..20 = %d (limit 13)\n", worst_gap);

	/* property 6: fairness over 7000 bags per seed, 15 percent bound */
	{
		double worst = 0;

		for (seed = 1; seed <= 20; seed++) {
			static int cnt[7][8];
			int p2;

			memset(cnt, 0, sizeof cnt);
			srand((unsigned)seed);
			for (bag = 0; bag < 7000; bag++) {
				int out[7];

				ff_bag(out, ff_rnd);
				for (p2 = 0; p2 < 7; p2++)
					cnt[p2][out[p2]]++;
			}
			for (p2 = 0; p2 < 7; p2++)
				for (t = 1; t <= 7; t++) {
					double dev = (cnt[p2][t] - 1000) / 10.0;

					if (dev < 0)
						dev = -dev;
					if (dev > worst)
						worst = dev;
					CHECK(dev <= 15.0, "seed %d pos %d type %d count %d", seed, p2, t, cnt[p2][t]);
				}
		}
		printf("  bag: worst position/type deviation over seeds 1..20 = %.1f percent (limit 15)\n", worst);
	}
	(void)off;
}

/* ---- gravity, lock timer, queue and state checks ------------------ */

static void
state_checks(void)
{
	struct ff_game g, h;
	int i, k, d;

	/* gravity: 800 ms per row at level 1, then the next step */
	ff_new(&g, 1, "O", NULL);
	ff_advance(&g, 799);
	CHECK(g.cur.row == 0, "row %d after 799 ms", g.cur.row);
	ff_advance(&g, 1);
	CHECK(g.cur.row == 1, "row %d after 800 ms", g.cur.row);
	ff_advance(&g, 800 * 3);
	CHECK(g.cur.row == 4, "row %d after 3200 ms", g.cur.row);
	CHECK(ff_deadline(&g) == 800, "deadline %d", ff_deadline(&g));
	ff_advance(&g, 300);
	CHECK(ff_deadline(&g) == 500, "deadline %d after 300 ms", ff_deadline(&g));

	/* a higher level steps faster: level 9 is 210 ms */
	ff_new(&g, 9, "O", NULL);
	ff_advance(&g, 210 * 3);
	CHECK(g.cur.row == 3, "level 9 row %d", g.cur.row);

	/* lock after 500 ms grounded: soft drop to the floor first */
	ff_new(&g, 1, "O", NULL);
	for (i = 0; i < FF_ROWS - 2; i++)
		ff_soft_drop(&g);
	CHECK(g.cur.row == FF_ROWS - 2 && g.locks == 0, "grounded row %d locks %d", g.cur.row, g.locks);
	CHECK(g.score == FF_ROWS - 2, "soft drop score %lu", g.score);
	CHECK(g.lock_on, "lock timer not running");
	ff_advance(&g, 499);
	CHECK(g.locks == 0, "locked early");
	ff_advance(&g, 1);
	CHECK(g.locks == 1 && g.cur.row == 0 && g.board[FF_ROWS - 1][5] == FF_O &&
	    g.board[FF_ROWS - 1][4] == 0 && g.board[FF_ROWS - 1][7] == 0,
	    "no lock at 500 ms (locks %d)", g.locks);

	/* a soft drop on the ground locks and adds nothing */
	ff_new(&g, 1, "O", NULL);
	for (i = 0; i < FF_ROWS - 2; i++)
		ff_soft_drop(&g);
	ff_soft_drop(&g);
	CHECK(g.locks == 1 && g.score == FF_ROWS - 2, "grounded soft drop: locks %d score %lu", g.locks, g.score);

	/* ten restarts, then the eleventh adjustment leaves the timer running */
	ff_new(&g, 1, "O", NULL);
	for (i = 0; i < FF_ROWS - 2; i++)
		ff_soft_drop(&g);
	for (k = 0; k < 10; k++) {
		ff_advance(&g, 400);
		CHECK(ff_move(&g, k % 2 ? 1 : -1), "move %d refused", k);
	}
	CHECK(g.lock_resets == 10 && g.locks == 0, "resets %d locks %d", g.lock_resets, g.locks);
	ff_advance(&g, 400);
	CHECK(ff_move(&g, 1), "eleventh move refused");
	CHECK(g.lock_resets == 10, "eleventh adjustment counted");
	ff_advance(&g, 99);
	CHECK(g.locks == 0, "locked 1 ms early after the eleventh adjustment");
	ff_advance(&g, 1);
	CHECK(g.locks == 1, "no lock 100 ms after the eleventh adjustment");

	/* an adjustment that leaves the piece ungrounded stops the timer */
	ff_new(&g, 1, "O", NULL);
	memset(g.board[FF_ROWS - 1], 1, 6);	/* a ledge under columns 0-5 */
	for (i = 0; i < FF_ROWS - 3; i++)
		ff_soft_drop(&g);
	CHECK(g.cur.row == FF_ROWS - 3 && g.lock_on, "on the ledge: row %d", g.cur.row);
	ff_advance(&g, 300);
	CHECK(ff_move(&g, 1), "slide off the ledge refused");
	CHECK(!g.lock_on, "timer still running after sliding off the ledge");

	/* hard drop: two points a row, immediate lock */
	ff_new(&g, 1, "I", NULL);
	ff_hard_drop(&g);
	CHECK(g.score == 2 * (FF_ROWS - 2) && g.locks == 1, "hard drop score %lu", g.score);

	/* score saturates at 99,999,999 */
	ff_new(&g, 1, "I", NULL);
	g.score = 99999990UL;
	ff_hard_drop(&g);
	CHECK(g.score == 99999999UL, "saturation %lu", g.score);

	/* a clear scores at the level in effect before the level update */
	ff_new(&g, 3, "I", NULL);
	memset(g.board[FF_ROWS - 1], 1, 4);	/* columns 0-3 and 8-11 */
	memset(&g.board[FF_ROWS - 1][8], 1, 4);
	ff_hard_drop(&g);		/* I at columns 4-7 completes the bottom row */
	CHECK(g.lines == 1 && g.score == 300 + 44 && g.level == 3,
	    "clear at level 3: lines %d score %lu level %d", g.lines, g.score, g.level);
	ff_new(&g, 1, "I", NULL);
	g.lines = 9;
	memset(g.board[FF_ROWS - 1], 1, 4);
	memset(&g.board[FF_ROWS - 1][8], 1, 4);
	ff_hard_drop(&g);
	CHECK(g.lines == 10 && g.score == 100 + 44 && g.level == 2,
	    "level step: lines %d score %lu level %d", g.lines, g.score, g.level);

	/* the flash record: rows cleared by the last lock, board before collapse */
	CHECK(g.flash_mask == 1UL << (FF_ROWS - 1), "flash mask %#lx", g.flash_mask);
	CHECK(ff_rowfull(g.flash_board[FF_ROWS - 1]) &&
	    g.flash_board[FF_ROWS - 1][5] == FF_I, "flash board lacks the cleared row");
	CHECK(!g.board[FF_ROWS - 1][0], "board row not collapsed under the flash record");
	ff_new(&g, 1, "I", NULL);
	ff_hard_drop(&g);
	CHECK(g.flash_mask == 0, "flash mask set by a lock that cleared nothing");
	ff_new(&g, 1, "I", NULL);
	memset(g.board[FF_ROWS - 1], 1, 4);
	memset(&g.board[FF_ROWS - 1][8], 1, 4);
	memset(g.board[FF_ROWS - 2], 1, 4);
	ff_hard_drop(&g);
	CHECK(g.flash_mask == 1UL << (FF_ROWS - 1), "flash mask %#lx with a partial row above", g.flash_mask);
	ff_new(&g, 1, "I", NULL);
	CHECK(g.flash_mask == 0, "flash mask survives ff_new");

	/* queue: cyclic script, and two or more pieces at all times */
	ff_new(&g, 1, "TZ", NULL);
	for (i = 0; i < 20; i++) {
		CHECK(g.cur.type == (i % 2 ? FF_Z : FF_T), "script piece %d is %d", i, g.cur.type);
		CHECK(g.qlen >= 2, "script queue %d", g.qlen);
		ff_spawn(&g);
	}

	/* stepping equivalence: one long advance equals many short ones */
	for (k = 0; k < 200; k++) {
		int ms = 1 + (int)(xr() % 5000), moves = (int)(xr() % 6);

		srand((unsigned)k);
		ff_new(&g, 1 + k % 9, NULL, ff_rnd);
		srand((unsigned)k);
		ff_new(&h, 1 + k % 9, NULL, ff_rnd);
		for (i = 0; i < moves; i++) {
			ff_move(&g, i % 2 ? 1 : -1);
			ff_move(&h, i % 2 ? 1 : -1);
		}
		ff_advance(&g, ms);
		for (d = 0; d < ms; d++)
			ff_advance(&h, 1);
		CHECK(g.score == h.score && g.locks == h.locks && g.over == h.over &&
		    g.cur.row == h.cur.row && g.cur.col == h.cur.col &&
		    g.cur.type == h.cur.type && memcmp(g.board, h.board, sizeof g.board) == 0,
		    "advance(%d) differs from %d steps of 1 (k=%d)", ms, ms, k);
	}

	/* a stack to the top ends the game by top-out or lock-out */
	ff_new(&g, 1, "O", NULL);
	for (i = 0; i < 14 && !g.over; i++)
		ff_hard_drop(&g);
	CHECK(g.over, "tower did not end the game");
	ff_hard_drop(&g);
	CHECK(g.score == 264, "score changed after game over: %lu", g.score);
}

int
main(int argc, char **argv)
{
	const char *vectors = argc > 1 ? argv[1] : "tests/vectors.txt";

	setvbuf(stdout, NULL, _IOLBF, 0);
	run_vectors(vectors);
	printf("vectors: %d run, %d failed (expected %d)\n", nvec, nvec_bad, EXPECTED_VECTORS);

	prop_rotation_and_shapes();
	check_o_enumeration();
	prop_preview_boxes();
	prop_spawn_centered();
	prop_line_clear();
	prop_hard_drop();
	prop_bag();
	state_checks();
	printf("checks: %d run, %d failed\n", nchk, nchk_bad);

	if (nvec < EXPECTED_VECTORS) {
		printf("FAIL: only %d of %d vectors ran\n", nvec, EXPECTED_VECTORS);
		return 1;
	}
	if (nvec_bad || nchk_bad) {
		printf("fourfall logic: FAIL\n");
		return 1;
	}
	printf("fourfall logic: OK\n");
	return 0;
}
