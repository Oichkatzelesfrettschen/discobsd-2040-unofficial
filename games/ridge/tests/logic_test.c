/*
 * Host test of the ridge rules. Includes ../ridge.c with RIDGE_LOGIC so the
 * same text that ships is the text tested.
 *
 *   logic_test
 *
 * Hand-derived vectors first: the rise of a full and of a cut jump tick by
 * tick, run acceleration and stopping distance, wall, ceiling and ledge
 * contacts, coyote time and jump buffering at their tick boundaries, then
 * shards, spikes, pits, checkpoints, stomps, the goal, scoring, level
 * progress, game over, the camera, the key-event translation and the level
 * data rules (starts, goals, crawler corridors, sprite budget). A random
 * property check compares rg_move_x and rg_move_y with a reference that
 * moves one sub-pixel unit at a time and finds contact by rectangle
 * intersection. A breadth-first search over the player's inputs then proves
 * every shipped level completable and replays the winning inputs through
 * rg_tick; the search is calibrated on a level with a gap one tile too wide.
 * The search is skipped after a failed vector, which keeps the mutant runs
 * short. Prints the counts and exits nonzero on any mismatch.
 */
#define RIDGE_LOGIC
#include "../ridge.c"

#include <stdio.h>
#include <stdlib.h>

static int nchk, nbad;

#define CHECK(cond, ...) do { \
	nchk++; \
	if (!(cond)) { \
		nbad++; \
		if (nbad <= 40) { \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} \
} while (0)

static struct rg_game G;

/* A flat arena of width w: rock from row 24 down, the player on tile 5. */
static void
arena(int w)
{
	int x, y;

	memset(&G, 0, sizeof G);
	G.w = w;
	G.lives = RG_LIVES;
	G.state = RG_PLAY;
	G.time_left = 3000;
	for (x = 0; x < w; x++)
		for (y = 24; y < RG_H; y++)
			G.map[x][y] = RG_ROCK;
	rg_place_start(&G, 5, 23);
	G.p.face = 1;
}

static void
settle(void)
{
	int i;

	for (i = 0; i < 4; i++)
		rg_player_step(&G, &G.p, 0);
}

static void
ticks(unsigned in, int n)
{
	while (n-- > 0)
		rg_tick(&G, in);
}

/* Tick with input `in` until the game reaches state st; the tick count, or -1 after max ticks. */
static int
until(unsigned in, int st, int max)
{
	int t;

	for (t = 1; t <= max; t++) {
		rg_tick(&G, in);
		if (G.state == st)
			return t;
	}
	return -1;
}

static void
test_jump_arcs(void)
{
	/* Sub-pixel rise above the start after each tick; derived by hand from
	 * a jump speed of 132, gravity 12 and a fall cap of 96. */
	static const int full[22] = { 120, 228, 324, 408, 480, 540, 588, 624, 648,
	    660, 660, 648, 624, 588, 540, 480, 408, 324, 228, 132, 36, 0 };
	static const int cut[11] = { 120, 156, 180, 192, 192, 180, 156, 120, 72, 12, 0 };
	int y0, t;

	arena(40);
	settle();
	CHECK(G.p.ground == 1, "player rests on the floor");
	y0 = G.p.y;
	CHECK(y0 == 24 * 128 - 14 * 16, "rests with its feet on the floor, y=%d", y0);
	for (t = 1; t <= 22; t++) {
		rg_player_step(&G, &G.p, t == 1 ? RG_IN_JUMP | RG_IN_JPRESS : RG_IN_JUMP);
		CHECK(y0 - G.p.y == full[t - 1], "full jump tick %d: rise %d want %d",
		    t, y0 - G.p.y, full[t - 1]);
		CHECK(G.p.ground == (t == 22), "full jump tick %d: ground %d", t, G.p.ground);
	}
	arena(40);
	settle();
	y0 = G.p.y;
	for (t = 1; t <= 11; t++) {
		rg_player_step(&G, &G.p, t == 1 ? RG_IN_JUMP | RG_IN_JPRESS : 0);
		CHECK(y0 - G.p.y == cut[t - 1], "cut jump tick %d: rise %d want %d",
		    t, y0 - G.p.y, cut[t - 1]);
	}
	CHECK(G.p.ground == 1, "cut jump has landed");
}

static void
test_run(void)
{
	static const int dx[6] = { 8, 24, 48, 80, 120, 160 };
	static const int stop[5] = { 160 + 32, 160 + 56, 160 + 72, 160 + 80, 160 + 80 };
	int x0, t;

	arena(40);
	settle();
	x0 = G.p.x;
	for (t = 1; t <= 6; t++) {
		rg_player_step(&G, &G.p, RG_IN_RIGHT);
		CHECK(G.p.x - x0 == dx[t - 1], "run tick %d: moved %d want %d", t,
		    G.p.x - x0, dx[t - 1]);
	}
	CHECK(G.p.vx == 40, "top speed %d", G.p.vx);
	for (t = 1; t <= 5; t++) {
		rg_player_step(&G, &G.p, 0);
		CHECK(G.p.x - x0 == stop[t - 1], "stop tick %d: moved %d want %d", t,
		    G.p.x - x0, stop[t - 1]);
	}
	CHECK(G.p.vx == 0, "stopped");
	/* In the air the speed builds by 6 a tick. */
	arena(40);
	settle();
	rg_player_step(&G, &G.p, RG_IN_JUMP | RG_IN_JPRESS | RG_IN_RIGHT);
	CHECK(G.p.vx == 8, "the jump tick still accelerates on the ground");
	for (t = 2; t <= 8; t++)
		rg_player_step(&G, &G.p, RG_IN_JUMP | RG_IN_RIGHT);
	CHECK(G.p.vx == 40 && G.p.ground == 0, "air speed %d", G.p.vx);
	arena(40);
	settle();
	rg_player_step(&G, &G.p, RG_IN_JUMP | RG_IN_JPRESS);
	for (t = 2; t <= 4; t++)
		rg_player_step(&G, &G.p, RG_IN_JUMP | RG_IN_LEFT);
	CHECK(G.p.vx == -18, "air acceleration is 6 a tick, vx=%d", G.p.vx);
}

static void
test_walls(void)
{
	int t;

	arena(40);
	G.map[12][23] = G.map[12][22] = G.map[12][21] = G.map[12][20] = RG_ROCK;
	settle();
	for (t = 0; t < 60; t++)
		rg_player_step(&G, &G.p, RG_IN_RIGHT);
	CHECK(G.p.x == 12 * 128 - 10 * 16, "right wall: x=%d", G.p.x);
	CHECK(G.p.vx == 0, "right wall stops the run");
	for (t = 0; t < 120; t++)
		rg_player_step(&G, &G.p, RG_IN_LEFT);
	CHECK(G.p.x == 0, "left edge of the map is a wall: x=%d", G.p.x);
	/* A ceiling slab: the box top ends flush with its underside. */
	arena(40);
	for (t = 3; t <= 9; t++)
		G.map[t][20] = RG_ROCK;
	settle();
	rg_player_step(&G, &G.p, RG_IN_JUMP | RG_IN_JPRESS);
	CHECK(G.p.y == 24 * 128 - 224 - 120, "first rise tick: y=%d", G.p.y);
	rg_player_step(&G, &G.p, RG_IN_JUMP);
	CHECK(G.p.y == 21 * 128 && G.p.vy == 0, "ceiling contact: y=%d vy=%d", G.p.y, G.p.vy);
	rg_player_step(&G, &G.p, RG_IN_JUMP);
	CHECK(G.p.vy == 12, "falls after the bump");
	/* The top of the map is a ceiling too. */
	arena(40);
	G.p.y = 0;
	G.p.vy = -90;
	rg_player_step(&G, &G.p, RG_IN_JUMP);
	CHECK(G.p.y == 0 && G.p.vy == 0, "map top: y=%d", G.p.y);
}

static void
test_ledges(void)
{
	int t, floor_y = 24 * 128 - 224;

	arena(40);
	for (t = 8; t <= 11; t++)
		G.map[t][20] = RG_LEDGE;
	rg_place_start(&G, 9, 23);
	settle();
	rg_player_step(&G, &G.p, RG_IN_JUMP | RG_IN_JPRESS);
	for (t = 0; t < 70; t++)
		rg_player_step(&G, &G.p, RG_IN_JUMP);
	CHECK(G.p.ground == 1 && G.p.y == 20 * 128 - 224,
	    "jumps up through a ledge and lands on it: y=%d", G.p.y);
	rg_player_step(&G, &G.p, RG_IN_DOWN);
	for (t = 0; t < 40; t++)
		rg_player_step(&G, &G.p, 0);
	CHECK(G.p.ground == 1 && G.p.y == floor_y, "drops through: y=%d", G.p.y);
	/* Without the drop key the ledge holds. */
	arena(40);
	for (t = 8; t <= 11; t++)
		G.map[t][20] = RG_LEDGE;
	rg_place_start(&G, 9, 19);
	for (t = 0; t < 40; t++)
		rg_player_step(&G, &G.p, 0);
	CHECK(G.p.ground == 1 && G.p.y == 20 * 128 - 224, "stands on a ledge: y=%d", G.p.y);
	/* A ledge does not block the sides. */
	arena(40);
	for (t = 8; t <= 11; t++)
		G.map[t][22] = RG_LEDGE;
	settle();
	for (t = 0; t < 50; t++)
		rg_player_step(&G, &G.p, RG_IN_RIGHT);
	CHECK(G.p.x > 14 * 128, "runs past a ledge at head height");
}

static void
test_grace(void)
{
	struct rg_player p;
	int x, t, k, first;

	/* A platform of ten columns over a pit. */
	arena(40);
	for (x = 10; x < 40; x++)
		for (t = 24; t < RG_H; t++)
			G.map[x][t] = RG_AIR;
	settle();
	first = 0;
	for (t = 1; t < 80 && !first; t++) {
		rg_player_step(&G, &G.p, RG_IN_RIGHT);
		if (!G.p.ground)
			first = t;
	}
	CHECK(first > 0, "walks off the edge");
	/* A jump k ticks after the first airborne tick: 1 to 3 work, 4 does not. */
	for (k = 1; k <= 5; k++) {
		p = G.p;
		for (t = 1; t < k; t++)
			rg_player_step(&G, &p, RG_IN_RIGHT);
		rg_player_step(&G, &p, RG_IN_RIGHT | RG_IN_JUMP | RG_IN_JPRESS);
		CHECK((p.vy < 0) == (k <= 3), "coyote jump %d ticks after leaving: vy=%d", k, p.vy);
	}
	/*
	 * A full jump lands at tick 22. A press at tick k stays buffered for
	 * five ticks, so it jumps on the first tick on the ground, 23, from
	 * k = 19 on and not before.
	 */
	for (k = 17; k <= 22; k++) {
		arena(40);
		settle();
		for (t = 1; t <= 23; t++)
			rg_player_step(&G, &G.p, RG_IN_JUMP | (t == 1 || t == k ? RG_IN_JPRESS : 0));
		CHECK(G.p.ground == (k < 19) || G.p.vy < 0, "press at tick %d: ground %d vy %d", k,
		    G.p.ground, G.p.vy);
		CHECK((k >= 19) == (G.p.vy < 0), "buffered press at tick %d: vy=%d", k, G.p.vy);
	}
}

static void
test_items_and_hazards(void)
{
	int x, y, t, n;
	struct rg_player p;

	/* Shards add 10 each and leave the map. */
	arena(40);
	G.map[8][23] = RG_SHARD;
	G.map[10][23] = RG_SHARD;
	G.nshards = 2;
	settle();
	ticks(RG_IN_RIGHT, 30);
	CHECK(G.shards == 2 && G.score == 20, "two shards: %d, score %lu", G.shards, G.score);
	CHECK(G.map[8][23] == RG_AIR && G.map[10][23] == RG_AIR, "shards leave the map");
	/* A pit costs one life and the respawn returns to the start. */
	arena(40);
	for (x = 10; x < 16; x++)
		for (y = 24; y < RG_H; y++)
			G.map[x][y] = RG_AIR;
	n = until(RG_IN_RIGHT, RG_DYING, 200);
	CHECK(n > 0 && G.lives == RG_LIVES - 1, "pit: died after %d ticks, lives %d", n, G.lives);
	ticks(0, RG_DYING_T);
	CHECK(G.state == RG_READY && G.p.x == G.spawn_x && G.p.y == G.spawn_y,
	    "respawn at the start, state %d", G.state);
	ticks(0, RG_RESPAWN_T);
	CHECK(G.state == RG_PLAY, "play resumes after the respawn delay");
	/* The pit ends the life only when the box is past the map's bottom edge. */
	memset(&p, 0, sizeof p);
	p.y = (RG_H + 2) * RG_T;
	CHECK(!rg_in_pit(&p), "two rows below the map is not yet a pit");
	p.y++;
	CHECK(rg_in_pit(&p), "beyond two rows below the map is a pit");
	/* Spikes: walking into two kills, a jump over lives. */
	arena(40);
	G.map[12][23] = G.map[13][23] = RG_SPIKE;
	n = until(RG_IN_RIGHT, RG_DYING, 100);
	CHECK(n > 0 && G.lives == RG_LIVES - 1, "spikes kill after %d ticks", n);
	arena(40);
	G.map[12][23] = G.map[13][23] = RG_SPIKE;
	for (t = 0; t < 60; t++) {
		unsigned in = RG_IN_RIGHT;

		if (G.p.x >= 8 * 128 && G.p.x < 8 * 128 + 80 && G.p.ground)
			in |= RG_IN_JUMP | RG_IN_JPRESS;
		else if (!G.p.ground)
			in |= RG_IN_JUMP;
		ticks(in, 1);
	}
	CHECK(G.state == RG_PLAY && G.lives == RG_LIVES && G.p.x > 15 * 128,
	    "jump over the spikes: state %d x %d", G.state, G.p.x);
	/* The hit region of a spike is inset by 2 pixels at the sides and 5 at the top. */
	arena(40);
	G.map[8][22] = RG_SPIKE;
	memset(&p, 0, sizeof p);
	p.x = 8 * 128;
	p.y = 22 * 128 + 80 - 224;
	CHECK(!rg_spike_hit(&G, &p), "bottom edge on the top of the hit region");
	p.y++;
	CHECK(rg_spike_hit(&G, &p), "one unit into the hit region");
	p.y = 22 * 128 + 100 - 224;
	p.x = 8 * 128 + 32 - 160;
	CHECK(!rg_spike_hit(&G, &p), "right edge on the left of the hit region");
	p.x++;
	CHECK(rg_spike_hit(&G, &p), "one unit into the left edge");
	p.x = 9 * 128 - 32 - 1;
	CHECK(rg_spike_hit(&G, &p), "one unit short of the right edge");
	p.x++;
	CHECK(!rg_spike_hit(&G, &p), "left edge on the right of the hit region");
}

static void
test_flag_and_checkpoint(void)
{
	int n;

	arena(60);
	G.map[9][23] = RG_FLAG;
	G.map[20][23] = RG_SPIKE;
	ticks(RG_IN_RIGHT, 20);
	CHECK(G.map[9][23] == RG_FLAGUP, "the flag rises");
	CHECK(G.spawn_x == 9 * 128 - 16 && G.spawn_y == 24 * 128 - 224, "spawn at the flag: %d,%d",
	    G.spawn_x, G.spawn_y);
	n = until(RG_IN_RIGHT, RG_DYING, 100);
	CHECK(n > 0, "runs into the spike beyond the flag");
	ticks(0, RG_DYING_T);
	CHECK(G.p.x == 9 * 128 - 16 && G.state == RG_READY, "respawns at the flag: x %d", G.p.x);
	CHECK(G.map[9][23] == RG_FLAGUP, "the flag stays up");
}

static void
test_enemies(void)
{
	struct rg_enemy *e;
	int t, lo, hi;

	/* A crawler turns at both ends of its corridor. */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = 20 * 128;
	e->x1 = 28 * 128 - 12 * 16;
	e->x = e->x0;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	ticks(0, 10);
	CHECK(e->x == e->x0 + 120, "a crawler walks 12 units a tick: %d", e->x - e->x0);
	lo = e->x;
	hi = e->x;
	for (t = 0; t < 400; t++) {
		ticks(0, 1);
		if (e->x < lo)
			lo = e->x;
		if (e->x > hi)
			hi = e->x;
	}
	CHECK(lo == e->x0 && hi == e->x1, "patrol limits %d..%d", lo, hi);
	/* Landing on top stomps: +50, a bounce, the crawler is gone. */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = 10 * 128;
	e->x = 10 * 128;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	G.p.x = 10 * 128 + 8;
	G.p.y = 24 * 128 - 224 - 8 * 16 - 20;
	G.p.vy = 60;
	G.p.ground = 0;
	ticks(0, 1);
	CHECK(e->alive == 0 && G.score == 50 && G.p.vy == -88 && G.state == RG_PLAY,
	    "stomp: alive %d score %lu vy %d state %d", e->alive, G.score, G.p.vy, G.state);
	ticks(0, 1);
	CHECK(G.p.vy == -76, "the bounce is not cut short by a released jump key: vy %d", G.p.vy);
	/*
	 * The stomp window is 5 pixels (80 units) below the crawler's top, by
	 * the height the box bottom had before the tick's move.
	 */
	for (t = 0; t < 2; t++) {
		arena(60);
		G.ne = 1;
		e = &G.en[0];
		e->x0 = e->x1 = 10 * 128;
		e->x = 10 * 128;
		e->y = 24 * 128 - 8 * 16;
		e->dir = 1;
		e->alive = 1;
		G.p.x = 10 * 128 + 8;
		G.p.y = e->y + 80 + t - 224;
		G.p.vy = 0;
		G.p.ground = 0;
		ticks(0, 1);
		CHECK(t == 0 ? e->alive == 0 : G.state == RG_DYING && e->alive == 1,
		    "box bottom %d units into the crawler before the tick: alive %d state %d", 80 + t,
		    e->alive, G.state);
	}
	/*
	 * Entering a crawler in the tick that lands on the floor: the box bottom
	 * was at pixel 187, inside the stomp window of a crawler whose top is at
	 * 184, and the floor at 192 stops the fall, which clears vy.
	 */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = e->x = 10 * 128;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	G.p.x = 10 * 128 + 8;
	G.p.y = 187 * 16 - 224;
	G.p.vy = 70;
	G.p.ground = 0;
	ticks(0, 1);
	CHECK(G.p.ground == 1 && G.p.y == 24 * 128 - 224, "the fall ended on the floor: y %d", G.p.y);
	CHECK(e->alive == 0 && G.score == 50 && G.state == RG_PLAY,
	    "stomp that lands in the same tick: alive %d score %lu state %d", e->alive, G.score, G.state);
	/* A climber at the top of its jump, level with the crawler's top, does not stomp: it is not falling. */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = e->x = 10 * 128;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	G.p.x = 10 * 128 + 8;
	G.p.y = e->y + 40 - 224;
	G.p.vy = -12;
	G.p.ground = 0;
	ticks(0, 1);
	CHECK(G.state == RG_DYING && e->alive == 1, "no stomp at the apex: state %d alive %d", G.state, e->alive);
	/* Standing on the floor beside a crawler's patrol is not a fall. */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = e->x = 10 * 128;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	G.p.x = 10 * 128 + 8;
	G.p.y = 24 * 128 - 224;
	G.p.ground = 1;
	ticks(0, 1);
	CHECK(G.state == RG_DYING && e->alive == 1, "a standing climber is hit, not a stomper");
	/* Running into the side costs a life. */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = 14 * 128;
	e->x = 14 * 128;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	until(RG_IN_RIGHT, RG_DYING, 100);
	CHECK(G.state == RG_DYING && G.lives == 2 && e->alive == 1,
	    "side hit: state %d lives %d", G.state, G.lives);
	/* A dead crawler is harmless and scores once. */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = 14 * 128;
	e->x = 14 * 128;
	e->y = 24 * 128 - 8 * 16;
	e->alive = 0;
	ticks(RG_IN_RIGHT, 60);
	CHECK(G.state == RG_PLAY && G.score == 0, "a dead crawler does nothing");
}

/*
 * A checkpoint inside a crawler's patrol: the crawler touches the spawn box
 * when the player comes back, and it does not move while the player is dying
 * or getting ready. The respawned player is safe from crawlers until the
 * grace period ends with no crawler touching.
 */
static void
test_respawn_grace(void)
{
	struct rg_enemy *e;
	int t, lives;

	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = 39 * 128;
	e->x1 = 45 * 128 - 12 * 16;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	G.spawn_x = 41 * 128 - 16;
	G.spawn_y = 24 * 128 - 224;
	G.p.x = G.spawn_x;
	G.p.y = G.spawn_y;
	e->x = G.p.x - 8 * 16;
	CHECK(G.p.grace == 0, "play begins without grace");
	t = until(0, RG_DYING, 40);
	CHECK(t > 0 && G.lives == RG_LIVES - 1, "a crawler's touch kills a player who has no grace: %d", t);
	ticks(0, RG_DYING_T);
	CHECK(G.state == RG_READY && G.p.grace == RG_GRACE, "respawn grants %d ticks of grace: state %d grace %d",
	    RG_GRACE, G.state, G.p.grace);
	ticks(0, RG_RESPAWN_T);
	CHECK(G.state == RG_PLAY, "play resumes");
	lives = G.lives;
	for (t = 0; t < 40; t++) {
		ticks(0, 1);
		CHECK(G.state == RG_PLAY && G.lives == lives, "tick %d after the respawn: state %d lives %d", t,
		    G.state, G.lives);
	}
	CHECK(G.p.grace > 0 && G.p.grace < RG_GRACE, "grace runs down while the crawler is clear: %d", G.p.grace);
	/* The crawler comes back through the spawn box after the grace period: the touch kills. */
	t = until(0, RG_DYING, 80);
	CHECK(t > 0 && G.p.grace == 0, "grace is finite: died after %d more ticks, grace %d", t, G.p.grace);
	/* A crawler that stays on the player holds the grace period open. */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = e->x = 10 * 128;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	G.p.x = 10 * 128 + 8;
	G.p.y = 24 * 128 - 224;
	G.p.grace = RG_GRACE;
	ticks(0, 3 * RG_GRACE);
	CHECK(G.state == RG_PLAY && G.p.grace == 1, "grace waits while a crawler touches: state %d grace %d",
	    G.state, G.p.grace);
	ticks(RG_IN_RIGHT, 12);
	CHECK(G.state == RG_PLAY && G.p.grace == 0, "grace ends once the player is clear: grace %d", G.p.grace);
	G.p.x = 10 * 128 + 8;
	ticks(0, 1);
	CHECK(G.state == RG_DYING, "touching after the grace period kills");
	/* A stomp still counts during the grace period. */
	arena(60);
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = e->x = 10 * 128;
	e->y = 24 * 128 - 8 * 16;
	e->dir = 1;
	e->alive = 1;
	G.p.x = 10 * 128 + 8;
	G.p.y = e->y + 80 - 224;
	G.p.grace = RG_GRACE;
	ticks(0, 1);
	CHECK(e->alive == 0 && G.score == 50, "a stomp during grace scores");
}

static void
test_goal_and_progress(void)
{
	int i;

	arena(60);
	G.map[14][23] = RG_GOALBASE;
	G.map[14][22] = RG_GOALTOP;
	G.time_left = 300;	/* ten seconds */
	ticks(RG_IN_RIGHT, 60);
	CHECK(G.state == RG_CLEAR, "goal reached, state %d", G.state);
	/* 100 for the level, 2 a second left, 100 for every shard taken (none here). */
	CHECK(G.score == (unsigned long)(100 + 2 * (G.time_left / 30) + 100), "clear bonus: score %lu left %d",
	    G.score, G.time_left);
	ticks(0, RG_CLEAR_T);
	CHECK(G.state == RG_READY && G.level == 1, "next level, state %d level %d", G.state, G.level);
	CHECK(G.w == 130 && G.lives == 3, "level 2 loaded: width %d", G.w);
	/* A missed shard forfeits the all-shards bonus. */
	arena(60);
	G.map[14][23] = RG_GOALBASE;
	G.map[14][22] = RG_GOALTOP;
	G.map[30][23] = RG_SHARD;
	G.nshards = 1;
	G.time_left = 0;
	ticks(RG_IN_RIGHT, 60);
	CHECK(G.state == RG_CLEAR && G.score == 100, "no all-shards bonus: %lu", G.score);
	/* The last level ends in a win. */
	arena(60);
	G.level = RG_NLEVELS - 1;
	G.map[14][23] = RG_GOALBASE;
	G.map[14][22] = RG_GOALTOP;
	ticks(RG_IN_RIGHT, 60);
	ticks(0, RG_CLEAR_T);
	CHECK(G.state == RG_WON, "last level won, state %d", G.state);
	ticks(RG_IN_RIGHT, 10);
	CHECK(G.state == RG_WON, "a won game stays won");
	/* The third death ends the game. */
	arena(40);
	G.lives = 1;
	G.map[12][23] = RG_SPIKE;
	until(RG_IN_RIGHT, RG_DYING, 100);
	CHECK(G.state == RG_DYING && G.lives == 0, "last life lost");
	ticks(0, RG_DYING_T);
	CHECK(G.state == RG_OVER, "game over, state %d", G.state);
	ticks(RG_IN_RIGHT, 10);
	CHECK(G.state == RG_OVER && G.lives == 0, "game over holds");
	G.score = 500;
	rg_new_game(&G, 2);
	CHECK(G.lives == RG_LIVES && G.score == 0 && G.level == 2 && G.state == RG_READY,
	    "new game from level 3");
	/* The ready delay holds the player still. */
	i = G.p.x;
	ticks(RG_IN_RIGHT, RG_READY_T - 1);
	CHECK(G.state == RG_READY && G.p.x == i, "ready holds input");
	ticks(RG_IN_RIGHT, 1);
	CHECK(G.state == RG_PLAY, "ready ends after %d ticks", RG_READY_T);
	/* The clock runs down one tick at a time during play only. */
	i = G.time_left;
	ticks(0, 10);
	CHECK(G.time_left == i - 10, "clock runs");
	/* The score saturates. */
	G.score = RG_SCORE_MAX - 5;
	rg_add_score(&G, 10);
	CHECK(G.score == RG_SCORE_MAX, "score saturates");
}

static void
test_camera(void)
{
	int t, prev, centre;

	arena(100);
	rg_cam_snap(&G);
	CHECK(G.cam == 0, "camera starts at the left edge");
	prev = G.cam;
	for (t = 0; t < 700; t++) {
		ticks(RG_IN_RIGHT, 1);
		CHECK(G.cam - prev >= 0 && G.cam - prev <= 1, "camera moves at most one column a tick");
		prev = G.cam;
		centre = G.p.x / 16 + 5 - G.cam * 8;
		if (G.cam > 0 && G.cam < G.w - RG_VIEW_W && G.state == RG_PLAY)
			CHECK(centre >= RG_DZ_LEFT * 8 - 8 && centre <= RG_DZ_RIGHT * 8 + 8,
			    "player outside the dead zone: %d", centre);
	}
	CHECK(G.cam == 60, "camera stops at the right edge: %d", G.cam);
	for (t = 0; t < 900; t++)
		ticks(RG_IN_LEFT, 1);
	CHECK(G.cam == 0, "camera follows back to the left edge: %d", G.cam);
	/* A respawn snaps the camera to the checkpoint in one step. */
	arena(100);
	G.spawn_x = 95 * 128;
	G.spawn_y = 24 * 128 - 224;
	rg_respawn(&G);
	CHECK(G.cam == 60, "snap on respawn is clamped to the right edge: %d", G.cam);
	arena(100);
	G.spawn_x = 50 * 128;
	G.spawn_y = 24 * 128 - 224;
	rg_respawn(&G);
	CHECK(G.cam == 30, "snap centers the player: %d", G.cam);
}

static int
feed(struct rg_keys *k, int state, int key)
{
	rg_key_event(k, state, key);
	return (int)rg_keys_input(k);
}

static void
test_keys(void)
{
	struct rg_keys k;
	unsigned in;

	memset(&k, 0, sizeof k);
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, LCD_KEY_RIGHT);
	CHECK(in == RG_IN_RIGHT, "right pressed: %u", in);
	in = rg_keys_input(&k);
	CHECK(in == RG_IN_RIGHT, "right stays held with no new event: %u", in);
	in = (unsigned)feed(&k, LCD_KEY_HOLD, LCD_KEY_RIGHT);
	CHECK(in == RG_IN_RIGHT, "hold keeps it down");
	in = (unsigned)feed(&k, LCD_KEY_RELEASED, LCD_KEY_RIGHT);
	CHECK(in == 0, "right released: %u", in);
	/* A tap between two ticks still moves for one tick. */
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_LEFT);
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_LEFT);
	in = rg_keys_input(&k);
	CHECK(in == RG_IN_LEFT, "a tap moves one tick: %u", in);
	in = rg_keys_input(&k);
	CHECK(in == 0, "the tap is consumed");
	/* Jump: the edge once, the held bit while down; a tap is an edge without hold. */
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, LCD_KEY_UP);
	CHECK(in == (RG_IN_JUMP | RG_IN_JPRESS), "jump pressed: %u", in);
	in = rg_keys_input(&k);
	CHECK(in == RG_IN_JUMP, "jump held: %u", in);
	in = (unsigned)feed(&k, LCD_KEY_HOLD, LCD_KEY_UP);
	CHECK(in == RG_IN_JUMP, "a hold event is no new press: %u", in);
	in = (unsigned)feed(&k, LCD_KEY_RELEASED, LCD_KEY_UP);
	CHECK(in == 0, "jump released");
	rg_key_event(&k, LCD_KEY_PRESSED, ' ');
	rg_key_event(&k, LCD_KEY_RELEASED, ' ');
	in = rg_keys_input(&k);
	CHECK(in == RG_IN_JPRESS, "a jump tap is a press without the held bit: %u", in);
	CHECK(k.restart == 1, "space also asks for a restart");
	k.restart = 0;
	/* Drop. */
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, LCD_KEY_DOWN);
	CHECK(in == RG_IN_DOWN, "drop pressed: %u", in);
	in = rg_keys_input(&k);
	CHECK(in == 0, "drop is an edge only: %u", in);
	feed(&k, LCD_KEY_RELEASED, LCD_KEY_DOWN);
	/* Letters, either case. */
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, 'L');
	CHECK(in == RG_IN_RIGHT, "capital L runs right: %u", in);
	feed(&k, LCD_KEY_RELEASED, 'l');
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, 'h');
	CHECK(in == RG_IN_LEFT, "h runs left: %u", in);
	feed(&k, LCD_KEY_RELEASED, 'H');
	in = rg_keys_input(&k);
	CHECK(in == 0, "released with the other case");
	/* Both directions cancel at the tick. */
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_LEFT);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_RIGHT);
	in = rg_keys_input(&k);
	CHECK(in == (RG_IN_LEFT | RG_IN_RIGHT), "both bits are passed on: %u", in);
	/* Lost events clear the held state and the down state of every key. */
	rg_keys_lost(&k);
	in = rg_keys_input(&k);
	CHECK(in == 0, "lost events clear the held keys: %u", in);
	in = (unsigned)feed(&k, LCD_KEY_HOLD, LCD_KEY_UP);
	CHECK(in == RG_IN_JUMP, "a hold event after lost events holds the key without a press: %u", in);
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_UP);
	rg_keys_input(&k);
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, LCD_KEY_UP);
	CHECK(in == (RG_IN_JUMP | RG_IN_JPRESS), "a press after a release is a new edge: %u", in);
	rg_keys_lost(&k);
	rg_keys_input(&k);
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, LCD_KEY_UP);
	CHECK(in == (RG_IN_JUMP | RG_IN_JPRESS), "lost events forget that the key was down: %u", in);
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_UP);
	rg_keys_input(&k);
	/* An unknown state or key changes nothing. */
	in = (unsigned)feed(&k, 9, LCD_KEY_LEFT);
	CHECK(in == 0, "unknown state ignored");
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, 'x');
	CHECK(in == 0, "unmapped key ignored");
	in = (unsigned)feed(&k, LCD_KEY_PRESSED, LCD_KEY_CTRL);
	CHECK(in == 0, "a modifier key is ignored");
	/* A held key repeats as PRESSED events: one edge until the release. */
	memset(&k, 0, sizeof k);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_UP);
	in = rg_keys_input(&k);
	CHECK(in == (RG_IN_JUMP | RG_IN_JPRESS), "first press of Up: %u", in);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_UP);
	in = rg_keys_input(&k);
	CHECK(in == RG_IN_JUMP, "a repeated PRESSED of Up is no new jump edge: %u", in);
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_UP);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_UP);
	in = rg_keys_input(&k);
	CHECK(in == (RG_IN_JUMP | RG_IN_JPRESS), "press after the release is an edge again: %u", in);
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_UP);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_DOWN);
	rg_keys_input(&k);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_DOWN);
	in = rg_keys_input(&k);
	CHECK(in == 0, "a repeated PRESSED of Down is no new drop: %u", in);
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_DOWN);
	memset(&k, 0, sizeof k);
	rg_key_event(&k, LCD_KEY_PRESSED, 'p');
	CHECK(k.pause == 1, "first p pauses");
	k.pause = 0;
	rg_key_event(&k, LCD_KEY_PRESSED, 'p');
	rg_key_event(&k, LCD_KEY_PRESSED, 'P');
	CHECK(k.pause == 0, "a held p does not toggle again");
	rg_key_event(&k, LCD_KEY_RELEASED, 'p');
	rg_key_event(&k, LCD_KEY_PRESSED, 'p');
	CHECK(k.pause == 1, "p after its release toggles");
	rg_key_event(&k, LCD_KEY_PRESSED, 'q');
	k.quit = 0;
	rg_key_event(&k, LCD_KEY_PRESSED, 'q');
	CHECK(k.quit == 0, "a held q does not request quit twice");
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_ESC);
	k.quit = 0;
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_ESC);
	CHECK(k.quit == 0, "a held Esc does not request quit twice");
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_ENTER);
	k.restart = 0;
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_ENTER);
	CHECK(k.restart == 0, "a held Enter does not confirm twice");
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_ENTER);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_ENTER);
	CHECK(k.restart == 1, "Enter after its release confirms");
	k.restart = 0;
	rg_key_event(&k, LCD_KEY_PRESSED, ' ');
	k.restart = 0;
	rg_key_event(&k, LCD_KEY_PRESSED, ' ');
	CHECK(k.restart == 0, "a held Space does not confirm twice");
	/* Aliased keys: Space and Up both jump, and the jump lasts until both are up. */
	memset(&k, 0, sizeof k);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_UP);
	rg_key_event(&k, LCD_KEY_PRESSED, ' ');
	in = rg_keys_input(&k);
	CHECK(in == (RG_IN_JUMP | RG_IN_JPRESS), "Up and Space together: %u", in);
	rg_key_event(&k, LCD_KEY_RELEASED, ' ');
	in = rg_keys_input(&k);
	CHECK(in == RG_IN_JUMP, "releasing Space keeps the jump while Up is down: %u", in);
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_UP);
	in = rg_keys_input(&k);
	CHECK(in == 0, "the jump ends when the last alias is released: %u", in);
	rg_key_event(&k, LCD_KEY_PRESSED, 'h');
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_LEFT);
	rg_key_event(&k, LCD_KEY_RELEASED, LCD_KEY_LEFT);
	rg_keys_input(&k);
	in = rg_keys_input(&k);
	CHECK(in == RG_IN_LEFT, "releasing Left keeps running left while h is down: %u", in);
	/* Requests. */
	memset(&k, 0, sizeof k);
	rg_key_event(&k, LCD_KEY_PRESSED, 'p');
	CHECK(k.pause == 1 && k.quit == 0, "p pauses");
	rg_key_event(&k, LCD_KEY_PRESSED, 'q');
	CHECK(k.quit == 1, "q quits");
	memset(&k, 0, sizeof k);
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_ESC);
	CHECK(k.quit == 1, "Esc quits");
	memset(&k, 0, sizeof k);
	rg_key_event(&k, LCD_KEY_HOLD, 'q');
	CHECK(k.quit == 0, "a held q is not a new press");
	rg_key_event(&k, LCD_KEY_PRESSED, LCD_KEY_ENTER);
	CHECK(k.restart == 1, "Enter restarts");
}

/* ---- Level data rules ---- */

static int
count_cells(int kind)
{
	int x, y, n = 0;

	for (x = 0; x < G.w; x++)
		for (y = 0; y < RG_H; y++)
			n += G.map[x][y] == kind;
	return n;
}

static int
supports(int x, int y)
{
	int k = rg_cell(&G, x, y);

	return k == RG_ROCK || k == RG_LEDGE;
}

/* A cell a crawler or the player can occupy: no rock, ledge or spike. */
static int
open_cell(int x, int y)
{
	int k = rg_cell(&G, x, y);

	return k != RG_ROCK && k != RG_LEDGE && k != RG_SPIKE;
}

static void
test_level_data(int lv)
{
	struct rg_player p;
	const struct rg_enemy *e;
	int i, x, y, cam, n, worst = 0, c0, c1;

	rg_new_game(&G, lv);
	CHECK(G.w == rg_levels[lv].w && G.w > RG_VIEW_W && G.w <= RG_MAXW, "level %d width %d", lv + 1, G.w);
	CHECK(count_cells(RG_GOALBASE) == 1 && count_cells(RG_GOALTOP) == 1, "level %d has one goal", lv + 1);
	CHECK(count_cells(RG_SHARD) == G.nshards && G.nshards > 0, "level %d shard count %d vs %d", lv + 1,
	    count_cells(RG_SHARD), G.nshards);
	p = G.p;
	for (i = 0; i < 6; i++)
		rg_player_step(&G, &p, 0);
	CHECK(p.ground == 1 && !rg_hit_rock(&G, p.x, p.y), "level %d start stands on the ground", lv + 1);
	CHECK(G.time_left > 600, "level %d has a clock", lv + 1);
	for (x = 0; x < G.w; x++)
		for (y = 0; y < RG_H; y++) {
			int k = G.map[x][y];

			if (k == RG_SPIKE || k == RG_FLAG || k == RG_GOALBASE)
				CHECK(rg_cell(&G, x, y + 1) == RG_ROCK, "level %d: %d at %d,%d needs rock below", lv + 1, k, x, y);
			if (k == RG_GOALTOP)
				CHECK(rg_cell(&G, x, y + 1) == RG_GOALBASE, "level %d goal top", lv + 1);
			if (k == RG_SPIKE)
				CHECK(rg_cell(&G, x, y - 1) != RG_ROCK, "level %d: spike at %d,%d is buried", lv + 1, x, y);
		}
	for (i = 0; i < G.ne; i++) {
		e = &G.en[i];
		c0 = e->x0 / RG_T;
		c1 = (e->x1 + RG_EW * RG_SUB - 1) / RG_T;
		y = e->y / RG_T;
		CHECK(c1 - c0 >= 4, "level %d crawler %d corridor of %d columns", lv + 1, i, c1 - c0 + 1);
		for (x = c0; x <= c1; x++) {
			CHECK(supports(x, y + 1), "level %d crawler %d has floor at column %d", lv + 1, i, x);
			CHECK(open_cell(x, y) && open_cell(x, y - 1) && open_cell(x, y - 2),
			    "level %d crawler %d needs headroom at column %d", lv + 1, i, x);
		}
	}
	/*
	 * A checkpoint's spawn box lies clear of every crawler patrol: the
	 * crawler's hit box over its whole corridor, in the rows the box spans.
	 */
	for (x = 0; x < G.w; x++)
		for (y = 0; y < RG_H; y++) {
			int sx, sy;

			if (G.map[x][y] != RG_FLAG)
				continue;
			sx = x * RG_T + (RG_T - RG_PW * RG_SUB) / 2;
			sy = (y + 1) * RG_T - RG_PH * RG_SUB;
			for (i = 0; i < G.ne; i++) {
				e = &G.en[i];
				CHECK(sx + RG_PW * RG_SUB <= e->x0 || sx >= e->x1 + RG_EW * RG_SUB ||
				    sy + RG_PH * RG_SUB <= e->y || sy >= e->y + RG_EH * RG_SUB,
				    "level %d: the checkpoint at column %d lies in crawler %d's patrol", lv + 1, x, i);
			}
		}
	/* No view ever holds more crawlers than there are sprite slots after the player. */
	for (cam = 0; cam + RG_VIEW_W <= G.w; cam++) {
		n = 0;
		for (i = 0; i < G.ne; i++)
			if (G.en[i].x1 / RG_SUB + 16 > cam * RG_TW &&
			    G.en[i].x0 / RG_SUB - 16 < (cam + RG_VIEW_W) * RG_TW)
				n++;
		if (n > worst)
			worst = n;
	}
	CHECK(worst <= 6, "level %d shows %d crawlers at once", lv + 1, worst);
}

/* ---- Movement against a reference ---- */

static unsigned long rng_state = 12345;

static unsigned
rnd(unsigned n)
{
	rng_state = rng_state * 6364136223846793005UL + 1442695040888963407UL;
	return (unsigned)(rng_state >> 33) % n;
}

/* Does the box [x, x + w) by [y, y + h) overlap a rock cell of the map or the outside? */
static int
ref_rock(const struct rg_game *g, int x, int y)
{
	int tx, ty;

	for (tx = -1; tx <= g->w; tx++)
		for (ty = -1; ty < RG_H; ty++) {
			int rock;

			if (tx < 0 || tx >= g->w || ty < 0)
				rock = 1;
			else
				rock = g->map[tx][ty] == RG_ROCK;
			if (!rock)
				continue;
			if (x < (tx + 1) * 128 && x + 160 > tx * 128 &&
			    y < (ty + 1) * 128 && y + 224 > ty * 128)
				return 1;
		}
	return 0;
}

/* Does lowering the box by one unit carry its bottom across the top of a ledge cell? */
static int
ref_ledge(const struct rg_game *g, int x, int y)
{
	int tx, ty;

	for (tx = 0; tx < g->w; tx++)
		for (ty = 0; ty < RG_H; ty++) {
			if (g->map[tx][ty] != RG_LEDGE)
				continue;
			if (x < (tx + 1) * 128 && x + 160 > tx * 128 &&
			    y + 224 <= ty * 128 && y + 224 + 1 > ty * 128)
				return 1;
		}
	return 0;
}

static void
ref_move_x(const struct rg_game *g, struct rg_player *p, int dx)
{
	int s = dx > 0 ? 1 : -1, i, n = dx > 0 ? dx : -dx;

	for (i = 0; i < n; i++) {
		if (ref_rock(g, p->x + s, p->y)) {
			p->vx = 0;
			return;
		}
		p->x += s;
	}
}

static void
ref_move_y(const struct rg_game *g, struct rg_player *p, int dy)
{
	int s = dy > 0 ? 1 : -1, i, n = dy > 0 ? dy : -dy;

	for (i = 0; i < n; i++) {
		if (ref_rock(g, p->x, p->y + s)) {
			if (s > 0)
				p->ground = 1;
			p->vy = 0;
			return;
		}
		if (s > 0 && p->drop == 0 && ref_ledge(g, p->x, p->y)) {
			p->ground = 1;
			p->vy = 0;
			return;
		}
		p->y += s;
	}
}

static void
test_collision_reference(void)
{
	static struct rg_game g;
	struct rg_player a, b;
	int trial, x, y, tries, run = 0, grounded = 0, blocked = 0;

	for (trial = 0; trial < 3000; trial++) {
		memset(&g, 0, sizeof g);
		g.w = 10;
		for (x = 0; x < g.w; x++)
			for (y = 0; y < RG_H; y++) {
				unsigned r = rnd(100);

				if (y < 14)
					continue;
				g.map[x][y] = r < 22 ? RG_ROCK : r < 34 ? RG_LEDGE : RG_AIR;
			}
		for (tries = 0; tries < 50; tries++) {
			memset(&a, 0, sizeof a);
			a.x = (int)rnd(10 * 128 - 160);
			a.y = 14 * 128 + (int)rnd(18 * 128 - 224);
			if (!ref_rock(&g, a.x, a.y))
				break;
		}
		if (tries == 50)
			continue;
		a.drop = (int)rnd(2);
		a.vx = (int)rnd(193) - 96;
		a.vy = (int)rnd(193) - 96;
		b = a;
		rg_move_x(&g, &a, a.vx);
		ref_move_x(&g, &b, b.vx);
		CHECK(a.x == b.x && a.vx == b.vx, "trial %d: move_x x %d/%d vx %d/%d", trial, a.x, b.x, a.vx, b.vx);
		a.ground = b.ground = 0;
		rg_move_y(&g, &a, a.vy);
		ref_move_y(&g, &b, b.vy);
		CHECK(a.y == b.y && a.vy == b.vy && a.ground == b.ground,
		    "trial %d: move_y y %d/%d vy %d/%d ground %d/%d", trial, a.y, b.y, a.vy, b.vy,
		    a.ground, b.ground);
		run++;
		grounded += a.ground;
		blocked += a.vx == 0 || a.vy == 0;
	}
	CHECK(run > 2500 && grounded > 100 && blocked > 300,
	    "the random trials exercise the contact rules: %d run, %d grounded, %d blocked",
	    run, grounded, blocked);
}

/* ---- Reachability search ---- */

#define BFS_MAXN	1500000
#define BFS_TABLE	(1 << 22)
#define BFS_WEIGHT	2		/* weighted A*: heuristic ticks count double */

struct bnode {
	int x, y, parent, g;
	short vx, vy;
	unsigned char ground, coyote, jbuf, jumping, drop, pj, in, face;
};

static struct bnode *bnodes;
static unsigned long long *btable;
static int *bheap, bheap_n, bgoal_x, bgoal_y;

static unsigned long long
bkey(const struct bnode *n)
{
	unsigned long long k = (unsigned long long)(n->x / 32);

	k = k << 10 | (unsigned long long)(n->y / 16);
	k = k << 8 | (unsigned long long)((n->vx + 100) / 8);
	k = k << 6 | (unsigned long long)(n->vy / 24 + 20);
	k = k << 1 | n->ground;
	k = k << 3 | (n->coyote > 0);
	k = k << 3 | (n->jbuf > 0);
	k = k << 1 | n->jumping;
	k = k << 2 | n->drop;
	k = k << 1 | n->pj;
	return k + 1;
}

static int
bseen(unsigned long long key)
{
	unsigned long long h = key * 0x9e3779b97f4a7c15ULL;
	unsigned i = (unsigned)(h >> 41) & (BFS_TABLE - 1);

	while (btable[i] != 0) {
		if (btable[i] == key)
			return 1;
		i = (i + 1) & (BFS_TABLE - 1);
	}
	btable[i] = key;
	return 0;
}

/* Estimated ticks to the goal: its horizontal distance at top speed, or the climb at about 3.7 pixels a tick. */
static int
bcost(const struct bnode *n)
{
	int dx = (n->x - bgoal_x) / RG_SUB, up = (n->y - bgoal_y) / RG_SUB, h;

	dx = dx < 0 ? -dx : dx;
	h = dx * 2 / 5;
	if (up > 0 && up * 10 / 37 > h)
		h = up * 10 / 37;
	return n->g + BFS_WEIGHT * h;
}

static void
bpush(int i)
{
	int c = bheap_n++, p;

	while (c > 0) {
		p = (c - 1) / 2;
		if (bcost(&bnodes[bheap[p]]) <= bcost(&bnodes[i]))
			break;
		bheap[c] = bheap[p];
		c = p;
	}
	bheap[c] = i;
}

static int
bpop(void)
{
	int top = bheap[0], last = bheap[--bheap_n], c = 0, k;

	if (bheap_n == 0)
		return top;
	for (;;) {
		k = 2 * c + 1;
		if (k >= bheap_n)
			break;
		if (k + 1 < bheap_n && bcost(&bnodes[bheap[k + 1]]) < bcost(&bnodes[bheap[k]]))
			k++;
		if (bcost(&bnodes[last]) <= bcost(&bnodes[bheap[k]]))
			break;
		bheap[c] = bheap[k];
		c = k;
	}
	bheap[c] = last;
	return top;
}

static void
to_player(const struct bnode *n, struct rg_player *p)
{
	memset(p, 0, sizeof *p);
	p->x = n->x;
	p->y = n->y;
	p->vx = n->vx;
	p->vy = n->vy;
	p->ground = n->ground;
	p->coyote = n->coyote;
	p->jbuf = n->jbuf;
	p->jumping = n->jumping;
	p->drop = n->drop;
	p->face = n->face ? 1 : -1;
}

/*
 * Weighted A* over the inputs {left, none, right} x {jump up, jump down}
 * tick by tick from the loaded level's start until the player touches the
 * goal; every state that touches a spike or leaves through a pit is dropped.
 * Returns the tick count and fills inputs[] (RG_IN_* of each tick), or -1
 * when the reachable states are exhausted or the node budget maxn is spent;
 * the goal's node counts against the budget like any other and is never
 * written beyond it.
 */
static int
solve(const struct rg_game *g, unsigned char *inputs, int cap, int *nstates, int maxn)
{
	struct rg_player p;
	int tail = 0, found = -1, exhausted = 0, d, j, x, y;
	struct bnode n0;

	if (maxn < 1) {
		*nstates = 0;
		return -1;
	}
	bgoal_x = bgoal_y = 0;
	for (x = 0; x < g->w; x++)
		for (y = 0; y < RG_H; y++)
			if (g->map[x][y] == RG_GOALBASE) {
				bgoal_x = x * RG_T;
				bgoal_y = y * RG_T;
			}
	memset(btable, 0, sizeof(*btable) * BFS_TABLE);
	bheap_n = 0;
	memset(&n0, 0, sizeof n0);
	n0.x = g->p.x;
	n0.y = g->p.y;
	n0.parent = -1;
	n0.face = 1;
	bnodes[tail++] = n0;
	bseen(bkey(&n0));
	bpush(0);
	while (bheap_n > 0 && found < 0 && !exhausted) {
		int head = bpop();
		struct bnode cur = bnodes[head];

		for (d = -1; d <= 1 && found < 0; d++)
			for (j = 0; j <= 1 && found < 0; j++) {
				unsigned in = (d < 0 ? RG_IN_LEFT : d > 0 ? RG_IN_RIGHT : 0) |
				    (j ? RG_IN_JUMP : 0) | (j && !cur.pj ? RG_IN_JPRESS : 0);
				struct bnode nn;

				to_player(&cur, &p);
				rg_player_step(g, &p, in);
				if (rg_in_pit(&p) || rg_spike_hit(g, &p))
					continue;
				nn.x = p.x;
				nn.y = p.y;
				nn.vx = (short)p.vx;
				nn.vy = (short)p.vy;
				nn.ground = (unsigned char)p.ground;
				nn.coyote = (unsigned char)p.coyote;
				nn.jbuf = (unsigned char)p.jbuf;
				nn.jumping = (unsigned char)p.jumping;
				nn.drop = (unsigned char)p.drop;
				nn.pj = (unsigned char)j;
				nn.face = p.face > 0;
				nn.in = (unsigned char)((d < 0 ? RG_IN_LEFT : d > 0 ? RG_IN_RIGHT : 0) |
				    (j ? RG_IN_JUMP : 0));
				nn.parent = head;
				nn.g = cur.g + 1;
				if (rg_goal_hit(g, &p)) {
					if (tail >= maxn) {
						exhausted = 1;
						break;
					}
					bnodes[tail++] = nn;
					found = tail - 1;
					break;
				}
				if (tail >= maxn || bseen(bkey(&nn)))
					continue;
				bnodes[tail] = nn;
				bpush(tail++);
			}
	}
	*nstates = tail;
	if (found < 0)
		return -1;
	{
		int len = 0, i = found;

		while (bnodes[i].parent >= 0) {
			len++;
			i = bnodes[i].parent;
		}
		if (len > cap)
			return -1;
		i = found;
		for (j = len - 1; j >= 0; j--) {
			inputs[j] = bnodes[i].in;
			i = bnodes[i].parent;
		}
		return len;
	}
}

/* Replay the found inputs through the game itself, crawlers removed. */
static int
replay(int lv, const unsigned char *inputs, int n)
{
	int i, pj = 0;

	rg_new_game(&G, lv);
	G.ne = 0;
	ticks(0, RG_READY_T);
	for (i = 0; i < n; i++) {
		unsigned in = inputs[i];

		if ((in & RG_IN_JUMP) && !pj)
			in |= RG_IN_JPRESS;
		pj = (in & RG_IN_JUMP) != 0;
		rg_tick(&G, in);
		if (G.state != RG_PLAY)
			break;
	}
	return G.state;
}

static void
test_solvable(void)
{
	static unsigned char inputs[4000];
	int lv, len, nst, x, y, st;

	bnodes = malloc(sizeof(*bnodes) * BFS_MAXN);
	btable = malloc(sizeof(*btable) * BFS_TABLE);
	bheap = malloc(sizeof(*bheap) * BFS_MAXN);
	if (!bnodes || !btable || !bheap) {
		CHECK(0, "no memory for the search");
		return;
	}
	for (lv = 0; lv < RG_NLEVELS; lv++) {
		rg_new_game(&G, lv);
		len = solve(&G, inputs, (int)sizeof inputs, &nst, BFS_MAXN);
		CHECK(len > 0, "level %d is completable (%d states searched)", lv + 1, nst);
		if (len <= 0)
			continue;
		CHECK(2 * (len * RG_TICK_MS / 1000) <= rg_levels[lv].par,
		    "level %d: a %d-tick route leaves under half of the %d s par for a player", lv + 1, len,
		    rg_levels[lv].par);
		st = replay(lv, inputs, len);
		CHECK(st == RG_CLEAR && G.lives == RG_LIVES, "level %d replay ends in state %d, lives %d", lv + 1,
		    st, G.lives);
		printf("level %d: %d ticks (%d s) by search, %d states\n", lv + 1, len,
		    len * RG_TICK_MS / 1000, nst);
	}
	/* Calibration: a pit of 5 columns is crossable, one of 9 is not. */
	for (x = 5; x <= 9; x += 4) {
		arena(60);
		for (y = 24; y < RG_H; y++) {
			int c;

			for (c = 12; c < 12 + x; c++)
				G.map[c][y] = RG_AIR;
		}
		G.map[40][23] = RG_GOALBASE;
		G.map[40][22] = RG_GOALTOP;
		len = solve(&G, inputs, (int)sizeof inputs, &nst, BFS_MAXN);
		CHECK((len > 0) == (x == 5), "a pit of %d columns: search says %d", x, len);
	}
	/* And a wall of 6 rows is not climbable, one of 4 is. */
	for (x = 4; x <= 6; x += 2) {
		arena(60);
		for (y = 24 - x; y < 24; y++)
			G.map[14][y] = RG_ROCK;
		G.map[40][23] = RG_GOALBASE;
		G.map[40][22] = RG_GOALTOP;
		len = solve(&G, inputs, (int)sizeof inputs, &nst, BFS_MAXN);
		CHECK((len > 0) == (x == 4), "a wall of %d rows: search says %d", x, len);
	}
	/*
	 * Budget boundary: the search of a short flat run needs nst nodes, the
	 * goal's among them. With arrays of exactly that size it succeeds; one
	 * node fewer, or none, must fail without a write past the arrays.
	 */
	arena(60);
	G.map[12][23] = RG_GOALBASE;
	G.map[12][22] = RG_GOALTOP;
	len = solve(&G, inputs, (int)sizeof inputs, &nst, BFS_MAXN);
	CHECK(len > 0 && nst > 2, "the short run is solvable with %d nodes", nst);
	if (len > 0 && nst > 2) {
		int full = nst, lenfull = len, budget;

		for (budget = full + 1; budget >= full - 1; budget -= 1) {
			free(bnodes);
			free(bheap);
			bnodes = malloc(sizeof(*bnodes) * (size_t)budget);
			bheap = malloc(sizeof(*bheap) * (size_t)budget);
			len = solve(&G, inputs, (int)sizeof inputs, &nst, budget);
			CHECK((len > 0) == (budget >= full) && (len <= 0 || len == lenfull),
			    "budget %d of %d nodes: result %d", budget, full, len);
			CHECK(nst <= budget, "the search keeps within its %d nodes: %d", budget, nst);
		}
		free(bnodes);
		free(bheap);
		bnodes = malloc(sizeof(*bnodes));
		bheap = malloc(sizeof(*bheap));
		len = solve(&G, inputs, (int)sizeof inputs, &nst, 1);
		CHECK(len < 0 && nst == 1, "a budget of one node fails: %d, %d", len, nst);
		len = solve(&G, inputs, (int)sizeof inputs, &nst, 0);
		CHECK(len < 0 && nst == 0, "a budget of zero fails: %d, %d", len, nst);
	}
	free(bnodes);
	free(btable);
	free(bheap);
}

static void
test_constants(void)
{
	/* The level table is consistent with its constants. */
	CHECK(sizeof rg_levels / sizeof rg_levels[0] == RG_NLEVELS, "level table size");
	CHECK(RG_T == 128 && RG_PW * RG_SUB == 160 && RG_PH * RG_SUB == 224, "box sizes in sub-pixels");
	CHECK(RG_STEP * 2 <= RG_T && RG_STEP > 0, "no collision step spans a tile");
}

int
main(void)
{
	int lv;

	test_constants();
	test_jump_arcs();
	test_run();
	test_walls();
	test_ledges();
	test_grace();
	test_items_and_hazards();
	test_flag_and_checkpoint();
	test_enemies();
	test_respawn_grace();
	test_goal_and_progress();
	test_camera();
	test_keys();
	test_collision_reference();
	for (lv = 0; lv < RG_NLEVELS; lv++)
		test_level_data(lv);
	if (nbad == 0)
		test_solvable();
	else
		printf("search skipped after %d failed checks\n", nbad);
	printf("%d checks, %d failed\n", nchk, nbad);
	return nbad != 0;
}
