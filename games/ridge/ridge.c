/*
 * ridge -- a side-scrolling platformer for the PicoCalc pixel device
 * (/dev/lcdgfx) of the DiscoBSD RP2040 port. A hooded climber crosses four
 * mountain levels: it runs, jumps (a held key jumps higher), lands on rock
 * and on one-way ledges, collects shards, lights checkpoint flags, stomps
 * crawlers and reaches the cairn that ends the level. Spikes, crawlers and
 * the pits between the rocks cost a life; the game ends with the third.
 * Levels, tile art and sprites are original to this port and generated or
 * stored in this file.
 *
 * usage: ridge [level]
 *
 *   level  1..4, default 1
 *
 * Keys: Left/Right (h, l) run, Up, Space or k jump (hold for height), Down
 * (j) drops through a ledge, p pauses, q or Esc quits. Enter or Space starts
 * a new game after the last life or the last level.
 *
 * The file has three parts. The rules half owns the level data, the
 * integer physics (positions in sixteenths of a pixel, one tick per
 * RG_TICK_MS), collision against the tile map, scoring, level progress, the
 * camera and the translation of raw key events into per-tick input; it
 * performs no I/O and reads no clock. Defining RIDGE_LOGIC compiles that
 * half alone, for tests/logic_test.c. The render half draws the game through
 * the tile map of games/gametile.h: world cells and the heads-up display are
 * tile numbers, and a sprite is composited over the tiles it covers into
 * atlas slots reserved for it, so a one-column scroll changes only the
 * cells whose tile differs and sends one window per run of them. Defining
 * RIDGE_RENDER compiles the rules and the render half, for
 * tests/render_test.c, which binds the two device calls of gametile.h to a
 * model of /dev/lcdgfx; the key-event translation, which only the front end
 * calls, is outside that build. The front end opens the device and the console,
 * starts the raw key event session of sys/lcdcons.h and runs the timed loop.
 * It needs both; without /dev/lcdgfx or without the key session it prints
 * why and exits, because the text console cannot show the game.
 *
 * Units. A tile is 8 by 8 pixels and 128 sub-pixel units; the map holds
 * RG_MAXW columns of RG_H rows, column first. Positions are the top left of
 * a hit box. Outside the map the left, right and top are rock and the
 * bottom is open air, so a pit leads out of the map and ends the life.
 */
#include <string.h>

#include <sys/lcdcons.h>

/* ------------------------------------------------------------------ */
/* Rules                                                              */
/* ------------------------------------------------------------------ */

#define RG_TW		8		/* tile edge in pixels */
#define RG_SUB		16		/* sub-pixel units per pixel */
#define RG_T		(RG_TW * RG_SUB)
#define RG_H		32
#define RG_MAXW		200
#define RG_MAXE		12
#define RG_NLEVELS	4
#define RG_TICK_MS	33

#define RG_PW		10		/* player hit box, pixels */
#define RG_PH		14
#define RG_EW		12		/* crawler hit box, pixels */
#define RG_EH		8

#define RG_VMAX		40		/* run speed, sub-pixels per tick */
#define RG_ACC_G	8
#define RG_ACC_A	6
#define RG_GRAV		12
#define RG_VFALL	96
#define RG_JUMP		132
#define RG_CUT		48		/* rise speed left after a released jump key */
#define RG_BOUNCE	88		/* rise speed after a stomp */
#define RG_STEP		64		/* longest move without a collision test */
#define RG_COYOTE	4
#define RG_JBUF		5
#define RG_DROP		3
#define RG_ESPD		12
#define RG_STOMP	5		/* pixels below a crawler's top that still stomp */

#define RG_LIVES	3
#define RG_READY_T	45
#define RG_RESPAWN_T	20
#define RG_GRACE	45		/* ticks a respawned player is safe from crawlers */
#define RG_DYING_T	40
#define RG_CLEAR_T	60
#define RG_HOP		100
#define RG_SCORE_MAX	999999UL
#define RG_SCORE_SHARD	10UL
#define RG_SCORE_STOMP	50UL
#define RG_SCORE_CLEAR	100UL
#define RG_SCORE_SECOND	2UL
#define RG_SCORE_ALL	100UL

#define RG_DZ_LEFT	12		/* camera dead zone, tile columns of the view */
#define RG_DZ_RIGHT	20
#define RG_VIEW_W	40

enum { RG_AIR, RG_ROCK, RG_LEDGE, RG_SPIKE, RG_SHARD, RG_FLAG, RG_FLAGUP,
    RG_GOALBASE, RG_GOALTOP };
enum { RG_READY, RG_PLAY, RG_DYING, RG_CLEAR, RG_OVER, RG_WON };

#define RG_IN_LEFT	1U
#define RG_IN_RIGHT	2U
#define RG_IN_JUMP	4U		/* the jump key is down */
#define RG_IN_JPRESS	8U		/* the jump key went down since the last tick */
#define RG_IN_DOWN	16U		/* the drop key went down since the last tick */

struct rg_player {
	int x, y;			/* hit box top left, sub-pixels */
	int vx, vy;
	int face;			/* -1 or 1 */
	int ground, coyote, jbuf, jumping, drop, anim;
	int grace;			/* ticks left in which a crawler's touch does no harm */
};

struct rg_enemy {
	int x, y;			/* hit box top left, sub-pixels */
	int x0, x1;			/* leftmost and rightmost x */
	int dir;
	int alive;
};

struct rg_game {
	unsigned char map[RG_MAXW][RG_H];
	struct rg_enemy en[RG_MAXE];
	struct rg_player p;
	unsigned long score, ticks;
	unsigned gen;			/* changes when a map cell changes */
	int w, level, state, timer, lives, cam;
	int shards, nshards, time_left, ne;
	int spawn_x, spawn_y;
};

/* Level commands: a rectangle of rock, a ledge run, spikes, shards, a flag, the goal, a crawler, the start. */
enum { RG_C_FILL, RG_C_LEDGE, RG_C_SPIKE, RG_C_SHARD, RG_C_FLAG, RG_C_GOAL,
    RG_C_CRAWL, RG_C_START };

struct rg_cmd {
	unsigned char op, a, b, c, d;
};

struct rg_leveldef {
	const char *name;
	unsigned char w, par;		/* columns; seconds of the time bonus */
	unsigned short n;
	const struct rg_cmd *cmd;
};

/*
 * FILL x y w h: rock; h of 0 runs to the bottom of the map.
 * LEDGE x y w, SPIKE x y w: a run along row y.
 * SHARD x y n dx: n shards on row y, dx columns apart.
 * FLAG x y, GOAL x y: y is the row of the lower cell.
 * CRAWL x0 y x1: a crawler walks in row y over columns x0 to x1.
 * START x y: the player stands on row y in column x.
 */
#define FILL(x, y, w, h)	{ RG_C_FILL, x, y, w, h }
#define GND(x, y, w)		{ RG_C_FILL, x, y, w, 0 }
#define LEDGE(x, y, w)		{ RG_C_LEDGE, x, y, w, 0 }
#define SPIKE(x, y, w)		{ RG_C_SPIKE, x, y, w, 0 }
#define SHARDS(x, y, n, dx)	{ RG_C_SHARD, x, y, n, dx }
#define FLAG(x, y)		{ RG_C_FLAG, x, y, 0, 0 }
#define GOAL(x, y)		{ RG_C_GOAL, x, y, 0, 0 }
#define CRAWL(x0, y, x1)	{ RG_C_CRAWL, x0, y, x1, 0 }
#define START(x, y)		{ RG_C_START, x, y, 0, 0 }

static const struct rg_cmd rg_level1[] = {
	START(3, 23),
	GND(0, 24, 22),
	FILL(12, 22, 2, 2),
	SHARDS(8, 22, 2, 2),
	SHARDS(12, 20, 2, 1),
	GND(26, 24, 22),
	SHARDS(23, 21, 2, 2),
	FLAG(28, 23),
	CRAWL(32, 23, 36),
	LEDGE(38, 20, 6),
	SHARDS(39, 18, 4, 1),
	GND(53, 24, 6),
	FILL(56, 21, 3, 3),
	GND(59, 21, 13),
	SPIKE(64, 20, 3),
	SHARDS(63, 17, 5, 1),
	GND(72, 24, 24),
	FLAG(76, 23),
	CRAWL(78, 23, 84),
	GOAL(90, 23)
};

static const struct rg_cmd rg_level2[] = {
	START(2, 23),
	GND(0, 24, 14),
	GND(14, 23, 3),
	GND(17, 22, 3),
	GND(20, 21, 3),
	GND(23, 20, 4),
	SPIKE(24, 19, 2),
	GND(31, 20, 6),
	FLAG(32, 19),
	LEDGE(38, 17, 4),
	LEDGE(44, 15, 4),
	SHARDS(38, 15, 4, 1),
	SHARDS(44, 13, 4, 1),
	GND(49, 18, 12),
	CRAWL(51, 17, 58),
	GND(65, 20, 4),
	GND(73, 22, 20),
	SPIKE(78, 21, 2),
	SPIKE(84, 21, 2),
	SHARDS(77, 18, 10, 1),
	FLAG(75, 21),
	GND(97, 24, 33),
	CRAWL(101, 23, 108),
	FILL(112, 20, 2, 4),
	SHARDS(116, 22, 3, 3),
	GOAL(125, 23)
};

static const struct rg_cmd rg_level3[] = {
	START(3, 23),
	GND(0, 24, 40),
	FILL(18, 4, 3, 18),
	SPIKE(24, 23, 16),
	LEDGE(21, 21, 8),
	LEDGE(32, 18, 8),
	LEDGE(21, 15, 8),
	CRAWL(23, 14, 27),
	LEDGE(32, 12, 8),
	FILL(40, 8, 30, 0),
	SHARDS(22, 19, 3, 2),
	SHARDS(33, 16, 3, 2),
	SHARDS(22, 13, 2, 3),
	SHARDS(33, 10, 3, 2),
	FLAG(44, 7),
	SPIKE(50, 7, 3),
	SHARDS(49, 4, 5, 1),
	CRAWL(56, 7, 62),
	GOAL(66, 7)
};

static const struct rg_cmd rg_level4[] = {
	START(2, 23),
	GND(0, 24, 10),
	GND(14, 22, 3),
	GND(21, 20, 3),
	GND(28, 22, 4),
	SPIKE(29, 21, 2),
	GND(37, 24, 8),
	CRAWL(39, 23, 44),
	FLAG(37, 23),
	GND(50, 21, 3),
	GND(57, 18, 3),
	GND(63, 15, 3),
	SHARDS(14, 19, 2, 7),
	SHARDS(57, 15, 1, 1),
	GND(70, 17, 14),
	SPIKE(74, 16, 4),
	FLAG(71, 16),
	GND(89, 20, 4),
	GND(98, 22, 3),
	GND(105, 20, 3),
	GND(112, 22, 14),
	CRAWL(114, 21, 124),
	SHARDS(113, 19, 4, 3),
	GND(131, 24, 4),
	GND(139, 21, 3),
	GND(146, 18, 14),
	SPIKE(150, 17, 2),
	SHARDS(149, 14, 4, 1),
	GOAL(156, 17)
};

static const struct rg_leveldef rg_levels[RG_NLEVELS] = {
	{ "FOOTFALL", 96, 40, sizeof rg_level1 / sizeof rg_level1[0], rg_level1 },
	{ "SCREE", 130, 60, sizeof rg_level2 / sizeof rg_level2[0], rg_level2 },
	{ "CHIMNEY", 70, 45, sizeof rg_level3 / sizeof rg_level3[0], rg_level3 },
	{ "SUMMIT", 160, 70, sizeof rg_level4 / sizeof rg_level4[0], rg_level4 }
};

/* Floor division of a sub-pixel or pixel coordinate by a tile edge. */
static int
rg_tdiv(int v)
{
	return v >= 0 ? v / RG_T : -((-v + RG_T - 1) / RG_T);
}

/* The cell at (tx, ty): rock beyond the sides and the top, air below the map. */
static int
rg_cell(const struct rg_game *g, int tx, int ty)
{
	if (tx < 0 || tx >= g->w || ty < 0)
		return RG_ROCK;
	if (ty >= RG_H)
		return RG_AIR;
	return g->map[tx][ty];
}

static void
rg_put(struct rg_game *g, int tx, int ty, int kind)
{
	if (tx >= 0 && tx < g->w && ty >= 0 && ty < RG_H)
		g->map[tx][ty] = (unsigned char)kind;
}

static void
rg_run(struct rg_game *g, int x, int y, int w, int h, int kind)
{
	int i, j;

	if (h == 0)
		h = RG_H - y;
	for (i = x; i < x + w; i++)
		for (j = y; j < y + h; j++)
			rg_put(g, i, j, kind);
}

static void
rg_place_start(struct rg_game *g, int tx, int ty)
{
	g->p.x = tx * RG_T + (RG_T - RG_PW * RG_SUB) / 2;
	g->p.y = (ty + 1) * RG_T - RG_PH * RG_SUB;
	g->spawn_x = g->p.x;
	g->spawn_y = g->p.y;
}

static void rg_cam_snap(struct rg_game *g);

/* Build level `level` (0-based) and ready the player; score and lives stay. */
static void
rg_load(struct rg_game *g, int level)
{
	const struct rg_leveldef *d = &rg_levels[level];
	const struct rg_cmd *c;
	struct rg_enemy *e;
	int i, k;

	memset(g->map, 0, sizeof g->map);
	memset(g->en, 0, sizeof g->en);
	memset(&g->p, 0, sizeof g->p);
	g->level = level;
	g->w = d->w;
	g->ne = 0;
	g->nshards = g->shards = 0;
	for (i = 0; i < d->n; i++) {
		c = &d->cmd[i];
		switch (c->op) {
		case RG_C_FILL:
			rg_run(g, c->a, c->b, c->c, c->d, RG_ROCK);
			break;
		case RG_C_LEDGE:
			rg_run(g, c->a, c->b, c->c, 1, RG_LEDGE);
			break;
		case RG_C_SPIKE:
			rg_run(g, c->a, c->b, c->c, 1, RG_SPIKE);
			break;
		case RG_C_SHARD:
			for (k = 0; k < c->c; k++) {
				rg_put(g, c->a + k * c->d, c->b, RG_SHARD);
				g->nshards++;
			}
			break;
		case RG_C_FLAG:
			rg_put(g, c->a, c->b, RG_FLAG);
			break;
		case RG_C_GOAL:
			rg_put(g, c->a, c->b, RG_GOALBASE);
			rg_put(g, c->a, c->b - 1, RG_GOALTOP);
			break;
		case RG_C_CRAWL:
			if (g->ne < RG_MAXE) {
				e = &g->en[g->ne++];
				e->x0 = c->a * RG_T;
				e->x1 = (c->c + 1) * RG_T - RG_EW * RG_SUB;
				e->x = e->x0;
				e->y = (c->b + 1) * RG_T - RG_EH * RG_SUB;
				e->dir = 1;
				e->alive = 1;
			}
			break;
		case RG_C_START:
			rg_place_start(g, c->a, c->b);
			break;
		}
	}
	g->p.face = 1;
	g->state = RG_READY;
	g->timer = RG_READY_T;
	g->time_left = d->par * 1000 / RG_TICK_MS;
	g->gen++;
	rg_cam_snap(g);
}

static void
rg_new_game(struct rg_game *g, int level)
{
	g->score = 0;
	g->lives = RG_LIVES;
	g->ticks = 0;
	rg_load(g, level);
}

static void
rg_add_score(struct rg_game *g, unsigned long n)
{
	g->score += n;
	if (g->score > RG_SCORE_MAX)
		g->score = RG_SCORE_MAX;
}

/* Does the player box at (x, y) overlap a rock cell? */
static int
rg_hit_rock(const struct rg_game *g, int x, int y)
{
	int tx0 = rg_tdiv(x), tx1 = rg_tdiv(x + RG_PW * RG_SUB - 1);
	int ty0 = rg_tdiv(y), ty1 = rg_tdiv(y + RG_PH * RG_SUB - 1);
	int tx, ty;

	for (ty = ty0; ty <= ty1; ty++)
		for (tx = tx0; tx <= tx1; tx++)
			if (rg_cell(g, tx, ty) == RG_ROCK)
				return 1;
	return 0;
}

/*
 * A ledge stops a fall that carries the box bottom across the ledge's top
 * edge: the bottom was at or above that edge before the move and is below
 * it after.
 */
static int
rg_hit_ledge(const struct rg_game *g, int x, int y_old, int y_new)
{
	int row = rg_tdiv(y_new + RG_PH * RG_SUB - 1);
	int tx0 = rg_tdiv(x), tx1 = rg_tdiv(x + RG_PW * RG_SUB - 1);
	int tx;

	if (y_old + RG_PH * RG_SUB > row * RG_T)
		return 0;
	for (tx = tx0; tx <= tx1; tx++)
		if (rg_cell(g, tx, row) == RG_LEDGE)
			return 1;
	return 0;
}

static void
rg_move_x(const struct rg_game *g, struct rg_player *p, int dx)
{
	while (dx != 0) {
		int s = dx > RG_STEP ? RG_STEP : dx < -RG_STEP ? -RG_STEP : dx;
		int nx = p->x + s;

		if (rg_hit_rock(g, nx, p->y)) {
			if (s > 0)
				p->x = rg_tdiv(nx + RG_PW * RG_SUB - 1) * RG_T - RG_PW * RG_SUB;
			else
				p->x = (rg_tdiv(nx) + 1) * RG_T;
			p->vx = 0;
			return;
		}
		p->x = nx;
		dx -= s;
	}
}

static void
rg_move_y(const struct rg_game *g, struct rg_player *p, int dy)
{
	while (dy != 0) {
		int s = dy > RG_STEP ? RG_STEP : dy < -RG_STEP ? -RG_STEP : dy;
		int ny = p->y + s;

		if (rg_hit_rock(g, p->x, ny)) {
			if (s > 0) {
				p->y = rg_tdiv(ny + RG_PH * RG_SUB - 1) * RG_T - RG_PH * RG_SUB;
				p->ground = 1;
			} else {
				p->y = (rg_tdiv(ny) + 1) * RG_T;
			}
			p->vy = 0;
			return;
		}
		if (s > 0 && p->drop == 0 && rg_hit_ledge(g, p->x, p->y, ny)) {
			p->y = rg_tdiv(ny + RG_PH * RG_SUB - 1) * RG_T - RG_PH * RG_SUB;
			p->ground = 1;
			p->vy = 0;
			return;
		}
		p->y = ny;
		dy -= s;
	}
}

/* One tick of the player's body against the map; no items, hazards or crawlers. */
static void
rg_player_step(const struct rg_game *g, struct rg_player *p, unsigned in)
{
	int dir = ((in & RG_IN_RIGHT) != 0) - ((in & RG_IN_LEFT) != 0);
	int target = dir * RG_VMAX, acc = p->ground ? RG_ACC_G : RG_ACC_A;

	if (dir != 0)
		p->face = dir;
	if (p->vx < target) {
		p->vx += acc;
		if (p->vx > target)
			p->vx = target;
	} else if (p->vx > target) {
		p->vx -= acc;
		if (p->vx < target)
			p->vx = target;
	}
	if (p->ground)
		p->coyote = RG_COYOTE;
	else if (p->coyote > 0)
		p->coyote--;
	if (in & RG_IN_JPRESS)
		p->jbuf = RG_JBUF;
	else if (p->jbuf > 0)
		p->jbuf--;
	if (p->jbuf > 0 && p->coyote > 0) {
		p->vy = -RG_JUMP;
		p->jbuf = 0;
		p->coyote = 0;
		p->ground = 0;
		p->jumping = 1;
	}
	if ((in & RG_IN_DOWN) && p->ground)
		p->drop = RG_DROP;
	if (p->jumping && !(in & RG_IN_JUMP) && p->vy < -RG_CUT)
		p->vy = -RG_CUT;
	p->vy += RG_GRAV;
	if (p->vy > RG_VFALL)
		p->vy = RG_VFALL;
	rg_move_x(g, p, p->vx);
	p->ground = 0;
	rg_move_y(g, p, p->vy);
	if (p->drop > 0)
		p->drop--;
	if (p->vy >= 0)
		p->jumping = 0;
	if (p->ground && p->vx != 0)
		p->anim++;
	else if (!p->ground)
		p->anim = 0;
}

/* Does the player box overlap the lower part of a spike cell? */
static int
rg_spike_hit(const struct rg_game *g, const struct rg_player *p)
{
	int tx0 = rg_tdiv(p->x), tx1 = rg_tdiv(p->x + RG_PW * RG_SUB - 1);
	int ty0 = rg_tdiv(p->y), ty1 = rg_tdiv(p->y + RG_PH * RG_SUB - 1);
	int tx, ty;

	for (ty = ty0; ty <= ty1; ty++)
		for (tx = tx0; tx <= tx1; tx++) {
			if (rg_cell(g, tx, ty) != RG_SPIKE)
				continue;
			if (p->x < (tx + 1) * RG_T - 2 * RG_SUB &&
			    p->x + RG_PW * RG_SUB > tx * RG_T + 2 * RG_SUB &&
			    p->y < (ty + 1) * RG_T &&
			    p->y + RG_PH * RG_SUB > ty * RG_T + 5 * RG_SUB)
				return 1;
		}
	return 0;
}

/* Does the player box overlap a goal cell? */
static int
rg_goal_hit(const struct rg_game *g, const struct rg_player *p)
{
	int tx0 = rg_tdiv(p->x), tx1 = rg_tdiv(p->x + RG_PW * RG_SUB - 1);
	int ty0 = rg_tdiv(p->y), ty1 = rg_tdiv(p->y + RG_PH * RG_SUB - 1);
	int tx, ty, k;

	for (ty = ty0; ty <= ty1; ty++)
		for (tx = tx0; tx <= tx1; tx++) {
			k = rg_cell(g, tx, ty);
			if (k == RG_GOALBASE || k == RG_GOALTOP)
				return 1;
		}
	return 0;
}

/* The player left the map through the bottom, far enough that no screen shows it. */
static int
rg_in_pit(const struct rg_player *p)
{
	return p->y > (RG_H + 2) * RG_T;
}

static void
rg_die(struct rg_game *g)
{
	g->state = RG_DYING;
	g->timer = RG_DYING_T;
	g->lives--;
	g->p.vx = 0;
	g->p.vy = -RG_HOP;
	g->p.jumping = 0;
}

/* Whole seconds left on the level clock; a tick lasts RG_TICK_MS. */
static unsigned long
rg_seconds_left(const struct rg_game *g)
{
	return (unsigned long)g->time_left * RG_TICK_MS / 1000;
}

static void
rg_win_level(struct rg_game *g)
{
	unsigned long bonus = RG_SCORE_CLEAR + RG_SCORE_SECOND * rg_seconds_left(g);

	if (g->shards == g->nshards)
		bonus += RG_SCORE_ALL;
	rg_add_score(g, bonus);
	g->state = RG_CLEAR;
	g->timer = RG_CLEAR_T;
}

/* Collect what the player's box overlaps: shards, flags. */
static void
rg_collect(struct rg_game *g)
{
	const struct rg_player *p = &g->p;
	int tx0 = rg_tdiv(p->x), tx1 = rg_tdiv(p->x + RG_PW * RG_SUB - 1);
	int ty0 = rg_tdiv(p->y), ty1 = rg_tdiv(p->y + RG_PH * RG_SUB - 1);
	int tx, ty;

	for (ty = ty0; ty <= ty1; ty++)
		for (tx = tx0; tx <= tx1; tx++) {
			switch (rg_cell(g, tx, ty)) {
			case RG_SHARD:
				rg_put(g, tx, ty, RG_AIR);
				g->shards++;
				rg_add_score(g, RG_SCORE_SHARD);
				g->gen++;
				break;
			case RG_FLAG:
				rg_put(g, tx, ty, RG_FLAGUP);
				g->spawn_x = tx * RG_T + (RG_T - RG_PW * RG_SUB) / 2;
				g->spawn_y = (ty + 1) * RG_T - RG_PH * RG_SUB;
				g->gen++;
				break;
			default:
				break;
			}
		}
}

/*
 * Walk the crawlers and resolve their contact with the player. A stomp needs
 * the move of this tick to have carried the box down (fell) from a bottom
 * (bottom0) within RG_STOMP pixels of the crawler's top; both come from before
 * the move, because landing on a floor in the same tick clears vy.
 */
static void
rg_enemies(struct rg_game *g, int bottom0, int fell)
{
	struct rg_player *p = &g->p;
	struct rg_enemy *e;
	int i, touching = 0;

	for (i = 0; i < g->ne; i++) {
		e = &g->en[i];
		if (!e->alive)
			continue;
		e->x += e->dir * RG_ESPD;
		if (e->x <= e->x0) {
			e->x = e->x0;
			e->dir = 1;
		} else if (e->x >= e->x1) {
			e->x = e->x1;
			e->dir = -1;
		}
		if (p->x >= e->x + RG_EW * RG_SUB || p->x + RG_PW * RG_SUB <= e->x ||
		    p->y >= e->y + RG_EH * RG_SUB || p->y + RG_PH * RG_SUB <= e->y)
			continue;
		if (fell && bottom0 <= e->y + RG_STOMP * RG_SUB) {
			e->alive = 0;
			rg_add_score(g, RG_SCORE_STOMP);
			p->vy = -RG_BOUNCE;
			p->jumping = 0;
			g->gen++;
		} else if (p->grace > 0) {
			touching = 1;
		} else {
			rg_die(g);
			return;
		}
	}
	/* The grace period ends only once the player stands clear of every crawler. */
	if (p->grace > 0 && (!touching || p->grace > 1))
		p->grace--;
}

/* Put the camera's left column where the dead zone keeps the player. */
static void
rg_cam_snap(struct rg_game *g)
{
	int cx = g->p.x / RG_SUB + RG_PW / 2;

	g->cam = cx / RG_TW - RG_VIEW_W / 2;
	if (g->cam > g->w - RG_VIEW_W)
		g->cam = g->w - RG_VIEW_W;
	if (g->cam < 0)
		g->cam = 0;
}

/* Move the camera one column toward the dead zone around the player. */
static void
rg_cam_step(struct rg_game *g)
{
	int in_view = g->p.x / RG_SUB + RG_PW / 2 - g->cam * RG_TW;

	if (in_view > RG_DZ_RIGHT * RG_TW && g->cam < g->w - RG_VIEW_W)
		g->cam++;
	else if (in_view < RG_DZ_LEFT * RG_TW && g->cam > 0)
		g->cam--;
}

static void
rg_respawn(struct rg_game *g)
{
	memset(&g->p, 0, sizeof g->p);
	g->p.x = g->spawn_x;
	g->p.y = g->spawn_y;
	g->p.face = 1;
	g->p.grace = RG_GRACE;
	g->state = RG_READY;
	g->timer = RG_RESPAWN_T;
	rg_cam_snap(g);
}

/* Advance the game one tick with the player's input. */
static void
rg_tick(struct rg_game *g, unsigned in)
{
	int bottom0, y0;

	g->ticks++;
	switch (g->state) {
	case RG_READY:
		if (--g->timer <= 0)
			g->state = RG_PLAY;
		return;
	case RG_DYING:
		g->p.vy += RG_GRAV;
		if (g->p.vy > RG_VFALL)
			g->p.vy = RG_VFALL;
		g->p.y += g->p.vy;
		if (--g->timer <= 0) {
			if (g->lives > 0)
				rg_respawn(g);
			else
				g->state = RG_OVER;
		}
		return;
	case RG_CLEAR:
		if (--g->timer <= 0) {
			if (g->level + 1 < RG_NLEVELS)
				rg_load(g, g->level + 1);
			else
				g->state = RG_WON;
		}
		return;
	case RG_PLAY:
		break;
	default:
		return;
	}
	if (g->time_left > 0)
		g->time_left--;
	bottom0 = g->p.y + RG_PH * RG_SUB;
	y0 = g->p.y;
	rg_player_step(g, &g->p, in);
	if (rg_in_pit(&g->p) || rg_spike_hit(g, &g->p)) {
		rg_die(g);
		return;
	}
	rg_enemies(g, bottom0, g->p.y > y0);
	if (g->state != RG_PLAY)
		return;
	rg_collect(g);
	if (rg_goal_hit(g, &g->p)) {
		rg_win_level(g);
		return;
	}
	rg_cam_step(g);
}

#ifndef RIDGE_RENDER

/* ------------------------------------------------------------------ */
/* Key events                                                         */
/* ------------------------------------------------------------------ */

#define RG_K_LEFT	1U
#define RG_K_RIGHT	2U
#define RG_K_JUMP	4U
#define RG_K_DOWN	8U

/*
 * The keys the game reads, one slot each, with the action bit each drives.
 * A held printable key repeats as PRESSED events, so an edge or a request
 * fires on the first PRESSED of a slot and not again until its RELEASED.
 */
enum { RG_S_LEFT, RG_S_H, RG_S_RIGHT, RG_S_L, RG_S_UP, RG_S_K, RG_S_SPACE,
    RG_S_DOWN, RG_S_J, RG_S_P, RG_S_Q, RG_S_ESC, RG_S_ENTER, RG_NSLOT };

static const unsigned char rg_slot_bit[RG_NSLOT] = {
	RG_K_LEFT, RG_K_LEFT, RG_K_RIGHT, RG_K_RIGHT, RG_K_JUMP, RG_K_JUMP,
	RG_K_JUMP, RG_K_DOWN, RG_K_DOWN, 0, 0, 0, 0
};

struct rg_keys {
	unsigned held;			/* RG_K_* bits of keys that are down */
	unsigned edge;			/* bits pressed since the last tick */
	unsigned down;			/* bit s: the key of slot s is down */
	int pause, quit, restart;	/* requests the front end consumes */
};

/* The slot of a key byte of the raw key session, or -1. */
static int
rg_key_slot(int key)
{
	if (key >= 'A' && key <= 'Z')
		key += 'a' - 'A';
	switch (key) {
	case LCD_KEY_LEFT:
		return RG_S_LEFT;
	case 'h':
		return RG_S_H;
	case LCD_KEY_RIGHT:
		return RG_S_RIGHT;
	case 'l':
		return RG_S_L;
	case LCD_KEY_UP:
		return RG_S_UP;
	case 'k':
		return RG_S_K;
	case ' ':
		return RG_S_SPACE;
	case LCD_KEY_DOWN:
		return RG_S_DOWN;
	case 'j':
		return RG_S_J;
	case 'p':
		return RG_S_P;
	case 'q':
		return RG_S_Q;
	case LCD_KEY_ESC:
		return RG_S_ESC;
	case LCD_KEY_ENTER:
		return RG_S_ENTER;
	default:
		return -1;
	}
}

/* Apply one event of the raw key session (LCDIOC_GETKEY). */
static void
rg_key_event(struct rg_keys *k, int state, int key)
{
	int slot = rg_key_slot(key), first, i;

	if (slot < 0 || (state != LCD_KEY_PRESSED && state != LCD_KEY_HOLD &&
	    state != LCD_KEY_RELEASED))
		return;
	first = state == LCD_KEY_PRESSED && !(k->down >> slot & 1);
	if (state == LCD_KEY_RELEASED)
		k->down &= ~(1U << slot);
	else
		k->down |= 1U << slot;
	k->held = 0;
	for (i = 0; i < RG_NSLOT; i++)
		if (k->down >> i & 1)
			k->held |= rg_slot_bit[i];
	if (!first)
		return;
	k->edge |= rg_slot_bit[slot];
	if (slot == RG_S_P)
		k->pause = 1;
	else if (slot == RG_S_Q || slot == RG_S_ESC)
		k->quit = 1;
	else if (slot == RG_S_ENTER || slot == RG_S_SPACE)
		k->restart = 1;
}

/* The driver dropped events: the key state is unknown until the next events rebuild it. */
static void
rg_keys_lost(struct rg_keys *k)
{
	k->held = 0;
	k->down = 0;
}

/*
 * Input for one tick. A direction pressed and released between two ticks
 * still moves the player for that tick; the edges are consumed.
 */
static unsigned
rg_keys_input(struct rg_keys *k)
{
	unsigned both = k->held | k->edge, in = 0;

	if (both & RG_K_LEFT)
		in |= RG_IN_LEFT;
	if (both & RG_K_RIGHT)
		in |= RG_IN_RIGHT;
	if (k->held & RG_K_JUMP)
		in |= RG_IN_JUMP;
	if (k->edge & RG_K_JUMP)
		in |= RG_IN_JPRESS;
	if (k->edge & RG_K_DOWN)
		in |= RG_IN_DOWN;
	k->edge = 0;
	return in;
}

#endif /* !RIDGE_RENDER */

#ifndef RIDGE_LOGIC

/* ------------------------------------------------------------------ */
/* Render                                                             */
/* ------------------------------------------------------------------ */

#include "../gametile.h"

#define RG_VIEW_H	38		/* world rows on the panel, below the HUD */
#define RG_HUD_ROWS	2
#define RG_NSPR		7		/* sprites per frame: the player and six crawlers */
#define RG_BOX		3		/* a 16 by 16 sprite covers at most 3 by 3 cells */
#define RG_DYN		64		/* first atlas tile of the sprite slots */
#define RG_RIDGE_N	64

/* Palette entries. */
enum { P_SKY, P_CONTOUR, P_HUD, P_ABYSS, P_ROCK_D, P_ROCK_M, P_ROCK_L,
    P_LICHEN_D, P_LICHEN_L, P_SHARD_C, P_SHARD_E, P_STEEL, P_AMBER, P_RUST,
    P_WHITE, P_INK };

/* Static atlas tiles; the font follows, then the sprite slots from RG_DYN. */
enum { T_SKY, T_CFLAT, T_CRISE, T_CFALL, T_ROCK, T_CAP, T_LEDGE, T_SPIKE,
    T_SHARD0, T_SHARD1, T_FLAG, T_FLAGUP, T_GOALBASE, T_GOALTOP, T_ABYSS,
    T_HUD, T_FONT };
#define RG_GLYPHS	41		/* 0-9, A-Z, / : - ! . */
#define RG_NSTATIC	(T_FONT + RG_GLYPHS)

static const unsigned char rg_pal[16][3] = {
	{ 28, 35, 80 }, { 76, 90, 168 }, { 16, 20, 46 }, { 8, 10, 28 },
	{ 59, 47, 51 }, { 110, 90, 82 }, { 165, 139, 114 }, { 44, 106, 69 },
	{ 98, 184, 106 }, { 159, 243, 255 }, { 46, 168, 216 }, { 212, 216, 232 },
	{ 255, 182, 74 }, { 200, 69, 42 }, { 246, 241, 222 }, { 21, 16, 26 }
};

/* 5 by 7 letter forms, top row first, the leftmost column in bit 4. */
static const unsigned char rg_font[RG_GLYPHS][7] = {
	{ 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },	/* 0 */
	{ 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
	{ 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
	{ 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
	{ 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
	{ 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },
	{ 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
	{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
	{ 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
	{ 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },
	{ 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },	/* A */
	{ 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
	{ 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
	{ 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
	{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
	{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
	{ 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
	{ 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
	{ 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
	{ 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c },
	{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
	{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
	{ 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
	{ 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
	{ 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
	{ 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
	{ 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
	{ 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
	{ 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
	{ 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
	{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 },
	{ 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
	{ 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
	{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },	/* Z */
	{ 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },	/* / */
	{ 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 },	/* : */
	{ 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 },	/* - */
	{ 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 },	/* ! */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c }	/* . */
};

/*
 * The far ridge is a one-pixel contour, so a cell that holds an item or a
 * sprite loses at most a stroke of it. Each column is flat (F), a rise (U)
 * or a fall (D); rg_ridge_init() walks the pattern from row 14 and the walk
 * closes on the same row and end after RG_RIDGE_N columns.
 */
static const char rg_ridge_pat[RG_RIDGE_N + 1] =
    "FFFFFUUUFFFFUUFFFFFFDDDFFFDDFFFFFUUUUFFDFFFFFUUFFFDDDDFFFFUFFFDD";
static unsigned char rg_ridge_row[RG_RIDGE_N], rg_ridge_ty[RG_RIDGE_N];

/*
 * Sprites, 16 by 16: the climber (stand, two run frames, jump) facing right,
 * then the crawler's two frames. '.' is transparent.
 */
static const char rg_art[6][16][17] = {
	{
		"................",
		"................",
		"....oooooo......",
		"...ohhhhhho.....",
		"...ohhhhhhfo....",
		"...ohhhhffffo...",
		"...ohhhhfowfo...",
		"...ohhhhffffo...",
		"...pohhhhfffo...",
		"..ppossssssoo...",
		"..ppossccsso....",
		"...poffhhhho....",
		"....ohhhhhho....",
		"....obbhhbbo....",
		"....obbo.obbo...",
		"...ooooo.oooo..."
	}, {
		"................",
		"................",
		"....oooooo......",
		"...ohhhhhho.....",
		"...ohhhhhhfo....",
		"...ohhhhffffo...",
		"...ohhhhfowfo...",
		"...ohhhhffffo...",
		"...pohhhhfffo...",
		"..ppossssssoo...",
		"..ppossccsso....",
		"...poffhhhho....",
		"....ohhhhhho....",
		"....obbhhob.....",
		"...obbo.obbo....",
		"..oooo...ooo...."
	}, {
		"................",
		"................",
		"....oooooo......",
		"...ohhhhhho.....",
		"...ohhhhhhfo....",
		"...ohhhhffffo...",
		"...ohhhhfowfo...",
		"...ohhhhffffo...",
		"...pohhhhfffo...",
		"..ppossssssoo...",
		"..ppossccsso....",
		"...poffhhhho....",
		"....ohhhhhho....",
		".....obhhbbo....",
		".....obbo.obbo..",
		"....ooooo.ooo..."
	}, {
		"................",
		"................",
		"....oooooo......",
		"...ohhhhhho.....",
		"...ohhhhhhfo....",
		"...ohhhhffffo...",
		"...ohhhhfowfo...",
		"...ohhhhffffo...",
		"...pohhhhfffo...",
		".ppossssssssoo..",
		"..ppossccssso...",
		"...poffhhhhfo...",
		"....ohhhhhho....",
		"...obbhhhhbbo...",
		"..obbo....obbo..",
		"..ooo......ooo.."
	}, {
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		".....oooooo.....",
		"...ooggGGggoo...",
		"..oggGgggGggoo..",
		"..ogggggggggwoo.",
		"..oggggggggggeo.",
		"..ooggggggggoo..",
		"...obobooboboo..",
		"...oo.oo.oo.oo.."
	}, {
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		".....oooooo.....",
		"...ooggGGggoo...",
		"..oggGgggGggoo..",
		"..ogggggggggwoo.",
		"..oggggggggggeo.",
		"..ooggggggggoo..",
		"...bobobobobob..",
		"....oo.oo.oo.o.."
	}
};
#define RG_F_STAND	0
#define RG_F_RUN	1
#define RG_F_JUMP	3
#define RG_F_CRAWL	4

static int
rg_art_pal(char ch)
{
	switch (ch) {
	case 'o':
		return P_INK;
	case 'h':
		return P_RUST;
	case 'f':
		return P_AMBER;
	case 'w':
	case 'e':
		return P_WHITE;
	case 's':
		return P_SHARD_E;
	case 'c':
		return P_SHARD_C;
	case 'p':
		return P_ROCK_M;
	case 'b':
		return P_ROCK_D;
	case 'g':
		return P_LICHEN_D;
	case 'G':
		return P_LICHEN_L;
	default:
		return -1;
	}
}

static void
rg_ridge_init(void)
{
	int i, row = 14, high = 0;

	for (i = 0; i < RG_RIDGE_N; i++) {
		char t = rg_ridge_pat[i];

		if (!high)
			row += t == 'D' ? 1 : 0;
		else
			row += t == 'D' ? 0 : -1;
		rg_ridge_row[i] = (unsigned char)row;
		rg_ridge_ty[i] = (unsigned char)(t == 'F' ? T_CFLAT : t == 'U' ? T_CRISE : T_CFALL);
		high = t == 'U';
	}
}

static void
rg_fill_px(unsigned char px[8][8], int v)
{
	memset(px, v, 64);
}

/* Palette indices of static tile t, row by row. */
static void
rg_tile_px(int t, unsigned char px[8][8])
{
	int x, y, g;

	rg_fill_px(px, P_SKY);
	switch (t) {
	case T_SKY:
		break;
	case T_CFLAT:
		for (x = 0; x < 8; x++)
			px[7][x] = P_CONTOUR;
		break;
	case T_CRISE:
		for (x = 0; x < 8; x++)
			px[7 - x][x] = P_CONTOUR;
		break;
	case T_CFALL:
		for (x = 0; x < 8; x++)
			px[x][x] = P_CONTOUR;
		break;
	case T_ROCK:
	case T_CAP:
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++)
				px[y][x] = (x * 5 + y * 3) % 7 == 0 ? P_ROCK_D :
				    (x * 3 + y * 7) % 11 == 0 ? P_ROCK_L : P_ROCK_M;
		if (t == T_CAP) {
			for (x = 0; x < 8; x++) {
				px[0][x] = P_LICHEN_L;
				px[1][x] = P_LICHEN_L;
				px[2][x] = x & 1 ? P_LICHEN_D : P_LICHEN_L;
				if (x % 3 == 1)
					px[3][x] = P_LICHEN_D;
			}
		}
		break;
	case T_LEDGE:
		for (x = 0; x < 8; x++) {
			px[0][x] = P_ROCK_L;
			px[1][x] = x == 3 || x == 7 ? P_ROCK_D : P_ROCK_M;
			px[2][x] = P_ROCK_D;
		}
		break;
	case T_SPIKE:
		for (x = 0; x < 8; x++) {
			g = x % 4 == 0 || x % 4 == 3 ? 3 : 6;
			for (y = 8 - g; y < 8; y++)
				px[y][x] = P_STEEL;
			if (g == 6)
				px[8 - g][x] = P_WHITE;
		}
		break;
	case T_SHARD0:
	case T_SHARD1:
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++) {
				int dx = 2 * x - 7, dy = 2 * y - 7;

				dx = dx < 0 ? -dx : dx;
				dy = dy < 0 ? -dy : dy;
				if (dx + dy <= 7)
					px[y][x] = dx + dy <= 3 ? P_SHARD_C : P_SHARD_E;
			}
		if (t == T_SHARD1)
			px[2][3] = px[3][2] = P_WHITE;
		break;
	case T_FLAG:
	case T_FLAGUP:
		for (y = 0; y < 8; y++)
			px[y][2] = P_STEEL;
		if (t == T_FLAG) {
			for (x = 3; x < 6; x++)
				px[6][x] = P_ROCK_D;
			px[5][3] = P_ROCK_D;
		} else {
			for (y = 1; y < 5; y++)
				for (x = 3; x < 8 - y; x++)
					px[y][x] = y == 1 ? P_AMBER : P_RUST;
		}
		break;
	case T_GOALBASE:
		for (x = 1; x < 7; x++)
			for (y = 5; y < 8; y++)
				px[y][x] = (x + y) & 1 ? P_ROCK_M : P_ROCK_L;
		for (x = 2; x < 6; x++)
			px[4][x] = P_ROCK_M;
		for (x = 0; x < 8; x++)
			px[7][x] = P_ROCK_D;
		break;
	case T_GOALTOP:
		for (x = 2; x < 6; x++)
			for (y = 5; y < 8; y++)
				px[y][x] = (x + y) & 1 ? P_ROCK_L : P_ROCK_M;
		for (y = 0; y < 5; y++)
			for (x = 0; x < 8; x++) {
				int dx = 2 * x - 7, dy = 2 * y - 4;

				dx = dx < 0 ? -dx : dx;
				dy = dy < 0 ? -dy : dy;
				if (dx + dy <= 6)
					px[y][x] = dx + dy <= 3 ? P_WHITE : P_SHARD_C;
			}
		break;
	case T_ABYSS:
		rg_fill_px(px, P_ABYSS);
		break;
	default:
		rg_fill_px(px, P_HUD);
		if (t >= T_FONT && t < RG_NSTATIC)
			for (y = 0; y < 7; y++)
				for (x = 0; x < 5; x++)
					if (rg_font[t - T_FONT][y] >> (4 - x) & 1)
						px[y + 1][x + 1] = P_WHITE;
		break;
	}
}

/* Pack a tile of palette indices: four bits a pixel, the leftmost in the high nibble. */
static void
rg_pack(unsigned char px[8][8], unsigned char *out)
{
	int y, x;

	for (y = 0; y < 8; y++)
		for (x = 0; x < 8; x += 2)
			*out++ = (unsigned char)(px[y][x] << 4 | px[y][x + 1]);
}

/* The atlas tile that draws character c of the HUD font; a space is the HUD ground. */
static int
rg_glyph(int c)
{
	if (c >= '0' && c <= '9')
		return T_FONT + (c - '0');
	if (c >= 'A' && c <= 'Z')
		return T_FONT + 10 + (c - 'A');
	switch (c) {
	case '/':
		return T_FONT + 36;
	case ':':
		return T_FONT + 37;
	case '-':
		return T_FONT + 38;
	case '!':
		return T_FONT + 39;
	case '.':
		return T_FONT + 40;
	default:
		return T_HUD;
	}
}

/* The static tile that world cell (wx, wy) shows before sprites. */
static int
rg_base_tile(const struct rg_game *g, int wx, int wy)
{
	int i;

	if (wy < 0)
		return T_SKY;
	if (wy >= RG_H)
		return T_ABYSS;
	switch (rg_cell(g, wx, wy)) {
	case RG_ROCK:
		return wy > 0 && rg_cell(g, wx, wy - 1) == RG_ROCK ? T_ROCK : T_CAP;
	case RG_LEDGE:
		return T_LEDGE;
	case RG_SPIKE:
		return T_SPIKE;
	case RG_SHARD:
		return T_SHARD0 + (int)((g->ticks >> 3) & 1);
	case RG_FLAG:
		return T_FLAG;
	case RG_FLAGUP:
		return T_FLAGUP;
	case RG_GOALBASE:
		return T_GOALBASE;
	case RG_GOALTOP:
		return T_GOALTOP;
	default:
		i = (wx % RG_RIDGE_N + RG_RIDGE_N) % RG_RIDGE_N;
		return rg_ridge_row[i] == wy ? rg_ridge_ty[i] : T_SKY;
	}
}

struct rg_sprite {
	int x, y, frame, flip;		/* top left in world pixels */
};

/* A sprite whose box meets another's, with its index relative to that one's. */
struct rg_ovl {
	struct rg_sprite s;
	int rel;
};

/*
 * What a sprite's atlas slots were composited from: the sprite, the camera
 * column, the twinkle phase, the map's change counter and every sprite whose
 * box meets this one's (they share cells, so their pixels are in the slots).
 */
struct rg_sig {
	int x, y, frame, flip, cam, anim, live, nov;
	struct rg_ovl ov[RG_NSPR - 1];
	unsigned gen;
};

struct rg_view {
	struct gtile gt;
	unsigned char fin[GTILE_CELLS];	/* tile number of every panel cell this frame */
	struct rg_sig sig[RG_NSPR];
};

static int
rg_set_palette(struct rg_view *v)
{
	struct lcdg_palette pal;
	int i;

	for (i = 0; i < LCDG_NPAL; i++) {
		pal.index = (u_char)i;
		pal.r = rg_pal[i][0];
		pal.g = rg_pal[i][1];
		pal.b = rg_pal[i][2];
		if (GTILE_IOCTL(v->gt.fd, LCDG_PALETTE, &pal) < 0)
			return -1;
	}
	return 0;
}

/* Load the palette and the static tiles on the open device fd. */
static int
rg_view_init(struct rg_view *v, int fd)
{
	unsigned char px[8][8], packed[LCDG_TILE_BYTES];
	int t;

	memset(v->sig, 0, sizeof v->sig);
	rg_ridge_init();
	gtile_init(&v->gt, fd);
	if (rg_set_palette(v) < 0)
		return -1;
	for (t = 0; t < RG_NSTATIC; t++) {
		rg_tile_px(t, px);
		rg_pack(px, packed);
		if (gtile_load(&v->gt, t, 1, packed) < 0)
			return -1;
	}
	return 0;
}

/* Sprites to draw, the player first; returns how many. */
static int
rg_sprites(const struct rg_game *g, struct rg_sprite *out)
{
	const struct rg_player *p = &g->p;
	int n = 0, i, sx;

	sx = p->x / RG_SUB - 3 - g->cam * RG_TW;
	if (sx > -16 && sx < RG_VIEW_W * RG_TW && !(p->grace > 0 && (g->ticks >> 2 & 1))) {
		out[n].x = p->x / RG_SUB - 3;
		out[n].y = p->y / RG_SUB - 2;
		out[n].frame = !p->ground ? RG_F_JUMP :
		    p->vx != 0 ? RG_F_RUN + ((p->anim >> 2) & 1) : RG_F_STAND;
		out[n].flip = p->face < 0;
		n++;
	}
	for (i = 0; i < g->ne && n < RG_NSPR; i++) {
		const struct rg_enemy *e = &g->en[i];

		if (!e->alive)
			continue;
		sx = e->x / RG_SUB - 2 - g->cam * RG_TW;
		if (sx <= -16 || sx >= RG_VIEW_W * RG_TW)
			continue;
		out[n].x = e->x / RG_SUB - 2;
		out[n].y = e->y / RG_SUB - 8;
		out[n].frame = RG_F_CRAWL + (int)((g->ticks >> 3) & 1);
		out[n].flip = e->dir < 0;
		n++;
	}
	return n;
}

static int
rg_fdiv8(int v)
{
	return v >= 0 ? v / RG_TW : -((-v + RG_TW - 1) / RG_TW);
}

/* Draw sprite s over buf, whose top left pixel is at (ox, oy) from the sprite's top left. */
static void
rg_blit(unsigned char buf[][RG_BOX * RG_TW], const struct rg_sprite *s, int ox, int oy)
{
	int x, y, c;

	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++) {
			c = rg_art_pal(rg_art[s->frame][y][s->flip ? 15 - x : x]);
			if (c >= 0 && oy + y >= 0 && oy + y < RG_BOX * 8 &&
			    ox + x >= 0 && ox + x < RG_BOX * 8)
				buf[oy + y][ox + x] = (unsigned char)c;
		}
}

/* Do two sprite boxes, given by one coordinate of their top left cells, share a cell? */
static int
rg_near(int a, int b)
{
	return a - b < RG_BOX && b - a < RG_BOX;
}

/*
 * Composite the 3 by 3 cells of sprite i's box into tiles[0..8], row by row
 * from the top left cell: the cells' world tiles, then every sprite whose
 * box meets this one's, the highest index first, so the climber (index 0)
 * lies over the crawlers and a lower crawler over a higher one. bxs and bys
 * hold each sprite's top left cell in view coordinates.
 */
static void
rg_composite(const struct rg_game *g, const struct rg_sprite *spr, int n, int i,
    const int *bxs, const int *bys, unsigned char *tiles)
{
	unsigned char buf[RG_BOX * RG_TW][RG_BOX * RG_TW], px[8][8];
	int bx = bxs[i], by = bys[i], cx, cy, y, k;

	for (cy = 0; cy < RG_BOX; cy++)
		for (cx = 0; cx < RG_BOX; cx++) {
			int vx = bx + cx, vy = by + cy;

			if (vx >= 0 && vx < RG_VIEW_W && vy >= 0 && vy < RG_VIEW_H)
				rg_tile_px(rg_base_tile(g, g->cam + vx, vy), px);
			else
				rg_fill_px(px, P_SKY);
			for (y = 0; y < 8; y++)
				memcpy(&buf[cy * 8 + y][cx * 8], px[y], 8);
		}
	for (k = n - 1; k >= 0; k--)
		if (k == i || (rg_near(bxs[k], bx) && rg_near(bys[k], by)))
			rg_blit(buf, &spr[k], spr[k].x - g->cam * RG_TW - bx * RG_TW,
			    spr[k].y - by * RG_TW);
	for (cy = 0; cy < RG_BOX; cy++)
		for (cx = 0; cx < RG_BOX; cx++) {
			for (y = 0; y < 8; y++)
				memcpy(px[y], &buf[cy * 8 + y][cx * 8], 8);
			rg_pack(px, tiles + (cy * RG_BOX + cx) * LCDG_TILE_BYTES);
		}
}

static void
rg_text(struct rg_view *v, int row, int col, const char *s)
{
	for (; *s != 0 && col < GTILE_COLS; s++, col++)
		v->fin[row * GTILE_COLS + col] = (unsigned char)rg_glyph(*s);
}

/* Write n digits of val, zero padded, at (row, col). */
static void
rg_number(struct rg_view *v, int row, int col, unsigned long val, int n)
{
	int i;

	for (i = n - 1; i >= 0; i--, val /= 10)
		v->fin[row * GTILE_COLS + col + i] = (unsigned char)(T_FONT + (int)(val % 10));
}

static void
rg_hud(struct rg_view *v, const struct rg_game *g)
{
	memset(v->fin, T_HUD, (size_t)(RG_HUD_ROWS * GTILE_COLS));
	rg_text(v, 0, 1, "SCORE");
	rg_number(v, 0, 7, g->score, 6);
	rg_text(v, 0, 16, "LIVES");
	rg_number(v, 0, 22, (unsigned long)(g->lives > 0 ? g->lives : 0), 1);
	rg_text(v, 0, 26, "LEVEL");
	rg_number(v, 0, 32, (unsigned long)(g->level + 1), 1);
	rg_text(v, 1, 1, "SHARDS");
	rg_number(v, 1, 8, (unsigned long)g->shards, 2);
	rg_text(v, 1, 10, "/");
	rg_number(v, 1, 11, (unsigned long)g->nshards, 2);
	rg_text(v, 1, 16, "TIME");
	rg_number(v, 1, 21, rg_seconds_left(g), 3);
	rg_text(v, 1, 26, rg_levels[g->level].name);
}

/* A centered message on a row of HUD ground. */
static void
rg_banner(struct rg_view *v, int row, const char *s)
{
	int n = (int)strlen(s), col = (GTILE_COLS - n) / 2, i;

	for (i = col - 1; i <= col + n; i++)
		v->fin[row * GTILE_COLS + i] = T_HUD;
	rg_text(v, row, col, s);
}

/*
 * Compose the frame and send what changed. Every panel cell is assigned a
 * tile number in v->fin first and the tile map compares the numbers with
 * what the panel shows, so a cell whose tile did not change costs nothing.
 * Returns the number of windows sent, or -1 on a failed device call.
 */
static int
rg_draw(struct rg_view *v, const struct rg_game *g, int paused)
{
	struct rg_sprite spr[RG_NSPR];
	unsigned char tiles[RG_BOX * RG_BOX * LCDG_TILE_BYTES];
	char msg[32];
	int bxs[RG_NSPR], bys[RG_NSPR];
	int x, y, i, k, n, cx, cy, anim = (int)((g->ticks >> 3) & 1);

	rg_hud(v, g);
	for (y = 0; y < RG_VIEW_H; y++)
		for (x = 0; x < RG_VIEW_W; x++)
			v->fin[(y + RG_HUD_ROWS) * GTILE_COLS + x] =
			    (unsigned char)rg_base_tile(g, g->cam + x, y);
	n = rg_sprites(g, spr);
	for (i = 0; i < n; i++) {
		bxs[i] = rg_fdiv8(spr[i].x - g->cam * RG_TW);
		bys[i] = rg_fdiv8(spr[i].y);
	}
	for (i = 0; i < RG_NSPR; i++) {
		struct rg_sig *sg = &v->sig[i];
		struct rg_sig now;

		if (i >= n) {
			sg->live = 0;
			continue;
		}
		memset(&now, 0, sizeof now);
		now.x = spr[i].x;
		now.y = spr[i].y;
		now.frame = spr[i].frame;
		now.flip = spr[i].flip;
		now.cam = g->cam;
		now.anim = anim;
		now.live = 1;
		now.gen = g->gen;
		for (k = 0; k < n; k++)
			if (k != i && rg_near(bxs[k], bxs[i]) && rg_near(bys[k], bys[i])) {
				now.ov[now.nov].s = spr[k];
				now.ov[now.nov].rel = k - i;
				now.nov++;
			}
		if (!sg->live || memcmp(sg, &now, sizeof now) != 0) {
			rg_composite(g, spr, n, i, bxs, bys, tiles);
			if (gtile_load(&v->gt, RG_DYN + i * RG_BOX * RG_BOX, RG_BOX * RG_BOX,
			    tiles) < 0)
				return -1;
			*sg = now;
		}
	}
	/* A cell two boxes cover shows the slot of the lowest-numbered sprite: it holds both. */
	for (i = n - 1; i >= 0; i--)
		for (cy = 0; cy < RG_BOX; cy++)
			for (cx = 0; cx < RG_BOX; cx++)
				if (bxs[i] + cx >= 0 && bxs[i] + cx < RG_VIEW_W &&
				    bys[i] + cy >= 0 && bys[i] + cy < RG_VIEW_H)
					v->fin[(bys[i] + cy + RG_HUD_ROWS) * GTILE_COLS + bxs[i] + cx] =
					    (unsigned char)(RG_DYN + (i * RG_BOX + cy) * RG_BOX + cx);
	if (paused) {
		rg_banner(v, 14, "PAUSED");
	} else if (g->state == RG_READY) {
		memcpy(msg, "LEVEL ", 6);
		msg[6] = (char)('1' + g->level);
		msg[7] = ' ';
		msg[8] = 0;
		strncat(msg, rg_levels[g->level].name, sizeof msg - 9);
		rg_banner(v, 14, msg);
	} else if (g->state == RG_CLEAR) {
		rg_banner(v, 14, "LEVEL CLEAR");
	} else if (g->state == RG_OVER) {
		rg_banner(v, 14, "GAME OVER  ENTER");
	} else if (g->state == RG_WON) {
		rg_banner(v, 14, "SUMMIT REACHED  ENTER");
	}
	for (y = 0; y < GTILE_ROWS; y++)
		for (x = 0; x < GTILE_COLS; x++)
			(void)gtile_set(&v->gt, x, y, v->fin[y * GTILE_COLS + x]);
	return gtile_flush(&v->gt);
}

#ifndef RIDGE_RENDER

/* ------------------------------------------------------------------ */
/* Front end                                                          */
/* ------------------------------------------------------------------ */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sgtty.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define RG_DEVICE	"/dev/lcdgfx"
#define RG_CATCHUP	4		/* ticks run for one frame at most */

/* rg_loop()'s status when the key session ended under it. */
#define RG_REVOKED	3

static volatile sig_atomic_t rg_got_signal;

static void
rg_on_signal(int sig)
{
	rg_got_signal = (sig_atomic_t)sig;
}

static void
rg_say(const char *s)
{
	size_t n = strlen(s);

	if (write(2, s, n) < 0)
		return;
}

static unsigned long
rg_now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, 0);
	return (unsigned long)tv.tv_sec * 1000UL + (unsigned long)tv.tv_usec / 1000UL;
}

/*
 * Read every waiting key event into k. Returns -1 when the session is gone:
 * the console left RAW or CBREAK, lost the foreground or the session ended.
 */
static int
rg_poll_keys(struct rg_keys *k)
{
	struct lcd_keyevs ev;
	int i, in = FREAD;

	do {
		memset(&ev, 0, sizeof ev);
		if (ioctl(0, LCDIOC_GETKEY, &ev) < 0)
			return -1;
		if (ev.lost != 0)
			rg_keys_lost(k);
		for (i = 0; i < ev.n; i++)
			rg_key_event(k, ev.ev[i].state, ev.ev[i].key);
	} while (ev.n == LCD_KEYEV_N);
	/*
	 * The drain succeeded, so the session still holds the console: the
	 * bytes the RAW tty queued for the same keys are dropped. TIOCFLUSH
	 * never waits, and ttyflush() leaves t_flags alone, so the flush does
	 * not end the session.
	 */
	ioctl(0, TIOCFLUSH, &in);
	return 0;
}

/*
 * Whether the key session still holds the console. picokbd_evq_check()
 * ends the session when the foreground group or the tty mode changes, and
 * a drain then fails, so a drain that succeeds proves the console is still
 * in the state ridge set. The events it removes are no longer needed.
 */
static int
rg_owns_console(void)
{
	struct lcd_keyevs ev;

	do {
		memset(&ev, 0, sizeof ev);
		if (ioctl(0, LCDIOC_GETKEY, &ev) < 0)
			return 0;
	} while (ev.n == LCD_KEYEV_N);
	return 1;
}

/*
 * Wait up to ms for console input. The bytes stay queued: rg_poll_keys()
 * drops them once a drain has shown that the session still holds the
 * console, so a program that took the console in between keeps its input.
 */
static void
rg_wait(int ms)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(0, &fds);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	(void)select(1, &fds, 0, 0, &tv);
}

/*
 * The timed loop; returns 0 on a request to quit, 1 when a device call fails
 * and RG_REVOKED when the key session ended under it.
 */
static int
rg_loop(struct rg_game *g, struct rg_view *v, struct rg_keys *k, int level)
{
	unsigned long last = rg_now_ms(), now, acc = 0;
	int paused = 0, n;

	rg_new_game(g, level);
	for (;;) {
		now = rg_now_ms();
		acc += now - last > 200 ? 200 : now - last;
		last = now;
		if (paused)
			acc = 0;
		for (n = 0; acc >= RG_TICK_MS && n < RG_CATCHUP; n++, acc -= RG_TICK_MS)
			rg_tick(g, rg_keys_input(k));
		if (acc >= RG_TICK_MS)
			acc = 0;
		if (paused)
			k->edge = 0;
		if (rg_draw(v, g, paused) < 0)
			return 1;
		rg_wait((int)(RG_TICK_MS - acc));
		if (rg_poll_keys(k) < 0)
			return RG_REVOKED;
		if (k->pause) {
			paused = !paused;
			k->pause = 0;
		}
		if (k->restart) {
			if (g->state == RG_OVER || g->state == RG_WON)
				rg_new_game(g, 0);
			k->restart = 0;
		}
		if (k->quit || rg_got_signal != 0)
			return 0;
	}
}

static int
rg_usage(void)
{
	rg_say("usage: ridge [level]\n");
	return 2;
}

int
main(int argc, char **argv)
{
	static struct rg_game game;
	static struct rg_view view;
	static struct rg_keys keys;
	struct sgttyb saved, raw;
	int fd, level = 0, on = 1, off = 0, status, pgrp;

	if (argc > 2)
		return rg_usage();
	if (argc == 2) {
		level = atoi(argv[1]);
		if (level < 1 || level > RG_NLEVELS)
			return rg_usage();
		level--;
	}
	if (ioctl(0, TIOCGETP, &saved) < 0) {
		rg_say("ridge: standard input is not a terminal\n");
		return 1;
	}
	/*
	 * The key session needs the foreground process group, so a background
	 * start is refused before the panel is taken. SIGTTOU is ignored for the
	 * whole run: ttioctl() in sys/kern/tty.c stops a process outside the
	 * foreground group at TIOCFLUSH and TIOCSETP unless it ignores SIGTTOU,
	 * and a stop there would leave /dev/lcdgfx open with the panel held.
	 */
	if (ioctl(0, TIOCGPGRP, &pgrp) < 0 || pgrp != getpgrp()) {
		rg_say("ridge: must run in the foreground of the console\n");
		return 1;
	}
	signal(SIGTTOU, SIG_IGN);
	fd = open(RG_DEVICE, O_WRONLY);
	if (fd < 0) {
		rg_say("ridge: needs " RG_DEVICE ", the PicoCalc pixel device\n");
		return 1;
	}
	signal(SIGINT, rg_on_signal);
	signal(SIGTERM, rg_on_signal);
	signal(SIGHUP, rg_on_signal);
	signal(SIGQUIT, rg_on_signal);
	raw = saved;
	raw.sg_flags |= RAW;
	raw.sg_flags &= ~ECHO;
	ioctl(0, TIOCSETP, &raw);
	if (ioctl(0, LCDIOC_KEYEVS, &on) < 0) {
		ioctl(0, TIOCSETP, &saved);
		close(fd);
		rg_say("ridge: needs the console's raw key events (LCDIOC_KEYEVS)\n");
		return 1;
	}
	status = rg_view_init(&view, fd) < 0 ? 1 : rg_loop(&game, &view, &keys, level);
	/*
	 * The tty is restored and its queues dropped only while the session
	 * still holds the console. A program that changed the mode or took the
	 * foreground ended the session, and its mode and queues are its own.
	 * Zero flushes both queues, so the key that quit the game never reaches
	 * the shell. The panel is released in every case.
	 */
	if (status != RG_REVOKED && rg_owns_console()) {
		ioctl(0, TIOCSETP, &saved);
		ioctl(0, TIOCFLUSH, &off);
	}
	ioctl(0, LCDIOC_KEYEVS, &off);
	close(fd);
	if (status == RG_REVOKED) {
		rg_say("ridge: the key session ended\n");
		return 1;
	}
	if (status == 1)
		rg_say("ridge: a device call failed\n");
	return status;
}

#endif /* !RIDGE_RENDER */

#endif /* !RIDGE_LOGIC */
