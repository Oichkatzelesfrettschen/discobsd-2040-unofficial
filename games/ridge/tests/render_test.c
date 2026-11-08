/*
 * Host test of ridge's render half through a model of /dev/lcdgfx.
 *
 *   render_test
 *
 * The two device calls of games/gametile.h are bound to a model before
 * ../ridge.c is included with RIDGE_RENDER. The model applies the kernel's
 * contract (an atlas of 128 tiles of 32 bytes, LCDG_TILESET uploads, an
 * LCDG_TILEWIN block drawn row by row through the atlas as it stands when
 * the row completes, a 16-entry palette) and keeps the pixels of every
 * panel cell, so the test compares what the panel shows. It checks that
 *
 *   - the first frame paints all 1,600 cells once and an unchanged frame
 *     sends nothing at all;
 *   - on a level without the far ridge, scrolling by one column sends the
 *     two windows of the two cells that changed, scrolling by five sends
 *     two runs of four with the unchanged cell between them left alone,
 *     and a one-pixel step of the player sends the three rows of its
 *     sprite box and one atlas upload;
 *   - the static tiles, palette, HUD text, sprite pixels (flip and frame)
 *     are those the rules ask for, against values written out here;
 *   - after hundreds of frames of play on every level, and in every state
 *     with and without pause, the panel the incremental frames left equals
 *     the panel a fresh view paints from the same game state.
 *
 * Prints the counts and exits nonzero on any mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/lcdgfx.h>

static int model_ioctl(int fd, unsigned long cmd, void *arg);
static long model_write(int fd, const void *buf, size_t n);

#define GTILE_IOCTL(fd, cmd, arg)	model_ioctl((fd), (cmd), (arg))
#define GTILE_WRITE(fd, buf, n)		model_write((fd), (buf), (n))

#define RIDGE_RENDER
#include "../ridge.c"

#define LOG_MAX		4096

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

struct dev {
	unsigned char atlas[LCDG_ATLAS_N][LCDG_TILE_BYTES];
	unsigned char panel[GTILE_CELLS][LCDG_TILE_BYTES];
	unsigned char drawn[GTILE_CELLS];
	unsigned char pal[LCDG_NPAL][3];
	int nioctl, nwrite, nwin, ntiles, nset, npal;
	struct {
		int tx, ty, tw, th;
	} wlog[LOG_MAX];
	int wx, wy, ww, wh, wleft, wfill;
	unsigned char wrow[GTILE_COLS];
	unsigned tpos, tleft;
};

static struct dev DA, DB, *cur = &DA;

static void
dev_reset(struct dev *d)
{
	memset(d, 0, sizeof *d);
}

/* Forget the counters, not the panel: the start of a measured frame. */
static void
dev_count_reset(struct dev *d)
{
	d->nioctl = d->nwrite = d->nwin = d->ntiles = d->nset = d->npal = 0;
}

static int
model_ioctl(int fd, unsigned long cmd, void *arg)
{
	struct dev *d = cur;
	const struct lcdg_tilewin *w = arg;
	const struct lcdg_tileset *t = arg;
	const struct lcdg_palette *p = arg;

	(void)fd;
	d->nioctl++;
	d->wleft = 0;
	d->tleft = 0;
	if (cmd == LCDG_TILEWIN) {
		if (w->tw == 0 || w->th == 0 || w->tx + w->tw > LCDG_GRID ||
		    w->ty + w->th > LCDG_GRID) {
			printf("FAIL model: LCDG_TILEWIN %d,%d %dx%d out of range\n", w->tx, w->ty,
			    w->tw, w->th);
			nbad++;
			return -1;
		}
		if (d->nwin < LOG_MAX) {
			d->wlog[d->nwin].tx = w->tx;
			d->wlog[d->nwin].ty = w->ty;
			d->wlog[d->nwin].tw = w->tw;
			d->wlog[d->nwin].th = w->th;
		}
		d->wx = w->tx;
		d->wy = w->ty;
		d->ww = w->tw;
		d->wh = w->th;
		d->wleft = d->ww * d->wh;
		d->wfill = 0;
		d->nwin++;
		return 0;
	}
	if (cmd == LCDG_TILESET) {
		if (t->count == 0 || t->first + t->count > LCDG_ATLAS_N) {
			printf("FAIL model: LCDG_TILESET %d,%d out of range\n", t->first, t->count);
			nbad++;
			return -1;
		}
		d->tpos = (unsigned)t->first * LCDG_TILE_BYTES;
		d->tleft = (unsigned)t->count * LCDG_TILE_BYTES;
		d->nset++;
		return 0;
	}
	if (cmd == LCDG_PALETTE) {
		if (p->index >= LCDG_NPAL) {
			printf("FAIL model: palette index %d\n", p->index);
			nbad++;
			return -1;
		}
		d->pal[p->index][0] = p->r;
		d->pal[p->index][1] = p->g;
		d->pal[p->index][2] = p->b;
		d->npal++;
		return 0;
	}
	printf("FAIL model: unexpected command %lx\n", cmd);
	nbad++;
	return -1;
}

static long
model_write(int fd, const void *buf, size_t n)
{
	struct dev *d = cur;
	const unsigned char *p = buf;
	size_t i;

	(void)fd;
	d->nwrite++;
	if (d->tleft != 0) {
		if (n > d->tleft)
			n = d->tleft;
		memcpy((unsigned char *)d->atlas + d->tpos, p, n);
		d->tpos += (unsigned)n;
		d->tleft -= (unsigned)n;
		return (long)n;
	}
	if (d->wleft == 0) {
		printf("FAIL model: write with no stream open\n");
		nbad++;
		return -1;
	}
	if ((int)n > d->wleft)
		n = (size_t)d->wleft;
	for (i = 0; i < n; i++) {
		if (p[i] >= LCDG_ATLAS_N) {
			printf("FAIL model: tile number %d beyond the atlas\n", p[i]);
			nbad++;
			return -1;
		}
		d->wrow[d->wfill++] = p[i];
		d->wleft--;
		if (d->wfill == d->ww) {
			int c;

			for (c = 0; c < d->ww; c++) {
				int cell = d->wy * GTILE_COLS + d->wx + c;

				memcpy(d->panel[cell], d->atlas[d->wrow[c]], LCDG_TILE_BYTES);
				d->drawn[cell] = 1;
				d->ntiles++;
			}
			d->wfill = 0;
			d->wy++;
		}
	}
	return (long)n;
}

static unsigned long rng_state = 777;

static unsigned
rnd(unsigned n)
{
	rng_state = rng_state * 6364136223846793005UL + 1442695040888963407UL;
	return (unsigned)(rng_state >> 33) % n;
}

static struct rg_game G;
static struct rg_view V, V2;
static int ridge_off;		/* The far ridge is cleared after each view init. */

/* Palette index of panel pixel (x, y) on device d. */
static int
panel_px(const struct dev *d, int x, int y)
{
	unsigned char b = d->panel[(y / 8) * GTILE_COLS + x / 8][(y % 8) * 4 + (x % 8) / 2];

	return x % 2 == 0 ? b >> 4 : b & 15;
}

/* A flat level of width w with the far ridge switched off: rock from row 24, the player on tile 5. */
static void
flat_level(int w)
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
	G.p.ground = 1;
	ridge_off = 1;
}

static void
start_view(struct dev *d, struct rg_view *v)
{
	cur = d;
	dev_reset(d);
	memset(v, 0, sizeof *v);
	CHECK(rg_view_init(v, 3) == 0, "view init");
	if (ridge_off)
		memset(rg_ridge_row, 255, sizeof rg_ridge_row);
}

static int
frame(struct dev *d, struct rg_view *v, int paused)
{
	int r;

	cur = d;
	dev_count_reset(d);
	r = rg_draw(v, &G, paused);
	CHECK(r >= 0, "draw failed");
	return r;
}

static void
test_static_tiles(void)
{
	struct dev *d = &DA;
	int i, t;

	start_view(d, &V);
	CHECK(d->npal == LCDG_NPAL, "all %d palette entries set: %d", LCDG_NPAL, d->npal);
	CHECK(d->pal[0][0] == 28 && d->pal[0][2] == 80 && d->pal[14][0] == 246 && d->pal[15][2] == 26,
	    "palette values");
	for (i = 0; i < LCDG_TILE_BYTES; i++) {
		CHECK(d->atlas[T_SKY][i] == 0x00, "sky tile byte %d", i);
		CHECK(d->atlas[T_ABYSS][i] == 0x33, "abyss tile byte %d", i);
		CHECK(d->atlas[T_HUD][i] == 0x22, "HUD ground tile byte %d", i);
	}
	/* The flat contour: row 7 of the sky tile in palette entry 1, two pixels a byte. */
	for (i = 0; i < LCDG_TILE_BYTES; i++)
		CHECK(d->atlas[T_CFLAT][i] == (i >= 28 ? 0x11 : 0x00), "flat contour byte %d", i);
	/* The rising contour runs from the bottom left to the top right. */
	for (i = 0; i < 8; i++)
		CHECK(((d->atlas[T_CRISE][(7 - i) * 4 + i / 2] >> (i % 2 ? 0 : 4)) & 15) == 1,
		    "rise pixel %d", i);
	/* The glyph '0': 01110 in the top row, bits placed from column 1 of an HUD-colored tile. */
	CHECK(d->atlas[T_FONT][0] == 0x22 && d->atlas[T_FONT][1] == 0x22, "glyph blank top row");
	CHECK(d->atlas[T_FONT][4] == 0x22 && d->atlas[T_FONT][5] == 0xee && d->atlas[T_FONT][6] == 0xe2,
	    "glyph 0 second row: %02x %02x %02x", d->atlas[T_FONT][4], d->atlas[T_FONT][5],
	    d->atlas[T_FONT][6]);
	/* Every static tile has been uploaded: the model's atlas is non-empty beyond sky. */
	for (t = T_ROCK; t < RG_NSTATIC; t++) {
		int nz = 0;

		for (i = 0; i < LCDG_TILE_BYTES; i++)
			nz |= d->atlas[t][i] != 0;
		CHECK(nz, "static tile %d uploaded", t);
	}
	CHECK(RG_NSTATIC <= RG_DYN && RG_DYN + RG_NSPR * RG_BOX * RG_BOX <= LCDG_ATLAS_N,
	    "static tiles and sprite slots fit the atlas: %d, %d", RG_NSTATIC,
	    RG_DYN + RG_NSPR * RG_BOX * RG_BOX);
}

static void
test_first_and_idle(void)
{
	struct dev *d = &DA;
	int w, x, y, k;

	flat_level(100);
	G.cam = 40;
	start_view(d, &V);
	w = frame(d, &V, 0);
	CHECK(d->ntiles == GTILE_CELLS && w == GTILE_ROWS, "first frame paints every cell once: %d tiles, %d windows",
	    d->ntiles, w);
	for (k = 0; k < GTILE_CELLS; k++)
		CHECK(d->drawn[k], "cell %d drawn", k);
	/* Layout on the panel: HUD rows, sky, the rock cap at world row 24, rock, the abyss rows. */
	CHECK(V.fin[0 * 40 + 0] == T_HUD && V.fin[1 * 40 + 39] == T_HUD, "HUD ground");
	CHECK(V.fin[(2 + 0) * 40 + 10] == T_SKY, "sky at the top of the world");
	CHECK(V.fin[(2 + 24) * 40 + 10] == T_CAP, "lichen cap on the first rock row");
	CHECK(V.fin[(2 + 25) * 40 + 10] == T_ROCK, "rock below the cap");
	CHECK(V.fin[(2 + 31) * 40 + 10] == T_ROCK && V.fin[(2 + 32) * 40 + 10] == T_ABYSS &&
	    V.fin[39 * 40 + 10] == T_ABYSS, "the abyss begins below the map");
	for (y = 2; y < 40; y++)
		for (x = 0; x < 40; x++)
			if (V.fin[y * 40 + x] >= RG_DYN)
				CHECK(0, "no sprite is in view at %d,%d", x, y);
	/* An unchanged frame costs no device call. */
	w = frame(d, &V, 0);
	CHECK(w == 0 && d->nioctl == 0 && d->nwrite == 0, "idle frame: %d windows, %d ioctls, %d writes", w,
	    d->nioctl, d->nwrite);
	/* The HUD numbers: score, lives, level and shards as font tiles. */
	G.score = 1234;
	G.lives = 2;
	G.shards = 3;
	G.nshards = 12;
	w = frame(d, &V, 0);
	CHECK(V.fin[0 * 40 + 1] == T_FONT + 10 + ('S' - 'A') && V.fin[0 * 40 + 5] == T_FONT + 10 + ('E' - 'A'),
	    "SCORE spelled");
	CHECK(V.fin[0 * 40 + 7] == T_FONT && V.fin[0 * 40 + 8] == T_FONT &&
	    V.fin[0 * 40 + 9] == T_FONT + 1 && V.fin[0 * 40 + 10] == T_FONT + 2 &&
	    V.fin[0 * 40 + 11] == T_FONT + 3 && V.fin[0 * 40 + 12] == T_FONT + 4, "score 001234");
	CHECK(V.fin[0 * 40 + 22] == T_FONT + 2, "two lives");
	CHECK(V.fin[0 * 40 + 32] == T_FONT + 1, "level 1");
	CHECK(V.fin[1 * 40 + 8] == T_FONT && V.fin[1 * 40 + 9] == T_FONT + 3 &&
	    V.fin[1 * 40 + 10] == T_FONT + 36 && V.fin[1 * 40 + 11] == T_FONT + 1 &&
	    V.fin[1 * 40 + 12] == T_FONT + 2, "shards 03/12");
	CHECK(V.fin[1 * 40 + 26] == T_FONT + 10 + ('F' - 'A'), "level name starts with F");
	CHECK(w >= 1 && d->ntiles <= 12, "a score change repaints only its digits: %d tiles", d->ntiles);
}

static void
test_scroll_runs(void)
{
	struct dev *d = &DA;
	int w;

	flat_level(100);
	for (int x = 60; x <= 63; x++)
		G.map[x][18] = RG_LEDGE;
	G.cam = 40;
	start_view(d, &V);
	frame(d, &V, 0);
	/* One column: the ledge's left end moves into column 19 and its right end leaves column 23. */
	G.cam = 41;
	w = frame(d, &V, 0);
	CHECK(w == 2 && d->ntiles == 2 && d->nset == 0, "scroll by one column: %d windows, %d tiles, %d uploads",
	    w, d->ntiles, d->nset);
	CHECK(d->wlog[0].tx == 19 && d->wlog[0].ty == 20 && d->wlog[0].tw == 1 && d->wlog[0].th == 1,
	    "first window %d,%d %dx%d", d->wlog[0].tx, d->wlog[0].ty, d->wlog[0].tw, d->wlog[0].th);
	CHECK(d->wlog[1].tx == 23 && d->wlog[1].ty == 20 && d->wlog[1].tw == 1 && d->wlog[1].th == 1,
	    "second window %d,%d %dx%d", d->wlog[1].tx, d->wlog[1].ty, d->wlog[1].tw, d->wlog[1].th);
	CHECK(panel_px(d, 19 * 8 + 3, 20 * 8) == P_ROCK_L && panel_px(d, 23 * 8 + 3, 20 * 8) == P_SKY,
	    "the ledge moved one cell to the left on the panel");
	/* And back. */
	G.cam = 40;
	w = frame(d, &V, 0);
	CHECK(w == 2 && d->ntiles == 2, "scroll back: %d windows, %d tiles", w, d->ntiles);
	/* Five columns: two runs of four around the one unchanged cell. */
	G.cam = 45;
	w = frame(d, &V, 0);
	CHECK(w == 2 && d->ntiles == 8, "scroll by five columns: %d windows, %d tiles", w, d->ntiles);
	CHECK(d->wlog[0].tx == 15 && d->wlog[0].tw == 4 && d->wlog[1].tx == 20 && d->wlog[1].tw == 4 &&
	    d->wlog[0].ty == 20 && d->wlog[1].ty == 20, "runs at %d+%d and %d+%d", d->wlog[0].tx,
	    d->wlog[0].tw, d->wlog[1].tx, d->wlog[1].tw);
	/* A scroll across bare ground changes nothing. */
	G.cam = 10;
	frame(d, &V, 0);
	G.cam = 11;
	w = frame(d, &V, 0);
	CHECK(w == 0 && d->ntiles == 0, "scroll over flat ground sends nothing: %d windows", w);
}

static void
test_sprite_steps(void)
{
	struct dev *d = &DA;
	int w, k, sx, sy, r, c, px;

	flat_level(100);
	/* Sprite top left at world pixel (80, 176): box cells 10..12 by 22..24. */
	G.p.x = (80 + 3) * RG_SUB;
	G.p.y = 24 * RG_T - RG_PH * RG_SUB;
	G.cam = 0;
	start_view(d, &V);
	frame(d, &V, 0);
	w = frame(d, &V, 0);
	CHECK(w == 0 && d->nioctl == 0, "a still sprite costs nothing: %d windows %d ioctls", w, d->nioctl);
	G.p.x += RG_SUB;
	w = frame(d, &V, 0);
	CHECK(w == 3 && d->ntiles == 9 && d->nset == 1, "one pixel step: %d windows, %d tiles, %d uploads", w,
	    d->ntiles, d->nset);
	for (k = 0; k < 3 && k < w; k++)
		CHECK(d->wlog[k].tx == 10 && d->wlog[k].ty == 24 + k && d->wlog[k].tw == 3 && d->wlog[k].th == 1,
		    "window %d: %d,%d %dx%d", k, d->wlog[k].tx, d->wlog[k].ty, d->wlog[k].tw, d->wlog[k].th);
	/* A step across a cell edge moves the box one column: four columns change, 3 rows each. */
	G.p.x += 7 * RG_SUB;
	w = frame(d, &V, 0);
	CHECK(w == 3 && d->ntiles == 12, "step across a cell edge: %d windows %d tiles", w, d->ntiles);
	for (k = 0; k < w; k++)
		CHECK(d->wlog[k].tx == 10 && d->wlog[k].tw == 4, "window %d spans columns %d+%d", k,
		    d->wlog[k].tx, d->wlog[k].tw);
	/* The pixels of the standing frame, mirrored when facing left. */
	flat_level(100);
	G.p.x = (80 + 3) * RG_SUB;
	G.p.y = 24 * RG_T - RG_PH * RG_SUB;
	start_view(d, &V);
	frame(d, &V, 0);
	sx = 80;
	sy = 176 + 16;
	px = panel_px(d, sx + 10, sy + 6);
	CHECK(px == 14, "the eye of the standing frame is white at column 10 of row 6: %d", px);
	CHECK(panel_px(d, sx + 5, sy + 6) == 13, "the hood pixel at column 5 is rust: %d",
	    panel_px(d, sx + 5, sy + 6));
	G.p.face = -1;
	frame(d, &V, 0);
	CHECK(panel_px(d, sx + 5, sy + 6) == 14 && panel_px(d, sx + 10, sy + 6) != 14,
	    "facing left mirrors the sprite: %d %d", panel_px(d, sx + 5, sy + 6), panel_px(d, sx + 10, sy + 6));
	/* Transparent pixels show the world: sky above the head, the ink outline on its top row. */
	CHECK(panel_px(d, sx + 1, sy + 2) == 0, "sky beside the head");
	CHECK(panel_px(d, sx + 7, sy + 2) == 15, "outline on the hood's top row: %d", panel_px(d, sx + 7, sy + 2));
	/* The jump frame has outline pixels at the left foot that the standing frame lacks. */
	G.p.face = 1;
	G.p.ground = 0;
	frame(d, &V, 0);
	CHECK(panel_px(d, sx + 2, sy + 14) == 15, "jump frame foot outline: %d", panel_px(d, sx + 2, sy + 14));
	G.p.ground = 1;
	frame(d, &V, 0);
	CHECK(panel_px(d, sx + 2, sy + 14) == 0, "standing frame has no pixel there: %d", panel_px(d, sx + 2, sy + 14));
	/* Running alternates the two run frames every four ticks of motion. */
	G.p.vx = 40;
	G.p.anim = 0;
	frame(d, &V, 0);
	r = panel_px(d, sx + 3, sy + 15);
	G.p.anim = 4;
	frame(d, &V, 0);
	c = panel_px(d, sx + 3, sy + 15);
	CHECK(r != c, "the two run frames differ at a foot pixel: %d %d", r, c);
}

static void
test_sprite_refresh(void)
{
	struct dev *d = &DA;
	int w;

	/*
	 * A shard beside a still sprite changes its animation frame every eight
	 * ticks and the cell it sits in lies in the sprite's box, so the box is
	 * recomposited although the sprite did not move.
	 */
	flat_level(100);
	G.p.x = (80 + 3) * RG_SUB;
	G.p.y = 24 * RG_T - RG_PH * RG_SUB;
	G.cam = 0;
	G.map[12][23] = RG_SHARD;
	start_view(d, &V);
	frame(d, &V, 0);
	G.ticks = 8;
	w = frame(d, &V, 0);
	CHECK(w >= 1 && d->nset == 1, "the box is recomposited on the shard's frame change: %d windows %d uploads",
	    w, d->nset);
	/* The same when the map changes under a still sprite. */
	G.map[12][23] = RG_AIR;
	G.gen++;
	w = frame(d, &V, 0);
	CHECK(w >= 1 && d->nset == 1, "the box is recomposited when its map cell changes: %d windows %d uploads", w,
	    d->nset);
	start_view(&DB, &V2);
	frame(&DB, &V2, 0);
	CHECK(memcmp(DA.panel, DB.panel, sizeof DA.panel) == 0, "incremental and fresh panels agree");
}

/* The far ridge: every column's contour meets the next column's within a pixel, wrap included. */
static void
test_ridge_contour(void)
{
	int i, lo = 99, hi = 0, row, ty, nrow, nty, right, left;

	rg_ridge_init();
	for (i = 0; i < RG_RIDGE_N; i++) {
		row = rg_ridge_row[i];
		ty = rg_ridge_ty[i];
		nrow = rg_ridge_row[(i + 1) % RG_RIDGE_N];
		nty = rg_ridge_ty[(i + 1) % RG_RIDGE_N];
		/* Pixel row of the stroke at the right edge of this tile and the left edge of the next. */
		right = row * 8 + (ty == T_CRISE ? 0 : 7);
		left = nrow * 8 + (nty == T_CFALL ? 0 : 7);
		CHECK(right - left >= -1 && right - left <= 1, "ridge column %d: stroke at %d meets %d", i, right, left);
		if (row < lo)
			lo = row;
		if (row > hi)
			hi = row;
	}
	CHECK(lo >= 8 && hi <= 15, "the ridge stays in the sky: rows %d to %d", lo, hi);
	CHECK(hi - lo >= 4, "the ridge has relief: rows %d to %d", lo, hi);
}

/* Shards twinkle every eight ticks. */
static void
test_shard_frames(void)
{
	flat_level(100);
	G.map[41][20] = RG_SHARD;
	G.cam = 40;
	start_view(&DA, &V);
	G.ticks = 0;
	frame(&DA, &V, 0);
	CHECK(V.fin[(2 + 20) * 40 + 1] == T_SHARD0, "shard frame 0");
	G.ticks = 7;
	frame(&DA, &V, 0);
	CHECK(V.fin[(2 + 20) * 40 + 1] == T_SHARD0, "shard frame still 0 at tick 7");
	G.ticks = 8;
	frame(&DA, &V, 0);
	CHECK(V.fin[(2 + 20) * 40 + 1] == T_SHARD1, "shard frame 1 at tick 8");
	G.ticks = 16;
	frame(&DA, &V, 0);
	CHECK(V.fin[(2 + 20) * 40 + 1] == T_SHARD0, "shard frame 0 at tick 16");
}

/* The climber and a crawler in one frame with the camera away from the left edge. */
static void
test_two_sprites(void)
{
	struct dev *d = &DA;
	struct rg_enemy *e;
	int ex, ey;

	flat_level(100);
	G.cam = 3;
	G.p.x = (3 * 8 + 80 + 3) * RG_SUB;
	G.p.y = 24 * RG_T - RG_PH * RG_SUB;
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = e->x = 152 * RG_SUB;
	e->y = 24 * RG_T - RG_EH * RG_SUB;
	e->dir = 1;
	e->alive = 1;
	start_view(d, &V);
	frame(d, &V, 0);
	/* The climber's eye is at column 10 of row 6 of the sprite at world x 104; the crawler's at column 12 of row 11 at 150. */
	ex = 104 - 3 * 8 + 10;
	ey = 176 + 16 + 6;
	CHECK(panel_px(d, ex, ey) == P_WHITE, "climber's eye with the camera at column 3: %d", panel_px(d, ex, ey));
	CHECK(panel_px(d, 150 - 3 * 8 + 12, 176 + 16 + 11) == P_WHITE, "crawler's eye: %d",
	    panel_px(d, 150 - 3 * 8 + 12, 176 + 16 + 11));
	/* The sprite's box shows the ground under its feet where it has no pixels. */
	CHECK(panel_px(d, 104 - 3 * 8 + 1, 208) == P_LICHEN_L, "ground in the sprite's lowest cell row: %d",
	    panel_px(d, 104 - 3 * 8 + 1, 208));
	/* The camera moves one column with both sprites still: they move left by eight pixels. */
	G.cam = 4;
	frame(d, &V, 0);
	CHECK(panel_px(d, ex - 8, ey) == P_WHITE, "climber follows the camera: %d", panel_px(d, ex - 8, ey));
	CHECK(panel_px(d, 150 - 4 * 8 + 12, 176 + 16 + 11) == P_WHITE, "crawler follows the camera");
	CHECK(panel_px(d, ex, ey) != P_WHITE, "nothing is left at the old column");
	start_view(&DB, &V2);
	frame(&DB, &V2, 0);
	CHECK(memcmp(DA.panel, DB.panel, sizeof DA.panel) == 0, "incremental and fresh panels agree");
	/* Facing: the crawler walking left mirrors its eye to column 3. */
	e->dir = -1;
	frame(d, &V, 0);
	CHECK(panel_px(d, 150 - 4 * 8 + 3, 176 + 16 + 11) == P_WHITE, "crawler facing left: %d",
	    panel_px(d, 150 - 4 * 8 + 3, 176 + 16 + 11));
	/* A dead crawler leaves its cells to the world. */
	e->alive = 0;
	frame(d, &V, 0);
	CHECK(panel_px(d, 150 - 4 * 8 + 3, 176 + 16 + 11) == P_SKY, "dead crawler is gone: %d",
	    panel_px(d, 150 - 4 * 8 + 3, 176 + 16 + 11));
	CHECK(V.sig[1].live == 0, "its sprite slot is free");
}

/*
 * A crawler standing still with the last cell of its box beyond the right
 * edge of the view: when the camera scrolls, that cell comes into view and
 * the sprite has to be composited over the world tile that is there.
 */
static void
test_edge_sprite_uncovered(void)
{
	struct dev *d = &DA;
	struct rg_enemy *e;

	flat_level(100);
	G.cam = 4;
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = e->x = (343 + 2) * RG_SUB;
	e->y = 24 * RG_T - RG_EH * RG_SUB;
	e->dir = 1;
	e->alive = 1;
	G.map[44][23] = RG_LEDGE;
	start_view(d, &V);
	frame(d, &V, 0);
	G.cam = 5;
	frame(d, &V, 0);
	start_view(&DB, &V2);
	frame(&DB, &V2, 0);
	CHECK(memcmp(DA.panel, DB.panel, sizeof DA.panel) == 0, "a still sprite is recomposited when its box cell comes into view");
	/* World cell 44 is view column 39 now: the ledge's top row shows right of the sprite's last pixel. */
	CHECK(panel_px(d, 39 * 8 + 7, 16 + 23 * 8) == P_ROCK_L, "the ledge shows beside the sprite: %d",
	    panel_px(d, 39 * 8 + 7, 16 + 23 * 8));
}
/*
 * Boxes that share tiles: the climber at x 244 stands two pixels clear of a
 * crawler at x 256, and the crawler's box covers two of the climber's cells.
 * The shared tiles hold both sprites, so the climber's eye at (251, 198)
 * stays white.
 */
static void
test_shared_tiles_pixel(void)
{
	struct dev *d = &DA;
	struct rg_enemy *e;

	flat_level(100);
	G.cam = 0;
	G.p.x = 244 * RG_SUB;
	G.p.y = 24 * RG_T - RG_PH * RG_SUB;
	G.ne = 1;
	e = &G.en[0];
	e->x0 = e->x1 = e->x = 256 * RG_SUB;
	e->y = 24 * RG_T - RG_EH * RG_SUB;
	e->dir = 1;
	e->alive = 1;
	start_view(d, &V);
	frame(d, &V, 0);
	CHECK(panel_px(d, 251, 198) == P_WHITE, "the climber's eye in a tile the crawler's box shares: %d",
	    panel_px(d, 251, 198));
	CHECK(panel_px(d, 266, 203) == P_WHITE, "the crawler's eye: %d", panel_px(d, 266, 203));
	/* The crawler's outline and body, in the cells it shares with the climber, are there too. */
	CHECK(panel_px(d, 256, 202) == P_INK && panel_px(d, 259, 203) == P_LICHEN_D,
	    "the crawler's pixels in a shared tile: %d %d", panel_px(d, 256, 202), panel_px(d, 259, 203));
	CHECK(V.fin[(2 + 22) * 40 + 31] == RG_DYN + 1 && V.fin[(2 + 22) * 40 + 33] == RG_DYN + RG_BOX * RG_BOX + 2,
	    "the shared cell shows the lower sprite's slot, the crawler's own cell its own: %d %d",
	    V.fin[(2 + 22) * 40 + 31], V.fin[(2 + 22) * 40 + 33]);
	/* The crawler walks away: the climber's cells stay right, and nothing is left of the crawler's. */
	e->x0 = e->x1 = e->x = 300 * RG_SUB;
	frame(d, &V, 0);
	CHECK(panel_px(d, 251, 198) == P_WHITE, "the climber alone");
	/* And back: moving only the crawler refreshes the climber's slots. */
	e->x0 = e->x1 = e->x = 256 * RG_SUB;
	frame(d, &V, 0);
	CHECK(panel_px(d, 251, 198) == P_WHITE, "the climber with the crawler back");
	start_view(&DB, &V2);
	frame(&DB, &V2, 0);
	CHECK(memcmp(DA.panel, DB.panel, sizeof DA.panel) == 0, "incremental and fresh panels agree");
}

/* A respawned climber blinks while its grace period runs: shown on two ticks in four. */
static void
test_grace_blink(void)
{
	struct dev *d = &DA;

	flat_level(100);
	G.cam = 0;
	G.p.x = (80 + 3) * RG_SUB;
	G.p.y = 24 * RG_T - RG_PH * RG_SUB;
	G.p.grace = 5;
	start_view(d, &V);
	G.ticks = 0;
	frame(d, &V, 0);
	CHECK(panel_px(d, 90, 16 + 176 + 6) == P_WHITE, "the climber shows on tick 0 of its grace");
	G.ticks = 4;
	frame(d, &V, 0);
	CHECK(panel_px(d, 90, 16 + 176 + 6) == P_SKY, "the climber is hidden on tick 4 of its grace: %d",
	    panel_px(d, 90, 16 + 176 + 6));
	G.p.grace = 0;
	frame(d, &V, 0);
	CHECK(panel_px(d, 90, 16 + 176 + 6) == P_WHITE, "no blink without grace");
}

/*
 * Reference image of the world area: terrain, then each sprite in z-order
 * (the highest index first, so the climber is on top), pixel by pixel.
 */
static unsigned char ref_img[RG_VIEW_H * 8][RG_VIEW_W * 8];

static void
ref_render(const struct rg_game *g, const struct rg_sprite *spr, int n)
{
	unsigned char px[8][8];
	int cx, cy, x, y, k, c;

	for (cy = 0; cy < RG_VIEW_H; cy++)
		for (cx = 0; cx < RG_VIEW_W; cx++) {
			rg_tile_px(rg_base_tile(g, g->cam + cx, cy), px);
			for (y = 0; y < 8; y++)
				memcpy(&ref_img[cy * 8 + y][cx * 8], px[y], 8);
		}
	for (k = n - 1; k >= 0; k--)
		for (y = 0; y < 16; y++)
			for (x = 0; x < 16; x++) {
				int X = spr[k].x - g->cam * RG_TW + x, Y = spr[k].y + y;

				c = rg_art_pal(rg_art[spr[k].frame][y][spr[k].flip ? 15 - x : x]);
				if (c >= 0 && X >= 0 && X < RG_VIEW_W * 8 && Y >= 0 && Y < RG_VIEW_H * 8)
					ref_img[Y][X] = (unsigned char)c;
			}
}

/* Count the pixels of the world area of DA that differ from the reference image of G. */
static int
ref_diff(int *shared)
{
	struct rg_sprite spr[RG_NSPR];
	int n = rg_sprites(&G, spr), i, x, y, bad = 0;

	for (i = 1; i < n; i++)
		if (rg_near(rg_fdiv8(spr[i].x - G.cam * RG_TW), rg_fdiv8(spr[0].x - G.cam * RG_TW)) &&
		    rg_near(rg_fdiv8(spr[i].y), rg_fdiv8(spr[0].y)))
			(*shared)++;
	ref_render(&G, spr, n);
	for (y = 0; y < RG_VIEW_H * 8; y++)
		for (x = 0; x < RG_VIEW_W * 8; x++)
			if (panel_px(&DA, x, y + 16) != ref_img[y][x])
				bad++;
	return bad;
}

/*
 * Two crawlers in one slot's box change places in the sprite list while the
 * sprite in the slot and its neighbor's pixels stay the same: only the
 * neighbor's index relative to the slot's tells that the z-order flipped.
 */
static void
test_overlap_swap(void)
{
	struct rg_enemy *e;
	int i, shared = 0, bad;

	flat_level(100);
	G.cam = 0;
	G.p.x = 10 * RG_SUB;
	G.ne = 3;
	for (i = 0; i < 3; i++) {
		e = &G.en[i];
		e->y = 24 * RG_T - RG_EH * RG_SUB;
		e->alive = 1;
	}
	start_view(&DA, &V);
	/* c1 is G at 206 facing left, c2 is F at 200 facing right, c3 is far away. */
	G.en[0].x0 = G.en[0].x1 = G.en[0].x = 206 * RG_SUB;
	G.en[0].dir = -1;
	G.en[1].x0 = G.en[1].x1 = G.en[1].x = 200 * RG_SUB;
	G.en[1].dir = 1;
	G.en[2].x0 = G.en[2].x1 = G.en[2].x = 280 * RG_SUB;
	G.en[2].dir = 1;
	frame(&DA, &V, 0);
	bad = ref_diff(&shared);
	CHECK(bad == 0, "overlapping crawlers: %d pixels differ", bad);
	/* c1 goes far away and c3 becomes G: slot 2 holds F again with G now below it. */
	G.en[0].x0 = G.en[0].x1 = G.en[0].x = 280 * RG_SUB;
	G.en[0].dir = 1;
	G.en[2].x0 = G.en[2].x1 = G.en[2].x = 206 * RG_SUB;
	G.en[2].dir = -1;
	frame(&DA, &V, 0);
	bad = ref_diff(&shared);
	CHECK(bad == 0, "after G moves to a higher index: %d pixels differ", bad);
}

/*
 * Random clusters of the climber and up to six crawlers, boxes meeting in
 * every way, moved a few at a time between frames so that a still sprite
 * has a moving neighbor: the panel equals the reference after every frame.
 */
static void
test_overlap_random(void)
{
	struct dev *d = &DA;
	int iter, i, bad, x, shared = 0, anchor;

	flat_level(100);
	for (x = 20; x < 30; x++)
		G.map[x][21] = RG_LEDGE;
	G.map[40][23] = RG_SHARD;
	G.map[41][20] = RG_SPIKE;
	G.cam = 3;
	G.ne = RG_NSPR - 1;
	G.p.x = 100 * RG_SUB;
	G.p.y = 24 * RG_T - RG_PH * RG_SUB;
	anchor = 100;
	for (i = 0; i < G.ne; i++) {
		G.en[i].x0 = G.en[i].x1 = G.en[i].x = (anchor + 6 * i) * RG_SUB;
		G.en[i].y = 24 * RG_T - RG_EH * RG_SUB;
		G.en[i].alive = 1;
		G.en[i].dir = 1;
	}
	start_view(d, &V);
	for (iter = 0; iter < 400; iter++) {
		if (iter % 50 == 0)
			anchor = 40 + (int)rnd(200);
		if (rnd(2)) {
			G.p.x = (anchor + (int)rnd(30) - 3) * RG_SUB;
			G.p.y = (24 * 8 - RG_PH - (int)rnd(20)) * RG_SUB;
			G.p.ground = (int)rnd(2);
			G.p.vx = (int)rnd(3) * 8 - 8;
			G.p.face = rnd(2) ? 1 : -1;
			G.p.anim = (int)rnd(8);
		}
		for (i = 0; i < G.ne; i++)
			if (rnd(2)) {
				G.en[i].x0 = G.en[i].x1 = G.en[i].x = (anchor + (int)rnd(44) - 8) * RG_SUB;
				G.en[i].y = (24 * 8 - RG_EH - (int)rnd(14)) * RG_SUB;
				G.en[i].dir = rnd(2) ? 1 : -1;
				G.en[i].alive = rnd(6) != 0;
			}
		G.p.grace = rnd(4) == 0 ? 1 : 0;
		if (rnd(10) == 0)
			G.ticks = (unsigned long)rnd(32);
		if (rnd(10) == 0)
			G.cam = 2 + (int)rnd(4);
		frame(d, &V, 0);
		bad = ref_diff(&shared);
		CHECK(bad == 0, "random overlap frame %d: %d pixels differ from the reference", iter, bad);
	}
	CHECK(shared > 100, "the random frames put crawler boxes against the climber's: %d", shared);
}

/*
 * What a one-column scroll costs on every shipped level, with the sprites
 * out of view: the tiles and windows the far ridge, rock edges and ledges
 * change. The bound is the measured maximum (166 tiles on the last level)
 * rounded up; at 123 us a tile it is 24.6 ms of bus time for the worst step.
 */
static void
test_scroll_budget(void)
{
	struct dev *d = &DA;
	int lv, cam, worst, tiles = 0, steps = 0;

	ridge_off = 0;
	for (lv = 0; lv < RG_NLEVELS; lv++) {
		rg_new_game(&G, lv);
		G.ne = 0;
		G.p.x = 0;
		start_view(d, &V);
		G.cam = 2;
		frame(d, &V, 0);
		worst = 0;
		for (cam = 3; cam + RG_VIEW_W <= G.w; cam++) {
			G.cam = cam;
			frame(d, &V, 0);
			if (d->ntiles > worst)
				worst = d->ntiles;
			tiles += d->ntiles;
			steps++;
			CHECK(d->nset == 0, "a scroll step uploads no tiles");
		}
		CHECK(worst <= 200 && worst > 0, "level %d: worst one-column scroll sends %d tiles", lv + 1, worst);
	}
	CHECK(steps > 0 && tiles / steps < 140, "mean scroll step %d tiles", steps > 0 ? tiles / steps : -1);
}

/* Compare the panel of DA, built incrementally, with a fresh paint of the same game state. */
static void
compare_fresh(const char *what, int paused)
{
	int k, bad = 0;

	start_view(&DB, &V2);
	frame(&DB, &V2, paused);
	for (k = 0; k < GTILE_CELLS; k++)
		if (!DA.drawn[k] || !DB.drawn[k] || memcmp(DA.panel[k], DB.panel[k], LCDG_TILE_BYTES) != 0)
			bad++;
	CHECK(bad == 0, "%s: %d cells differ from a fresh paint", what, bad);
}

static void
test_play_matches_fresh(void)
{
	int lv, f, k, st;
	unsigned in;

	ridge_off = 0;

	for (lv = 0; lv < RG_NLEVELS; lv++) {
		rg_new_game(&G, lv);
		start_view(&DA, &V);
		cur = &DA;
		in = 0;
		for (f = 0; f < 700; f++) {
			if (rnd(12) == 0)
				in = (rnd(4) != 0 ? RG_IN_RIGHT : RG_IN_LEFT) | (rnd(2) ? RG_IN_JUMP : 0);
			rg_tick(&G, in | ((in & RG_IN_JUMP) && rnd(5) == 0 ? RG_IN_JPRESS : 0));
			if (G.state == RG_OVER || G.state == RG_WON)
				rg_new_game(&G, lv);
			frame(&DA, &V, 0);
			if (f % 9 == 0)
				compare_fresh("random play", 0);
		}
		compare_fresh("end of the random run", 0);
	}
	/* Every state, paused or not, with the far ridge on. */
	for (st = RG_READY; st <= RG_WON; st++) {
		rg_new_game(&G, 1);
		start_view(&DA, &V);
		rg_ridge_init();
		frame(&DA, &V, 0);
		G.state = st;
		for (k = 0; k < 3; k++) {
			frame(&DA, &V, k == 1);
			compare_fresh("state banner", k == 1);
		}
	}
	/* A banner is HUD ground with text; "PAUSED" reads P A U S E D on row 14. */
	rg_new_game(&G, 0);
	start_view(&DA, &V);
	frame(&DA, &V, 1);
	CHECK(V.fin[14 * 40 + 17] == T_FONT + 10 + ('P' - 'A') && V.fin[14 * 40 + 22] == T_FONT + 10 + ('D' - 'A'),
	    "PAUSED banner");
}

int
main(void)
{
	test_static_tiles();
	test_first_and_idle();
	test_scroll_runs();
	test_sprite_steps();
	test_sprite_refresh();
	test_ridge_contour();
	test_shard_frames();
	test_two_sprites();
	test_edge_sprite_uncovered();
	test_grace_blink();
	test_shared_tiles_pixel();
	test_overlap_random();
	test_overlap_swap();
	test_scroll_budget();
	test_play_matches_fresh();
	printf("%d checks, %d failed\n", nchk, nbad);
	return nbad != 0;
}
