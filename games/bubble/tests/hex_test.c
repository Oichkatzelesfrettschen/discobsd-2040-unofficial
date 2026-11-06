/*
 * Host test of the bubble rules. Includes ../bubble.c with BUBBLE_LOGIC so
 * the same text that ships is the text tested.
 *
 *   hex_test [vectors.txt]
 *
 * Runs every line of tests/vectors.txt (NEIGH, POP, TRAJ, DESC, REPLAY),
 * then property checks over random boards: adjacency is symmetric and
 * agrees with the overlap of terminal-column spans, a shot rests on an
 * empty cell next to a bubble or in the ceiling row, a pop leaves no group
 * of three, and anchoring leaves no bubble cut off from the ceiling.
 * Prints the counts and exits nonzero on any mismatch, on an unknown
 * vector line, or when fewer than EXPECTED_VECTORS vectors ran.
 *
 * Vector grammar: "ID COMMAND key=value ... -> key=value ...". A board is
 * ROWS rows of COLS characters ('.' empty, else the color digit) joined by
 * '/'; trailing empty rows may be left out.
 */
#define BUBBLE_LOGIC
#include "../bubble.c"

#define EXPECTED_VECTORS 111

static int nvec, nvec_bad, nchk, nchk_bad;
static char curid[64];

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

static void
vec(int ok, const char *what)
{
	nvec++;
	if (!ok) {
		nvec_bad++;
		printf("FAIL vector %s: %s\n", curid, what);
	}
}

/* The text after "key=" when key starts s or follows a space, else NULL. */
static const char *
kv(const char *s, const char *key)
{
	size_t n = strlen(key);
	const char *p = s;

	while ((p = strstr(p, key)) != NULL) {
		if ((p == s || p[-1] == ' ') && p[n] == '=')
			return p + n + 1;
		p++;
	}
	return NULL;
}

static int
kv_int(const char *s, const char *key, int dflt)
{
	const char *p = kv(s, key);

	return p ? atoi(p) : dflt;
}

/* Copy the value of key (up to a space or end) into out. */
static int
kv_str(const char *s, const char *key, char *out, size_t cap)
{
	const char *p = kv(s, key);
	size_t n = 0;

	if (!p)
		return 0;
	while (p[n] && p[n] != ' ' && n + 1 < cap) {
		out[n] = p[n];
		n++;
	}
	out[n] = 0;
	return 1;
}

static void
board_set(const char *s)
{
	int r = 0, c = 0;

	memset(grid, 0, sizeof grid);
	for (; *s && r < ROWS; s++) {
		if (*s == '/') {
			r++;
			c = 0;
		} else if (c < COLS) {
			grid[r * COLS + c++] = *s == '.' ? 0 : *s - '0';
		}
	}
}

static void
board_str(char *out)
{
	int r, c, last = -1, n = 0;

	for (r = 0; r < ROWS; r++)
		for (c = 0; c < COLS; c++)
			if (grid[r * COLS + c])
				last = r;
	for (r = 0; r <= last; r++) {
		if (r)
			out[n++] = '/';
		for (c = 0; c < COLS; c++) {
			int v = grid[r * COLS + c];

			out[n++] = v ? (char)('0' + v) : '.';
		}
	}
	if (last < 0)
		out[n++] = '-';
	out[n] = 0;
}

static void
setmode(const char *line)
{
	char m[8];

	hexmode = kv_str(line, "mode", m, sizeof m) && strcmp(m, "hex") == 0;
	stagger = kv_int(line, "stagger", 0);
	misses = kv_int(line, "misses", 0);
	score = 0;
}

static int
cmpint(const void *a, const void *b)
{
	return *(const int *)a - *(const int *)b;
}

static void
v_neigh(const char *line)
{
	int nb[6], n, i, r, c, got[6];
	char want[128], have[128];
	const char *w;

	setmode(line);
	w = kv(line, "cell");
	r = atoi(w);
	c = atoi(strchr(w, ',') + 1);
	n = neighbors(r * COLS + c, nb);
	memcpy(got, nb, sizeof got);
	qsort(got, (size_t)n, sizeof got[0], cmpint);
	have[0] = 0;
	for (i = 0; i < n; i++)
		snprintf(have + strlen(have), sizeof have - strlen(have), "%s%d,%d",
		    i ? ";" : "",
		    got[i] / COLS, got[i] % COLS);
	kv_str(line, "list", want, sizeof want);
	vec(strcmp(want, have) == 0 && n == kv_int(line, "n", -1), have);
}

static void
v_pop(const char *line)
{
	char b[160], want[160], buf[160];
	const char *w;
	int r, c, ok;

	setmode(line);
	testmode = 1;
	kv_str(line, "board", buf, sizeof buf);
	board_set(buf);
	w = kv(line, "at");
	r = atoi(w);
	c = atoi(strchr(w, ',') + 1);
	nextcolor = kv_int(line, "color", 1);
	ok = fire_at(r * COLS + c) == kv_int(line, "alive", 1);
	board_str(b);
	kv_str(line, "result", want, sizeof want);
	testmode = 0;
	vec(ok && strcmp(b, want) == 0 && score == kv_int(line, "score", -1) &&
	    misses == kv_int(line, "misses_after", 0) &&
	    stagger == kv_int(line, "stagger_after", stagger), b);
}

static void
v_traj(const char *line)
{
	char buf[160];
	int cell, want, w;

	setmode(line);
	buf[0] = 0;
	kv_str(line, "board", buf, sizeof buf);
	board_set(buf);
	aim = kv_int(line, "aim", 0);
	cell = trajectory();
	want = kv_int(line, "cell", -2);
	w = kv_int(line, "len", -1);
	{
		char msg[64];

		snprintf(msg, sizeof msg, "trajectory cell %d len %d", cell, pathlen);
		vec(cell == want && (w < 0 || pathlen == w), msg);
	}
	aim = 0;
}

static void
v_desc(const char *line)
{
	char b[160], want[160], buf[160];

	setmode(line);
	testmode = 1;
	kv_str(line, "board", buf, sizeof buf);
	board_set(buf);
	descend();
	board_str(b);
	kv_str(line, "result", want, sizeof want);
	vec(strcmp(b, want) == 0 && stagger == kv_int(line, "stagger_after", -1), b);
}

/* Fire the listed aims, reading shot colors from the script. */
static void
v_replay(const char *line)
{
	static char sc[64];
	char b[160], want[160], buf[160], aims[128];
	char *p;
	int alive = 1;

	setmode(line);
	testmode = 1;
	script = 0;
	if (kv_str(line, "colors", sc, sizeof sc))
		script = sc;
	scriptpos = 0;
	nextcolor = pickcolor();
	if (kv_str(line, "board", buf, sizeof buf))
		board_set(buf);
	else
		memset(grid, 0, sizeof grid);
	kv_str(line, "aims", aims, sizeof aims);
	for (p = aims; *p && alive; ) {
		aim = (int)strtol(p, &p, 10);
		if (*p == ',')
			p++;
		alive = fire();
	}
	aim = 0;
	board_str(b);
	kv_str(line, "result", want, sizeof want);
	vec(strcmp(b, want) == 0 && score == kv_int(line, "score", -1) &&
	    stagger == kv_int(line, "stagger_after", -1) &&
	    misses == kv_int(line, "misses_after", -1) &&
	    alive == kv_int(line, "alive", 1), b);
	script = 0;
	testmode = 0;
}

static void
run_vectors(const char *path)
{
	char line[512], cmd[16];
	FILE *f = fopen(path, "r");
	char *arrow, *p;

	if (!f) {
		printf("FAIL cannot open %s\n", path);
		nvec_bad++;
		return;
	}
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (!line[0] || line[0] == '#')
			continue;
		p = line;
		if (sscanf(p, "%63s %15s", curid, cmd) != 2) {
			printf("FAIL unparsable line: %s\n", line);
			nvec_bad++;
			continue;
		}
		/* Merge the inputs and the expectations into one key space. */
		arrow = strstr(line, " -> ");
		if (arrow)
			memcpy(arrow, "    ", 4);
		p = line + strlen(curid) + 1 + strlen(cmd);
		hexmode = 0;
		stagger = 0;
		if (!strcmp(cmd, "NEIGH"))
			v_neigh(p);
		else if (!strcmp(cmd, "POP"))
			v_pop(p);
		else if (!strcmp(cmd, "TRAJ"))
			v_traj(p);
		else if (!strcmp(cmd, "DESC"))
			v_desc(p);
		else if (!strcmp(cmd, "REPLAY"))
			v_replay(p);
		else {
			printf("FAIL unknown vector command %s\n", cmd);
			nvec_bad++;
		}
	}
	fclose(f);
}

/* ---- properties --------------------------------------------------- */

static unsigned long rng = 0x2545F4914F6CDD1DUL;

static unsigned
rnd(unsigned m)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (unsigned)((rng >> 11) % m);
}

/* Terminal-column span of cell (r, c): [lo, lo + 1]. */
static int
span_lo(int r, int c)
{
	return 2 * c + (shifted(r) ? 1 : 0);
}

static void
prop_adjacency(void)
{
	int mode, st, a, b, na, nb_, i, j, r, c, rr, cc, ok;
	int la[6], lb[6];

	for (mode = 0; mode < 2; mode++)
		for (st = 0; st < 2; st++) {
			hexmode = mode;
			stagger = st;
			for (a = 0; a < ROWS * COLS; a++) {
				r = a / COLS;
				c = a % COLS;
				if (c >= rowlen(r))
					continue;
				na = neighbors(a, la);
				CHECK(na <= (mode ? 6 : 4), "degree %d", na);
				for (i = 0; i < na; i++) {
					b = la[i];
					rr = b / COLS;
					cc = b % COLS;
					CHECK(cc < rowlen(rr), "neighbor off row");
					nb_ = neighbors(b, lb);
					ok = 0;
					for (j = 0; j < nb_; j++)
						ok |= lb[j] == a;
					CHECK(ok, "asymmetric %d %d", a, b);
					for (j = i + 1; j < na; j++)
						CHECK(la[j] != b, "duplicate");
				}
				if (!mode)
					continue;
				/* Hexagonal adjacency is overlap of spans in the
				 * rows beside this one, and neighbors in the row. */
				for (b = 0; b < ROWS * COLS; b++) {
					int adj, dr, lo;

					rr = b / COLS;
					cc = b % COLS;
					if (cc >= rowlen(rr) || b == a)
						continue;
					dr = rr - r;
					lo = span_lo(rr, cc) - span_lo(r, c);
					adj = (dr == 0 && (lo == 2 || lo == -2)) ||
					    ((dr == 1 || dr == -1) &&
					    (lo == 1 || lo == -1));
					ok = 0;
					for (j = 0; j < na; j++)
						ok |= la[j] == b;
					CHECK(ok == adj, "span adjacency %d %d", a, b);
				}
			}
		}
}

/* Size of the same-color group of cell, by a breadth-first walk of
 * neighbors() that shares no state with floodfill(). */
static int
group_size(int cell)
{
	int seen[ROWS * COLS], queue[ROWS * COLS], nb[6], head = 0, tail = 0;
	int n, k, cur;

	memset(seen, 0, sizeof seen);
	queue[tail++] = cell;
	seen[cell] = 1;
	while (head < tail) {
		cur = queue[head++];
		n = neighbors(cur, nb);
		for (k = 0; k < n; k++)
			if (!seen[nb[k]] && grid[nb[k]] == grid[cell]) {
				seen[nb[k]] = 1;
				queue[tail++] = nb[k];
			}
	}
	return tail;
}

static void
prop_shots(void)
{
	int iter, i, fill, mode, cell, nb[6], n, k, ok, a, size;
	long before;

	testmode = 1;
	for (iter = 0; iter < 4000; iter++) {
		hexmode = mode = (int)rnd(2);
		stagger = mode ? (int)rnd(2) : 0;
		misses = 0;
		memset(grid, 0, sizeof grid);
		fill = (int)rnd(40);
		for (i = 0; i < fill; i++) {
			int r = (int)rnd(8), c = (int)rnd((unsigned)COLS);

			if (c < rowlen(r))
				grid[r * COLS + c] = 1 + (int)rnd(NCOLORS);
		}
		anchor_and_drop();	/* start from a legal board */
		for (a = -AIMMAX; a <= AIMMAX; a++) {
			aim = a;
			cell = trajectory();
			if (cell < 0) {
				CHECK(grid[cellat(FW / 2, FH - 1)], "-1 on empty launcher");
				continue;
			}
			CHECK(!grid[cell], "rest cell occupied");
			CHECK(cell % COLS < rowlen(cell / COLS), "rest cell off row");
			n = neighbors(cell, nb);
			ok = cell / COLS == 0;
			for (k = 0; k < n; k++)
				ok |= grid[nb[k]] != 0;
			CHECK(ok, "rest cell touches nothing");
			for (k = 1; k < pathlen; k++) {
				int j, adj = 0;

				n = neighbors(pathseq[k], nb);
				for (j = 0; j < n; j++)
					adj |= nb[j] == pathseq[k - 1];
				CHECK(adj, "path cells %d,%d not adjacent",
				    pathseq[k - 1], pathseq[k]);
			}
		}
		aim = (int)rnd(2 * AIMMAX + 1) - AIMMAX;
		cell = trajectory();
		if (cell < 0)
			continue;
		nextcolor = 1 + (int)rnd(NCOLORS);
		grid[cell] = nextcolor;
		size = group_size(cell);
		grid[cell] = 0;
		before = score;
		fire_at(cell);
		CHECK((size >= 3) == (score > before), "pop iff group of three: size %d", size);
		CHECK(anchor_and_drop() == 0, "unanchored bubble left");
	}
	aim = 0;
	testmode = 0;
}

/* The color script gate, the starting boards and the empty-board test. */
static void
prop_misc(void)
{
	int r, c, mode, st;

	CHECK(script_ok("12") && script_ok("4321") && !script_ok("1") &&
	    !script_ok("") && !script_ok("15") && !script_ok("1a"),
	    "script_ok");
	srand(1);
	testmode = 0;
	for (mode = 0; mode < 2; mode++)
		for (st = 0; st < 2; st++) {
			hexmode = mode;
			stagger = mode ? st : 0;
			initboard();
			CHECK(!boardempty(), "start board empty");
			for (r = 0; r < ROWS; r++)
				for (c = 0; c < COLS; c++) {
					int v = grid[r * COLS + c];

					if (r >= INITROWS || c >= rowlen(r))
						CHECK(v == 0, "cell %d,%d filled", r, c);
					else if (mode)
						CHECK(v >= 1 && v <= NCOLORS, "hex cell %d,%d", r, c);
					else if (c > 0 && c < COLS - 1)
						CHECK(v >= 1 && v <= NCOLORS, "sq cell %d,%d", r, c);
					else
						CHECK(v == 0, "sq edge %d,%d", r, c);
				}
		}
	testmode = 1;
	initboard();
	CHECK(boardempty(), "test board not empty");
	testmode = 0;
	hexmode = 0;
	stagger = 0;
}

int
main(int argc, char **argv)
{
	run_vectors(argc > 1 ? argv[1] : "tests/vectors.txt");
	prop_adjacency();
	prop_shots();
	prop_misc();
	printf("hex_test: %d vectors (%d failed), %d checks (%d failed)\n",
	    nvec, nvec_bad, nchk, nchk_bad);
	if (nvec < EXPECTED_VECTORS) {
		printf("FAIL: expected at least %d vectors\n", EXPECTED_VECTORS);
		return 1;
	}
	return nvec_bad || nchk_bad;
}
