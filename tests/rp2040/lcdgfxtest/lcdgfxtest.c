/*
 * lcdgfxtest: on-device speed and restore check of the PicoCalc pixel device
 * /dev/lcdgfx (sys/arch/rp2040/dev/lcdgfx.c, sys/lcdgfx.h).
 *
 * Run from the console of a PICOCALC kernel. The program opens the device,
 * draws the shapes whose bus time docs/research/picocalc-calculator-and-gui.md
 * predicts for RGB666 at 12.5 MHz (1.92 us per pixel, 7 us per window), times
 * each with gettimeofday(2) and prints the measured and predicted
 * microseconds with their ratio. The ratios are the measurement: the
 * research note's model is falsified by a full fill more than 15 percent
 * above its prediction, which points at the PL022's inter-frame gaps. The
 * program fails only when a system call fails.
 *
 * With the argument "crash" it fills the screen red and kills itself with
 * SIGKILL while the device is open; the console must then show its text
 * again, which is the restore on process exit.
 *
 * Regression tool, not shipped in the root manifest (distrib/rp2040/mi.rp2040
 * does not list it); stage it with a "file /usr/bin/lcdgfxtest" line for a
 * test image or copy it to the board over the console. A board run is
 * opt-in: it takes over the panel and needs the person at the board.
 * Prints "LCDGFXTEST OK" when every call succeeds.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/lcdgfx.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVICE		"/dev/lcdgfx"
#define TILE_ROW	40		/* Tiles of 8x8 across the panel. */

static int fd = -1;
static int errors;
static int bytes_pp = 3;		/* Bytes per pixel under the COLMOD in force. */
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
report(const char *name, long us, long predicted_us)
{
	long percent = predicted_us > 0 ? us * 100 / predicted_us : 0;

	printf("%-34s %9ld us  predicted %9ld us  %4ld%%\n", name, us,
	    predicted_us, percent);
}

/*
 * Bus time in microseconds: a byte takes 0.64 us (8 bits at 12.5 MHz), a
 * pixel bytes_pp bytes and a window setup eleven (CASET, RASET and RAMWR with
 * their parameters). The estimate leaves out the PL022's inter-frame gaps
 * and the CPU time between bytes.
 */
static long
bus(long pixels, long windows, long extra_bytes)
{
	return ((pixels * bytes_pp + windows * 11 + extra_bytes) * 64 + 50) / 100;
}

static void
do_fill(int x0, int y0, int x1, int y1, unsigned color)
{
	struct lcdg_rect r;

	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	r.color = color;
	if (ioctl(fd, LCDG_FILL, &r) < 0)
		fail("LCDG_FILL");
}

static void
do_window(int x, int y, int w, int h, int fmt)
{
	struct lcdg_window v;

	v.x = x;
	v.y = y;
	v.w = w;
	v.h = h;
	v.fmt = fmt;
	v.fg = 15;
	v.bg = 0;
	if (ioctl(fd, LCDG_WINDOW, &v) < 0)
		fail("LCDG_WINDOW");
}

static void
do_colmod(int fmt)
{
	struct lcdg_colmod c;

	c.fmt = fmt;
	if (ioctl(fd, LCDG_COLMOD, &c) < 0)
		fail("LCDG_COLMOD");
}

static void
crash(void)
{
	printf("filling the panel red and killing this process; the console\n"
	    "text must be visible again afterwards\n");
	fflush(stdout);
	do_fill(0, 0, LCDG_WIDTH - 1, LCDG_HEIGHT - 1, 0xf800);
	kill(getpid(), SIGKILL);
}

int
main(int argc, char **argv)
{
	static unsigned short tile[64];
	static unsigned char sprite[32];
	struct lcdg_info info;
	struct lcdg_circle c;
	struct lcdg_line l;
	struct lcdg_rect rr;
	struct lcdg_scroll s;
	struct lcdg_palette p;
	int i, second;

	fd = open(DEVICE, O_WRONLY);
	if (fd < 0) {
		printf("cannot open %s\n", DEVICE);
		return 1;
	}
	if (open(DEVICE, O_WRONLY) >= 0)
		fail("a second open succeeded");
	if (argc > 1 && strcmp(argv[1], "crash") == 0)
		crash();
	if (ioctl(fd, LCDG_INFO, &info) < 0)
		fail("LCDG_INFO");
	else
		printf("panel %dx%d, %d bits per pixel, %d tiles\n", info.w,
		    info.h, info.bpp, info.tiles);

	for (i = 0; i < 64; i++)
		tile[i] = (unsigned short)(0x0821 * (i % 31 + 1));
	for (i = 0; i < 32; i++)
		sprite[i] = (unsigned char)(i & 2 ? 0xaa : 0x55);
	p.index = 1;
	p.r = 255;
	p.g = 200;
	p.b = 0;
	if (ioctl(fd, LCDG_PALETTE, &p) < 0)
		fail("LCDG_PALETTE");

	for (second = 0; second < 2; second++) {
		printf("-- %s --\n", second ? "COLMOD 0x55 (RGB565)" :
		    "COLMOD 0x66 (RGB666, the console format)");
		if (second) {
			do_colmod(LCDG_COLMOD_RGB565);
			bytes_pp = 2;
		}
		start();
		do_fill(0, 0, 319, 319, 0x0000);
		report("full 320x320 fill", elapsed(), bus(320L * 320, 1, 0));

		start();
		for (i = 0; i < 100; i++)
			do_fill(0, i, 319, i, 0x07e0);
		report("100 pixel lines of 320", elapsed(), bus(100L * 320, 100, 0));

		start();
		for (i = 0; i < TILE_ROW; i++) {
			do_window(i * 8, 0, 8, 8, LCDG_WIN_RGB565);
			if (write(fd, tile, sizeof(tile)) != (ssize_t)sizeof(tile))
				fail("tile write");
		}
		report("40 tiles of 8x8, RGB565 stream", elapsed(),
		    bus(40L * 64, 40, 0));

		start();
		for (i = 0; i < 8; i++) {
			do_window(i * 16, 40, 16, 16, LCDG_WIN_BITS);
			if (write(fd, sprite, sizeof(sprite)) !=
			    (ssize_t)sizeof(sprite))
				fail("bit window write");
			do_fill(i * 16, 60, i * 16 + 15, 75, 0x001f);
		}
		report("8 sprites of 16x16 (1 bpp + fill)", elapsed(),
		    bus(8L * 512, 16, 0));

		start();
		l.x0 = 0;
		l.y0 = 100;
		l.x1 = 319;
		l.y1 = 319;
		l.color = 0xffff;
		if (ioctl(fd, LCDG_LINE, &l) < 0)
			fail("LCDG_LINE");
		report("diagonal line, one window per pixel", elapsed(),
		    bus(320, 320, 0));

		start();
		rr.x0 = 10;
		rr.y0 = 110;
		rr.x1 = 300;
		rr.y1 = 300;
		rr.color = 0xf81f;
		if (ioctl(fd, LCDG_FRAME, &rr) < 0)
			fail("LCDG_FRAME");
		report("frame 291x191, four windows", elapsed(),
		    bus(2 * 291 + 2 * 189, 4, 0));

		start();
		c.x = 160;
		c.y = 200;
		c.r = 100;
		c.color = 0x07ff;
		if (ioctl(fd, LCDG_CIRCLE, &c) < 0)
			fail("LCDG_CIRCLE");
		report("circle r=100, eight plots per step", elapsed(),
		    bus(568, 568, 0));

		start();
		for (i = 0; i < 60; i++) {
			s.line = i;
			if (ioctl(fd, LCDG_SCROLL, &s) < 0)
				fail("LCDG_SCROLL");
		}
		report("60 scroll starts", elapsed(), bus(0, 0, 60 * 3));
		s.line = 0;
		ioctl(fd, LCDG_SCROLL, &s);
	}
	do_colmod(LCDG_COLMOD_RGB666);
	sleep(2);
	close(fd);
	printf(errors ? "LCDGFXTEST FAILED\n" : "LCDGFXTEST OK\n");
	return errors != 0;
}
