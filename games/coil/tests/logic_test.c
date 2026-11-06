/*
 * Host test of the coil rules. Includes ../coil.c with COIL_LOGIC so the
 * same text that ships is the text tested.
 *
 *   logic_test [vectors.txt]
 *
 * Runs every line of tests/vectors.txt (PATHCELL, RAY, SLOT, INSERT, RUN,
 * FIRE, TICK, REPLAY), then property checks: the spiral's cells are
 * distinct, consecutive cells touch and no two other cells do, and random
 * games keep the chain ordered, inside the path, free of runs of three and
 * a front part that holds while a gap closes. Prints the counts and exits
 * nonzero on any mismatch, on an unknown vector line, or when fewer than
 * EXPECTED_VECTORS vectors ran.
 *
 * Vector grammar: "ID COMMAND key=value ... -> key=value ...". A chain is
 * "pos:color" pairs from the front joined by ',', or '-' when empty.
 */
#define COIL_LOGIC
#include "../coil.c"

#include <stdio.h>

#define EXPECTED_VECTORS 90

static int nvec, nvec_bad, nchk, nchk_bad;
static char curid[64];
static struct cl_game g;

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

static int zero_draw(int m) { (void)m; return 0; }

/* A fresh game with scripted colors and no random draws. */
static void
game_reset(const char *line)
{
	static char spawn[64], shots[64];

	memset(&g, 0, sizeof g);
	g.draw = zero_draw;
	g.spawn_script = kv_str(line, "spawn", spawn, sizeof spawn) ? spawn : NULL;
	g.shot_script = kv_str(line, "shots", shots, sizeof shots) ? shots : "1";
	cl_new(&g, kv_int(line, "level", 1), kv_int(line, "adv", 1));
	g.supply = kv_int(line, "supply", g.supply);
	g.combo = kv_int(line, "combo", 0);
	g.score = (unsigned long)kv_int(line, "score", 0);
	g.shot = kv_int(line, "shot", g.shot);
	g.dir = kv_int(line, "dir", 0);
	g.state = kv_int(line, "state", CL_PLAY);
}

static void
chain_set(const char *s)
{
	g.n = 0;
	if (*s == '-')
		return;
	while (*s) {
		g.pos[g.n] = (short)strtol(s, (char **)&s, 10);
		s++;
		g.col[g.n] = (unsigned char)(*s++ - '0');
		g.n++;
		if (*s == ',')
			s++;
	}
}

static void
chain_str(char *out)
{
	int k, n = 0;

	if (g.n == 0) {
		strcpy(out, "-");
		return;
	}
	for (k = 0; k < g.n; k++)
		n += sprintf(out + n, "%s%d:%d", k ? "," : "", g.pos[k], g.col[k]);
}

static void
load_chain(const char *line)
{
	char buf[1200];

	if (kv_str(line, "chain", buf, sizeof buf))
		chain_set(buf);
}

/* Compare the chain, and whichever of score, combo, supply and state the
 * vector names, against the game. */
static int
expect_state(const char *line, char *have)
{
	char want[1200];
	int ok = 1;

	chain_str(have);
	if (kv_str(line, "result", want, sizeof want) && strcmp(want, have))
		ok = 0;
	if (kv(line, "score") && (unsigned long)kv_int(line, "score", 0) != g.score)
		ok = 0;
	if (kv(line, "combo") && kv_int(line, "combo", 0) != g.combo)
		ok = 0;
	if (kv(line, "supply") && kv_int(line, "supply", 0) != g.supply)
		ok = 0;
	if (kv(line, "state") && kv_int(line, "state", 0) != g.state)
		ok = 0;
	if (kv(line, "loaded") && kv_int(line, "loaded", 0) != g.shot)
		ok = 0;
	if (kv(line, "nextc") && kv_int(line, "nextc", 0) != g.next)
		ok = 0;
	return ok;
}

static void
v_pathcell(const char *line, const char *exp)
{
	int i = kv_int(line, "idx", 0);
	char have[32], want[32];

	sprintf(have, "%d,%d", cl_path[i][0], cl_path[i][1]);
	kv_str(exp, "xy", want, sizeof want);
	vec(strcmp(have, want) == 0, have);
}

static void
v_ray(const char *line, const char *exp)
{
	char have[400], want[400];
	int t, x, y, n = 0;

	have[0] = 0;
	for (t = 1; cl_ray_cell(kv_int(line, "dir", 0), t, &x, &y); t++)
		n += sprintf(have + n, "%s%d,%d", t > 1 ? ";" : "", x, y);
	kv_str(exp, "cells", want, sizeof want);
	vec(strcmp(have, want) == 0, have);
}

static void
v_slot(const char *line, const char *exp)
{
	char from[32];
	int px, py, m;

	game_reset(line);
	load_chain(line);
	kv_str(line, "from", from, sizeof from);
	px = atoi(from);
	py = atoi(strchr(from, ',') + 1);
	m = cl_slot(&g, kv_int(line, "k", 0), px, py);
	vec(m == kv_int(exp, "m", -1), "slot index");
}

static void
v_insert(const char *line, const char *exp)
{
	char have[1200];
	int lo, hi;

	game_reset(line);
	load_chain(line);
	cl_insert(&g, kv_int(line, "m", 0), kv_int(line, "k", 0),
	    kv_int(line, "color", 1), &lo, &hi);
	vec(expect_state(exp, have), have);
}

static void
v_run(const char *line, const char *exp)
{
	int a, b;

	game_reset(line);
	load_chain(line);
	cl_run(&g, kv_int(line, "k", 0), &a, &b);
	vec(a == kv_int(exp, "a", -1) && b == kv_int(exp, "b", -1), "run bounds");
}

static void
v_fire(const char *line, const char *exp)
{
	char have[1200];
	int hit;

	game_reset(line);
	load_chain(line);
	hit = cl_fire(&g);
	vec(hit == kv_int(exp, "hit", -1) && expect_state(exp, have), have);
}

static void
v_tick(const char *line, const char *exp)
{
	char have[1200];
	int i, n = kv_int(line, "n", 1);

	game_reset(line);
	load_chain(line);
	for (i = 0; i < n; i++)
		cl_tick(&g);
	vec(expect_state(exp, have), have);
}

/* ops is a comma list: tN ticks, lN / rN turns, f fires. */
static void
v_replay(const char *line, const char *exp)
{
	char have[1200], ops[256], *p = ops;
	int i, n;

	game_reset(line);
	load_chain(line);
	kv_str(line, "ops", ops, sizeof ops);
	while (*p) {
		char op = *p++;

		n = (int)strtol(p, &p, 10);
		if (*p == ',')
			p++;
		if (op == 't')
			for (i = 0; i < n; i++)
				cl_tick(&g);
		else if (op == 'l')
			cl_turn(&g, -n);
		else if (op == 'r')
			cl_turn(&g, n);
		else if (op == 'f')
			cl_fire(&g);
	}
	vec(expect_state(exp, have), have);
}

static void
run_vectors(const char *path)
{
	char line[1600], cmd[16];
	FILE *f = fopen(path, "r");
	char *arrow, *p;
	const char *exp;

	if (!f) {
		printf("FAIL cannot open %s\n", path);
		nvec_bad++;
		return;
	}
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (!line[0] || line[0] == '#')
			continue;
		if (sscanf(line, "%63s %15s", curid, cmd) != 2) {
			printf("FAIL unparsable line: %s\n", line);
			nvec_bad++;
			continue;
		}
		arrow = strstr(line, " -> ");
		exp = "";
		if (arrow) {
			*arrow = 0;
			exp = arrow + 4;
		}
		p = line + strlen(curid) + 1 + strlen(cmd);
		if (!strcmp(cmd, "PATHCELL"))
			v_pathcell(p, exp);
		else if (!strcmp(cmd, "RAY"))
			v_ray(p, exp);
		else if (!strcmp(cmd, "SLOT"))
			v_slot(p, exp);
		else if (!strcmp(cmd, "INSERT"))
			v_insert(p, exp);
		else if (!strcmp(cmd, "RUN"))
			v_run(p, exp);
		else if (!strcmp(cmd, "FIRE"))
			v_fire(p, exp);
		else if (!strcmp(cmd, "TICK"))
			v_tick(p, exp);
		else if (!strcmp(cmd, "REPLAY"))
			v_replay(p, exp);
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

static int
rnd_draw(int m)
{
	return (int)rnd((unsigned)m);
}

static void
prop_path(void)
{
	int i, j, dx, dy, adj, cnt = 0, x, y;

	for (i = 0; i < CL_PATHLEN; i++) {
		x = cl_path[i][0];
		y = cl_path[i][1];
		CHECK(x >= 0 && x < CL_W && y >= 0 && y < CL_H, "cell %d off the field", i);
		CHECK(cl_idx[y][x] == i, "index map of cell %d", i);
		CHECK(!(x == CL_LX && y == CL_LY), "launcher on the path");
	}
	for (i = 0; i < CL_PATHLEN; i++)
		for (j = i + 1; j < CL_PATHLEN; j++) {
			dx = cl_path[i][0] - cl_path[j][0];
			dy = cl_path[i][1] - cl_path[j][1];
			adj = (dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1));
			CHECK(!(dx == 0 && dy == 0), "cells %d and %d coincide", i, j);
			CHECK(adj == (j == i + 1), "cells %d and %d adjacency %d", i, j, adj);
		}
	for (y = 0; y < CL_H; y++)
		for (x = 0; x < CL_W; x++)
			cnt += cl_idx[y][x] >= 0;
	CHECK(cnt == CL_PATHLEN, "index map holds %d cells", cnt);
}

/* The invariants of a chain in play. */
static void
chain_ok(const char *what)
{
	int k, a, b;

	CHECK(g.n >= 0 && g.n <= CL_MAXB, "%s: n %d", what, g.n);
	for (k = 0; k < g.n; k++) {
		CHECK(g.pos[k] >= 0 && g.pos[k] < CL_PATHLEN, "%s: pos %d", what, g.pos[k]);
		CHECK(g.col[k] >= 1 && g.col[k] <= g.ncol, "%s: color %d", what, g.col[k]);
		if (k > 0)
			CHECK(g.pos[k - 1] > g.pos[k], "%s: order at %d", what, k);
		cl_run(&g, k, &a, &b);
		CHECK(b - a + 1 < 3, "%s: run of %d left at %d", what, b - a + 1, k);
	}
	CHECK(g.supply >= 0, "%s: supply %d", what, g.supply);
}

static void
prop_games(void)
{
	int game, step, level, op, front, nbefore, gaps;

	for (game = 0; game < 200; game++) {
		level = 1 + game % CL_LEVEL_MAX;
		memset(&g, 0, sizeof g);
		/* Half the games draw from the shipped generator. */
		g.draw = game & 1 ? rnd_draw : cl_rnd;
		srand((unsigned)game);
		cl_new(&g, level, 1 + game % 3);
		for (step = 0; step < 4000 && g.state == CL_PLAY; step++) {
			op = (int)rnd(10);
			if (op < 3) {
				cl_turn(&g, (int)rnd(CL_NDIR));
			} else if (op < 5) {
				cl_fire(&g);
				chain_ok("fire");
			} else {
				gaps = cl_first_gap(&g);
				front = g.n ? g.pos[0] : 0;
				nbefore = g.n;
				cl_tick(&g);
				chain_ok("tick");
				/* The part ahead of the first gap holds, unless a
				 * rejoin just cleared balls. */
				if (gaps && g.n == nbefore)
					CHECK(g.pos[0] == front, "front moved over a gap");
			}
			if (g.n > 0 && g.state == CL_PLAY)
				CHECK(g.pos[0] < CL_PATHLEN - 1, "a ball on the hole in play");
		}
		CHECK(g.state != CL_WON || (g.n == 0 && g.supply == 0), "won with balls left");
		CHECK(g.state != CL_LOST || (g.n > 0 && g.pos[0] >= CL_PATHLEN - 1), "lost early");
	}
}

/* The shot line always rises by one step of the longer axis per step. */
static void
prop_rays(void)
{
	int d, t, x, y, px, py, ax, ay, m;

	for (d = 0; d < CL_NDIR; d++) {
		ax = cl_dir[d][0] < 0 ? -cl_dir[d][0] : cl_dir[d][0];
		ay = cl_dir[d][1] < 0 ? -cl_dir[d][1] : cl_dir[d][1];
		m = ax > ay ? ax : ay;
		px = CL_LX;
		py = CL_LY;
		for (t = 1; cl_ray_cell(d, t, &x, &y); t++) {
			int mx = x - px < 0 ? px - x : x - px;
			int my = y - py < 0 ? py - y : y - py;

			CHECK((ax == m ? mx : my) == 1, "dir %d step %d longer axis", d, t);
			CHECK((ax == m ? my : mx) <= 1, "dir %d step %d shorter axis", d, t);
			px = x;
			py = y;
		}
	}
}

int
main(int argc, char **argv)
{
	cl_path_init();
	run_vectors(argc > 1 ? argv[1] : "tests/vectors.txt");
	prop_path();
	prop_rays();
	prop_games();
	printf("logic_test: %d vectors (%d failed), %d checks (%d failed)\n",
	    nvec, nvec_bad, nchk, nchk_bad);
	if (nvec < EXPECTED_VECTORS) {
		printf("FAIL: expected at least %d vectors\n", EXPECTED_VECTORS);
		return 1;
	}
	return nvec_bad || nchk_bad;
}
