/*
 * tiledemo: on-device check of the tile atlas of the PicoCalc pixel device
 * (sys/arch/rp2040/dev/lcdgfx.c, sys/lcdgfx.h) through the tile library
 * (games/gametile.h), and of the console's raw key events (sys/lcdcons.h,
 * LCDIOC_GETKEY).
 *
 * Run from the console of a PICOCALC kernel. The program uploads twelve
 * generated tiles, then times three tile workloads against the bus time that
 * docs/research/picocalc-calculator-and-gui.md predicts for RGB666 at
 * 12.5 MHz (122.9 us per 8x8 tile, 7.0 us per window):
 *
 *	repaint	the 40 by 40 grid in 40 windows (196.9 ms predicted)
 *	pan	a 40 by 20 view of a hashed world one column at a time, so
 *		about eleven of twelve cells change per step
 *	ring	the grid scrolled one tile row per step by writing the row
 *		that left the screen and moving the scroll start by eight
 *		lines (4.9 ms of bus time per step predicted)
 *
 * Every number is printed with its prediction and ratio; the program fails
 * only when a system call fails. While it runs it drains the raw key events
 * each frame and counts them by state; Esc ends it early. It sets the
 * console tty to raw mode without echo and restores it before it exits.
 *
 * Regression tool, not shipped in the root manifest; stage it with a "file
 * /usr/bin/tiledemo" line in distrib/rp2040/mi.rp2040 for a test image or
 * copy it to the board over the console. A board run is opt-in: it takes
 * over the panel. Prints "TILEDEMO OK" when every call succeeds.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/lcdcons.h>
#include <sys/lcdgfx.h>
#include <fcntl.h>
#include <sgtty.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gametile.h"

#define DEVICE		"/dev/lcdgfx"
#define NKINDS		12		/* Generated tiles. */
#define WORLD_W		128
#define VIEW_W		40
#define VIEW_H		20
#define PAN_STEPS	(WORLD_W - VIEW_W)
#define RING_STEPS	120
#define US_PER_TILE	123		/* 64 pixels x 3 bytes x 0.64 us, rounded. */
#define US_PER_WINDOW	7

static int errors;
static int escape;			/* Esc was pressed. */
static long key_events[4];		/* Events seen per state. */
static long key_lost;
static struct timeval t0;

static void
fail(const char *what)
{
	printf("FAIL %s\n", what);
	errors++;
}

static void
start(void)
{
	gettimeofday(&t0, NULL);
}

static long
elapsed(void)
{
	struct timeval t1;

	gettimeofday(&t1, NULL);
	return (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
}

static void
report(const char *name, long frames, long us, long tiles, long windows)
{
	long predicted = tiles * US_PER_TILE + windows * US_PER_WINDOW;
	long per = frames > 0 ? us / frames : 0;
	long pred_per = frames > 0 ? predicted / frames : 0;

	printf("%-8s %4ld frames %8ld us total %7ld us/frame  predicted %7ld "
	    "us/frame  %4ld%%\n", name, frames, us, per, pred_per,
	    predicted > 0 ? us * 100 / predicted : 0L);
}

/* Drain the console's raw key events and count them by state. */
static void
poll_keys(void)
{
	struct lcd_keyevs k;
	int i;

	do {
		memset(&k, 0, sizeof(k));
		if (ioctl(0, LCDIOC_GETKEY, &k) < 0) {
			fail("LCDIOC_GETKEY");
			return;
		}
		key_lost += k.lost;
		for (i = 0; i < k.n; i++) {
			if (k.ev[i].state >= 1 && k.ev[i].state <= 3)
				key_events[k.ev[i].state]++;
			if (k.ev[i].state == LCD_KEY_PRESSED &&
			    k.ev[i].key == LCD_KEY_ESC)
				escape = 1;
		}
	} while (k.n == LCD_KEYEV_N);
}

static unsigned
hash(unsigned x, unsigned y)
{
	unsigned h = x * 374761393U + y * 668265263U;

	h = (h ^ (h >> 13)) * 1274126177U;
	return h ^ (h >> 16);
}

static int
world_tile(int wx, int wy)
{
	return (int)(hash((unsigned)wx, (unsigned)wy) % NKINDS);
}

/* Palette index of pixel (x, y) of generated tile n. */
static int
pixel_of(int n, int x, int y)
{
	int a = 1 + n % 7, b = 8 + n % 7;

	switch (n % 6) {
	case 0:
		return a;
	case 1:
		return (x + y) & 1 ? a : b;
	case 2:
		return y & 2 ? a : b;
	case 3:
		return x & 2 ? a : b;
	case 4:
		return ((x + y) & 3) == 0 ? a : b;
	default:
		return x == 0 || y == 0 || x == 7 || y == 7 ? a : b;
	}
}

static void
make_tiles(unsigned char *data)
{
	int n, x, y;

	for (n = 0; n < NKINDS; n++)
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x += 2)
				data[n * LCDG_TILE_BYTES + y * 4 + x / 2] =
				    (unsigned char)(pixel_of(n, x, y) << 4 |
				    pixel_of(n, x + 1, y));
}

static void
scroll_to(int fd, int line)
{
	struct lcdg_scroll s;

	s.line = (u_short)line;
	if (ioctl(fd, LCDG_SCROLL, &s) < 0)
		fail("LCDG_SCROLL");
}

static void
flush(struct gtile *g, long *tiles, long *windows)
{
	long pending = g->pending;
	int w = gtile_flush(g);

	if (w < 0) {
		fail("gtile_flush");
		return;
	}
	*tiles += pending;
	*windows += w;
}

int
main(void)
{
	static unsigned char data[NKINDS * LCDG_TILE_BYTES];
	static struct gtile g;
	struct lcdg_info info;
	struct sgttyb saved, raw;
	long us, tiles, windows;
	int fd, x, y, step, top, have_tty;

	have_tty = ioctl(0, TIOCGETP, &saved) == 0;
	if (have_tty) {
		raw = saved;
		raw.sg_flags |= RAW;
		raw.sg_flags &= ~ECHO;
		ioctl(0, TIOCSETP, &raw);
	}
	fd = open(DEVICE, O_WRONLY);
	if (fd < 0) {
		if (have_tty)
			ioctl(0, TIOCSETP, &saved);
		printf("cannot open %s\n", DEVICE);
		return 1;
	}
	poll_keys();			/* Forget keys typed before the start. */
	key_events[1] = key_events[2] = key_events[3] = 0;
	key_lost = 0;
	if (ioctl(fd, LCDG_INFO, &info) < 0)
		fail("LCDG_INFO");

	make_tiles(data);
	gtile_init(&g, fd);
	if (gtile_load(&g, 0, NKINDS, data) < 0)
		fail("gtile_load");

	/* The full grid: 40 windows of 40 tiles. */
	tiles = windows = 0;
	for (y = 0; y < GTILE_ROWS; y++)
		for (x = 0; x < GTILE_COLS; x++)
			gtile_set(&g, x, y, world_tile(x, y));
	start();
	flush(&g, &tiles, &windows);
	us = elapsed();
	report("repaint", 1, us, tiles, windows);
	poll_keys();

	/* A sideways pan: the view slides one world column per step. */
	tiles = windows = 0;
	start();
	for (step = 1; step <= PAN_STEPS && !escape; step++) {
		for (y = 0; y < VIEW_H; y++)
			for (x = 0; x < VIEW_W; x++)
				gtile_set(&g, x, y, world_tile(step + x, y));
		flush(&g, &tiles, &windows);
		poll_keys();
	}
	us = elapsed();
	report("pan", step - 1, us, tiles, windows);

	/*
	 * A vertical scroll through the ring of 40 tile rows. Row `top` of
	 * the grid is the screen's top line; the row that scrolls off is
	 * rewritten with the world row that scrolls in, then the scroll start
	 * moves down by one tile row.
	 */
	for (y = 0; y < GTILE_ROWS; y++)
		for (x = 0; x < GTILE_COLS; x++)
			gtile_set(&g, x, y, world_tile(x, y));
	tiles = windows = 0;
	flush(&g, &tiles, &windows);
	scroll_to(fd, 0);
	tiles = windows = 0;
	top = 0;
	start();
	for (step = 1; step <= RING_STEPS && !escape; step++) {
		for (x = 0; x < GTILE_COLS; x++)
			gtile_set(&g, x, top, world_tile(x, GTILE_ROWS + step - 1));
		flush(&g, &tiles, &windows);
		top = (top + 1) % GTILE_ROWS;
		scroll_to(fd, top * LCDG_TILE_H);
		poll_keys();
	}
	us = elapsed();
	report("ring", step - 1, us, tiles, windows);
	scroll_to(fd, 0);

	sleep(2);
	poll_keys();
	close(fd);
	if (have_tty)
		ioctl(0, TIOCSETP, &saved);
	printf("panel %dx%d, %d tiles; keys: %ld pressed, %ld hold, %ld released, "
	    "%ld lost%s\n", info.w, info.h, info.tiles, key_events[1],
	    key_events[2], key_events[3], key_lost, escape ? " (Esc)" : "");
	printf(errors ? "TILEDEMO FAILED\n" : "TILEDEMO OK\n");
	return errors != 0;
}
