/*
 * Tile map for the PicoCalc pixel device, /dev/lcdgfx (sys/lcdgfx.h).
 *
 * A program keeps the tile numbers of the 40 by 40 grid in a shadow map and
 * changes them with gtile_set(); a change marks the tile in a bitmap of
 * tiles the panel has not seen. gtile_flush() walks the bitmap and sends
 * one LCDG_TILEWIN, with the tile numbers of the run in the write(2) that
 * follows, for each run of marked tiles in a tile row, so a tile that did
 * not change costs no bus time and a run of n tiles costs one window setup
 * (11 bytes) and 64 n pixels. The panel is not read back, so the shadow map
 * is the only record of what it shows.
 *
 * Runs end at the edge of a tile row and at an unmarked tile; a run is never
 * widened across a tile that is already right, because a tile costs 64
 * pixels on the bus (123 us) against 11 bytes (7 us) for a window.
 * gtile_set() compares the new number with the shadow, not with the panel:
 * a tile set to a new number and back before the flush is still sent.
 *
 * The kernel keeps the tile pixels and the 16-entry palette. A program
 * uploads tiles with gtile_load(), which also marks the tiles that show them,
 * and changes palette entries with LCDG_PALETTE followed by gtile_touch_all(),
 * because the palette applies when a tile row is drawn. gtile_init() marks
 * every tile: the panel holds the text console's pixels when the device
 * opens.
 *
 * The header defines static functions and a structure of 1,808 bytes (the
 * shadow map of 1,600 bytes and the bitmap of 200) for one program's
 * translation unit, in the manner of gametty.h. It does not open
 * the device. A host test defines GTILE_IOCTL and GTILE_WRITE before the
 * include to bind them to a model of the device.
 */
#ifndef GAMES_GAMETILE_H
#define GAMES_GAMETILE_H

#include <string.h>
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef GTILE_IOCTL
#define GTILE_IOCTL(fd, cmd, arg)	ioctl((fd), (cmd), (arg))
#endif
#ifndef GTILE_WRITE
#define GTILE_WRITE(fd, buf, n)		write((fd), (buf), (n))
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#define GTILE_COLS	LCDG_GRID
#define GTILE_ROWS	LCDG_GRID
#define GTILE_CELLS	(GTILE_COLS * GTILE_ROWS)

struct gtile {
	int fd;			/* The open /dev/lcdgfx. */
	unsigned pending;	/* Marked tiles. */
	unsigned char shadow[GTILE_CELLS];	/* Tile number of every cell. */
	unsigned char dirty[GTILE_CELLS / 8];	/* Bit i: cell i is unsent. */
};

static int
gtile_is_dirty(const struct gtile *g, int i)
{
	return (g->dirty[i >> 3] >> (i & 7)) & 1;
}

static void
gtile_mark(struct gtile *g, int i)
{
	if (!gtile_is_dirty(g, i)) {
		g->dirty[i >> 3] = (unsigned char)(g->dirty[i >> 3] | 1U << (i & 7));
		g->pending++;
	}
}

static void
gtile_unmark(struct gtile *g, int i)
{
	if (gtile_is_dirty(g, i)) {
		g->dirty[i >> 3] = (unsigned char)(g->dirty[i >> 3] & ~(1U << (i & 7)));
		g->pending--;
	}
}

/* Start on the open device fd: every cell shows tile 0 once flushed. */
static void
gtile_init(struct gtile *g, int fd)
{
	g->fd = fd;
	memset(g->shadow, 0, sizeof(g->shadow));
	memset(g->dirty, 0xff, sizeof(g->dirty));
	g->pending = GTILE_CELLS;
}

/* Mark every cell, as after a palette change. */
static void
gtile_touch_all(struct gtile *g)
{
	memset(g->dirty, 0xff, sizeof(g->dirty));
	g->pending = GTILE_CELLS;
}

/* Tile number at (x, y), or -1 outside the grid. */
static int
gtile_get(const struct gtile *g, int x, int y)
{
	if (x < 0 || x >= GTILE_COLS || y < 0 || y >= GTILE_ROWS)
		return -1;
	return g->shadow[y * GTILE_COLS + x];
}

/*
 * Show tile t at (x, y). Returns -1, changing nothing, for a cell outside
 * the grid or a tile number the atlas does not hold. A number equal to the
 * shadow's marks nothing.
 */
static int
gtile_set(struct gtile *g, int x, int y, int t)
{
	int i;

	if (x < 0 || x >= GTILE_COLS || y < 0 || y >= GTILE_ROWS ||
	    t < 0 || t >= LCDG_ATLAS_N)
		return -1;
	i = y * GTILE_COLS + x;
	if (g->shadow[i] != t) {
		g->shadow[i] = (unsigned char)t;
		gtile_mark(g, i);
	}
	return 0;
}

/*
 * Show tile t over the w by h cells at (x, y); -1 if any cell or t is out of
 * range. The origin is bounded first and each extent is compared with the
 * room left after it, so no sum of caller values is formed.
 */
static int
gtile_fill(struct gtile *g, int x, int y, int w, int h, int t)
{
	int cx, cy;

	if (x < 0 || x > GTILE_COLS || y < 0 || y > GTILE_ROWS ||
	    w < 0 || w > GTILE_COLS - x || h < 0 || h > GTILE_ROWS - y ||
	    t < 0 || t >= LCDG_ATLAS_N)
		return -1;
	for (cy = y; cy < y + h; cy++)
		for (cx = x; cx < x + w; cx++)
			(void)gtile_set(g, cx, cy, t);
	return 0;
}

/* Atlas tiles first .. first + count - 1 exist; no sum of caller values is formed. */
static int
gtile_range_ok(int first, int count)
{
	return first >= 0 && first < LCDG_ATLAS_N && count >= 1 &&
	    count <= LCDG_ATLAS_N - first;
}

/* Mark the cells that show tiles first .. first + count - 1. */
static void
gtile_touch_tiles(struct gtile *g, int first, int count)
{
	int i;

	if (!gtile_range_ok(first, count))
		return;
	for (i = 0; i < GTILE_CELLS; i++)
		if (g->shadow[i] >= first && g->shadow[i] - first < count)
			gtile_mark(g, i);
}

/*
 * Store count tiles of LCDG_TILE_BYTES bytes each at atlas position first
 * and mark the cells that show them. Returns -1 for a range the atlas does
 * not hold or a failed call.
 */
static int
gtile_load(struct gtile *g, int first, int count, const unsigned char *data)
{
	struct lcdg_tileset ts;
	int n;

	if (!gtile_range_ok(first, count))
		return -1;
	n = count * LCDG_TILE_BYTES;
	ts.first = (u_char)first;
	ts.count = (u_char)count;
	if (GTILE_IOCTL(g->fd, LCDG_TILESET, &ts) < 0)
		return -1;
	if ((int)GTILE_WRITE(g->fd, data, (size_t)n) != n)
		return -1;
	gtile_touch_tiles(g, first, count);
	return 0;
}

/*
 * Send every marked tile, one LCDG_TILEWIN and one write(2) per run in a
 * tile row, in raster order. Returns the number of windows sent, or -1 at
 * the first failed call: the run that failed and every later one stay
 * marked, so the next flush resumes there.
 */
static int
gtile_flush(struct gtile *g)
{
	struct lcdg_tilewin w;
	int x, y, run, k, nwin = 0;

	if (g->pending == 0)
		return 0;
	for (y = 0; y < GTILE_ROWS; y++) {
		x = 0;
		while (x < GTILE_COLS) {
			if (!gtile_is_dirty(g, y * GTILE_COLS + x)) {
				x++;
				continue;
			}
			run = 1;
			while (x + run < GTILE_COLS &&
			    gtile_is_dirty(g, y * GTILE_COLS + x + run))
				run++;
			w.tx = (u_char)x;
			w.ty = (u_char)y;
			w.tw = (u_char)run;
			w.th = 1;
			if (GTILE_IOCTL(g->fd, LCDG_TILEWIN, &w) < 0)
				return -1;
			if ((int)GTILE_WRITE(g->fd, g->shadow + y * GTILE_COLS + x,
			    (size_t)run) != run)
				return -1;
			for (k = 0; k < run; k++)
				gtile_unmark(g, y * GTILE_COLS + x + k);
			nwin++;
			x += run;
		}
	}
	return nwin;
}

#pragma GCC diagnostic pop

#endif /* GAMES_GAMETILE_H */
