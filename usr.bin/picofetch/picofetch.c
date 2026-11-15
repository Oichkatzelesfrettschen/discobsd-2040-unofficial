/*
 * picofetch -- one screen of system and hardware readings beside a logo,
 * sized for the PicoCalc's 53 by 32 console and usable on any terminal.
 *
 * usage: picofetch [-lnp]
 *	-l	no logo
 *	-n	no color
 *	-p	plain: ASCII only, no escape sequence (the default when the
 *		output is not a terminal)
 *
 * Every value is a reading: a sysctl(3) record, getfsstat(2), the tty's
 * window size or the environment (hwinfo.h). A node the running kernel
 * does not serve prints "not in this kernel"; a failed read prints "read
 * failed". The program writes nothing to the kernel.
 *
 * The eight readings beside the logo are the short ones; the rows below it
 * use the whole width. On the PicoCalc the report is at most 30 rows, so
 * the command line above it and the prompt below it stay on the screen.
 *
 * The logo is a mirror ball computed from a circle and a facet grid, drawn
 * as a two-by-three mosaic (pcdraw.h): 3 by 3 pixel squares from the
 * console's glyph bank, ASCII elsewhere. Bars use the same bank at a sixth
 * of a cell.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hwinfo.h"
#include "pcdraw.h"

#define	LOGO_COLS	14		/* cells */
#define	LOGO_ROWS	10
#define	LOGO_W		(LOGO_COLS * 2)	/* mosaic squares */
#define	LOGO_H		(LOGO_ROWS * 3)
#define	LOGO_GAP	2
#define	LABEL_W		8
#define	BAR_CELLS	8
#define	MAXLINES	40

struct line {
	const char *label;
	char	text[192];
	int	bar;			/* draw a bar before the text */
	long	part, total;
	int	color;			/* the bar's color */
};

static struct line lines[MAXLINES];
static int nlines;
static struct pcterm term;
static struct hwinfo hw;

static struct line *
add(const char *label)
{
	struct line *l = &lines[nlines < MAXLINES - 1 ? nlines++ : nlines];

	memset(l, 0, sizeof *l);
	l->label = label;
	return l;
}

static const char *
why(const struct rd_int *r)
{
	return r->state == RD_ABSENT ? "not in this kernel" : "read failed";
}

/* A level bar: green below 60 percent, yellow below 85, red above. */
static void
bar(struct line *l, long part, long total, int inverse)
{
	long pct = total > 0 ? part / (total / 100 > 0 ? total / 100 : 1) : 0;

	l->bar = 1;
	l->part = part;
	l->total = total;
	if (inverse)			/* A level that is good when high. */
		l->color = pct > 40 ? 2 : pct > 15 ? 3 : 1;
	else
		l->color = pct < 60 ? 2 : pct < 85 ? 3 : 1;
}

/* One square of the logo: 0 empty, 1 the ball, 2 the cord, 3 a glint. */
static int
logo_at(int x, int y)
{
	static const signed char glint[4][2] = {
		{ 0, 7 }, { 27, 5 }, { 0, 26 }, { 27, 28 }
	};
	int dx = 2 * x - 28, dy = 2 * y - 33, i;

	/*
	 * Facets are three squares wide and two tall, so each spans two
	 * cells of one row and takes one color.
	 */
	if (dx * dx + dy * dy <= 25 * 25)
		return x % 4 == 2 || y % 3 == 0 ? 0 : 1;
	if (y < 4 && x == 14)
		return 2;
	for (i = 0; i < 4; i++)
		if (glint[i][0] == x && glint[i][1] == y)
			return 3;
	return 0;
}

static void
logo_row(int row)
{
	static const int facet[6] = { 13, 12, 14, 10, 11, 9 };
	int cx, sx, sy, mask, kind, k;

	for (cx = 0; cx < LOGO_COLS; cx++) {
		mask = kind = 0;
		for (sy = 0; sy < 3; sy++)
			for (sx = 0; sx < 2; sx++) {
				k = logo_at(cx * 2 + sx, row * 3 + sy);
				if (k == 0)
					continue;
				mask |= 1 << (sy * 2 + sx);
				kind = k;
			}
		if (mask != 0)
			pc_fg(&term, kind == 1 ? facet[((cx + 1) / 2 + row) % 6] :
			    kind == 2 ? 7 : 15);
		pc_mosaic(&term, mask);
	}
	pc_reset(&term);
}

static void
table(char *buf, size_t len, const char *name, const struct hwi_table *t)
{
	size_t n = strlen(buf);

	if (n < len)
		snprintf(buf + n, len - n, "%s%s %d/%d", n ? " " : "", name,
		    t->cur, t->limit);
}

static void
build(void)
{
	struct line *l;
	char a[16], b[16];
	const char *s;
	int i, sd = 0;

	l = add("OS");
	snprintf(l->text, sizeof l->text, "%s %s %s", hw.ostype, hw.osrelease,
	    hw.arch);
	l = add("Kernel");
	snprintf(l->text, sizeof l->text, "%s", hw.osversion);
	l = add("Board");
	snprintf(l->text, sizeof l->text, "%s",
	    hw.model[0] != '\0' ? hw.model : hw.machine);

	l = add("Uptime");
	if (hw.boottime.state == RD_OK)
		pc_uptime(l->text, sizeof l->text, hw.now - hw.boottime.v);
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.boottime));

	l = add("CPU");
	if (hw.cpu_khz.state == RD_OK)
		snprintf(l->text, sizeof l->text, "%ld.%03ld MHz, %ld Hz tick",
		    hw.cpu_khz.v / 1000, hw.cpu_khz.v % 1000, hw.hz.v);
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.cpu_khz));
	l = add("Bus");
	if (hw.bus_khz.state == RD_OK)
		snprintf(l->text, sizeof l->text, "%ld.%03ld MHz peripheral",
		    hw.bus_khz.v / 1000, hw.bus_khz.v % 1000);
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.bus_khz));

	l = add("SRAM");
	pc_size(a, sizeof a, hw.physmem.v / 1024);
	pc_size(b, sizeof b, hw.window.v / 1024);
	snprintf(l->text, sizeof l->text, "%s, process window %s", a, b);

	l = add("Display");
	if (term.xpixel > 0)
		snprintf(l->text, sizeof l->text, "%dx%d px, %dx%d cells",
		    term.xpixel, term.ypixel, term.cols, term.rows);
	else
		snprintf(l->text, sizeof l->text, "%dx%d cells, %s", term.cols,
		    term.rows, term.tier == PC_PLAIN ? "plain" : "ANSI");

	l = add("Cores");
	if (hw.core1.state == RD_OK) {
		static const char *const st[4] = {
			"parked", "observer", "restarting", "dead"
		};

		snprintf(l->text, sizeof l->text,
		    "%ld kernel; core1 %s r%ld s%ld f%ld", hw.ncpu.v,
		    st[hw.core1.v & 3], hw.core1_restarts.v,
		    hw.core1_stalls.v, hw.core1_faults.v);
	} else
		snprintf(l->text, sizeof l->text, "%ld kernel; core1 %s",
		    hw.ncpu.v, hw.core1.state == RD_ABSENT ?
		    "not in this kernel" : "read failed");

	l = add("Memory");
	if (hw.mem_real.state == RD_OK) {
		pc_size(a, sizeof a, (hw.mem_real.v + 1023) / 1024);
		pc_size(b, sizeof b, (hw.mem_virt.v + 1023) / 1024);
		snprintf(l->text, sizeof l->text, "%s core, %s all", a, b);
		bar(l, hw.mem_real.v, hw.window.v, 0);
	} else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.mem_real));

	l = add("Swap");
	if (hw.nswap.state == RD_OK && hw.swapfree.state == RD_OK) {
		pc_size(a, sizeof a, hw.nswap.v - hw.swapfree.v);
		pc_size(b, sizeof b, hw.nswap.v);
		if (hw.swapram_images.state == RD_OK)
			snprintf(l->text, sizeof l->text, "%s/%s ram %ld", a, b,
			    hw.swapram_images.v);
		else
			snprintf(l->text, sizeof l->text, "%s/%s", a, b);
		bar(l, hw.nswap.v - hw.swapfree.v, hw.nswap.v, 0);
	} else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.swapfree));

	for (i = 0; i < hw.nfs; i++) {
		const struct hwi_fs *f = &hw.fs[i];

		s = strrchr(f->from, '/');
		l = add(strcmp(f->on, "/") == 0 ? "Root" : f->on);
		pc_size(a, sizeof a, f->kb - f->kbfree);
		pc_size(b, sizeof b, f->kb);
		snprintf(l->text, sizeof l->text, "%s/%s %s", a, b,
		    s != NULL ? s + 1 : f->from);
		bar(l, f->kb - f->kbfree, f->kb, 0);
		if (strncmp(s != NULL ? s + 1 : f->from, "sd", 2) == 0)
			sd = 1;
	}
	l = add("Disks");
	for (i = 0; i < hw.ndisk; i++) {
		size_t n = strlen(l->text);

		snprintf(l->text + n, sizeof l->text - n, "%s%s", n ? " " : "",
		    hw.disk[i].name);
		if (strncmp(hw.disk[i].name, "sd", 2) == 0 && !sd)
			sd = 2;
	}
	if (hw.ndisk == 0)
		snprintf(l->text, sizeof l->text, "none reported");
	else if (sd == 2) {
		size_t n = strlen(l->text);

		snprintf(l->text + n, sizeof l->text - n, " (sd unmounted)");
	}

	l = add("Flash");
	if (hw.image_length.state == RD_OK)
		snprintf(l->text, sizeof l->text, "image %ld B crc %08lx %s",
		    hw.image_length.v, (unsigned long)hw.image_crc.v &
		    0xffffffffUL, hw.image_crc.v == hw.image_crc_stored.v ?
		    "ok" : "MISMATCH");
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.image_length));

	l = add("XIP");
	if (hw.xip_cache.state == RD_OK) {
		unsigned long acc = (unsigned long)hw.xip_acc.v;
		unsigned long hit = (unsigned long)hw.xip_hit.v;
		unsigned long per = acc / 1000 > 0 ? hit / (acc / 1000) : 0;

		snprintf(l->text, sizeof l->text, "cache %s, hit %lu.%lu%%",
		    hw.xip_cache.v ? "on" : "off", per / 10, per % 10);
	} else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.xip_cache));

	l = add("MPU");
	if (hw.mpu_enable.state == RD_OK)
		snprintf(l->text, sizeof l->text, "%s, %ld of %ld regions",
		    hw.mpu_enable.v ? "on" : "off", hw.mpu_programmed.v,
		    hw.mpu_nregions.v);
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.mpu_enable));

	l = add("Battery");
	if (hw.battery.state == RD_OK) {
		snprintf(l->text, sizeof l->text, "%ld%%%s", hw.battery.v,
		    hw.charging.state == RD_OK && hw.charging.v ?
		    " charging" : "");
		bar(l, hw.battery.v, 100, 1);
	} else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.battery));
	l = add("Light");
	if (hw.lcd_backlight.state == RD_OK)
		snprintf(l->text, sizeof l->text, "panel %ld, keys %ld of 240",
		    hw.lcd_backlight.v, hw.kbd_backlight.v);
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.lcd_backlight));

	l = add("Sensors");
	if (hw.temp_mc.state == RD_OK)
		snprintf(l->text, sizeof l->text, "%ld.%ld C, VSYS %ld.%02ld V",
		    hw.temp_mc.v / 1000, labs(hw.temp_mc.v) % 1000 / 100,
		    hw.vsys_mv.v / 1000, hw.vsys_mv.v % 1000 / 10);
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.temp_mc));

	l = add("Load");
	if (hw.load[0].state == RD_OK)
		snprintf(l->text, sizeof l->text,
		    "%ld.%02ld %ld.%02ld %ld.%02ld run %ld", hw.load[0].v / 100,
		    hw.load[0].v % 100, hw.load[1].v / 100, hw.load[1].v % 100,
		    hw.load[2].v / 100, hw.load[2].v % 100, hw.runq.v);
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.load[0]));

	l = add("Tables");
	if (hw.t_proc.state == RD_OK) {
		table(l->text, sizeof l->text, "proc", &hw.t_proc);
		table(l->text, sizeof l->text, "ino", &hw.t_inode);
		table(l->text, sizeof l->text, "file", &hw.t_file);
		table(l->text, sizeof l->text, "buf", &hw.t_buffer);
	} else
		snprintf(l->text, sizeof l->text, "%s", hw.t_proc.state ==
		    RD_ABSENT ? "not in this kernel" : "read failed");
	l = add("Kstack");
	if (hw.kstack_peak.state == RD_OK) {
		snprintf(l->text, sizeof l->text, "peak %ld of %ld B",
		    hw.kstack_peak.v, hw.kstack_bytes.v);
		bar(l, hw.kstack_peak.v, hw.kstack_bytes.v, 0);
	} else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.kstack_peak));

	l = add("I/O");
	if (hw.st_root_wr.state == RD_OK) {
		pc_size(a, sizeof a, hw.st_root_wr.v);
		pc_size(b, sizeof b, hw.st_swap_wr.v);
		snprintf(l->text, sizeof l->text, "root w %s swap w %s er %ld",
		    a, b, hw.st_erases.v);
	} else
		snprintf(l->text, sizeof l->text, "counters %s",
		    why(&hw.st_root_wr));

	l = add("USB");
	if (hw.usb_bulkin_arms.state == RD_OK)
		snprintf(l->text, sizeof l->text, "cdc in %ld, recovered %ld",
		    hw.usb_bulkin_arms.v, hw.usb_tx_recovered.v);
	else
		snprintf(l->text, sizeof l->text, "console %s",
		    why(&hw.usb_bulkin_arms));

	l = add("Console");
	s = getenv("TERM");
	if (hw.cons_major.state == RD_OK)
		snprintf(l->text, sizeof l->text, "dev %ld,%ld %s",
		    hw.cons_major.v, hw.cons_minor.v, s != NULL ? s : "");
	else
		snprintf(l->text, sizeof l->text, "%s", why(&hw.cons_major));

	l = add("Reset");
	if (hw.watchdog_reason.state != RD_OK)
		snprintf(l->text, sizeof l->text, "%s",
		    why(&hw.watchdog_reason));
	else if (hw.watchdog_reason.v == 0)
		snprintf(l->text, sizeof l->text, "power or run pin");
	else
		snprintf(l->text, sizeof l->text, "watchdog%s%s",
		    hw.watchdog_reason.v & 1 ? " timeout" : "",
		    hw.watchdog_reason.v & 2 ? " forced" : "");
}

static void
onsig(int sig)
{
	pc_done();
	_exit(128 + sig);
}

int
main(int argc, char **argv)
{
	int c, color = -1, plain = 0, logo = 1, row, rows, indent, width, i;
	const char *user;
	char title[HWI_STR + 24];

	while ((c = getopt(argc, argv, "lnp")) != -1)
		switch (c) {
		case 'l':
			logo = 0;
			break;
		case 'n':
			color = 0;
			break;
		case 'p':
			plain = 1;
			break;
		default:
			fprintf(stderr, "usage: picofetch [-lnp]\n");
			return 2;
		}

	signal(SIGINT, onsig);
	signal(SIGTERM, onsig);
	signal(SIGHUP, onsig);
	pc_init(&term, 1, color, plain);
	hw_collect(&hw, HWI_STATIC | HWI_LIVE);
	build();

	if (term.tier == PC_PLAIN || term.cols < LOGO_COLS + LOGO_GAP + 30)
		logo = 0;

	user = getenv("USER");
	if (user == NULL || user[0] == '\0')
		user = getuid() == 0 ? "root" : "user";
	snprintf(title, sizeof title, "%s@%s", user,
	    hw.hostname[0] != '\0' ? hw.hostname : "discobsd");

	rows = nlines + 3;		/* title, rule, palette */
	if (logo && rows < LOGO_ROWS)
		rows = LOGO_ROWS;
	for (row = 0; row < rows; row++) {
		/* Rows below the logo start at the left margin. */
		indent = logo && row < LOGO_ROWS ? LOGO_COLS + LOGO_GAP : 0;
		width = term.cols - indent - LABEL_W - 1;
		if (indent != 0) {
			logo_row(row);
			printf("%*s", LOGO_GAP, "");
		}
		if (row == 0) {
			pc_bold(&term);
			pc_fg(&term, 13);
			printf("%.*s", term.cols - indent, title);
			pc_reset(&term);
		} else if (row == 1) {
			pc_fg(&term, 8);
			pc_rule(&term, (int)strlen(title) < term.cols - indent ?
			    (int)strlen(title) : term.cols - indent);
			pc_reset(&term);
		} else if (row - 2 < nlines) {
			const struct line *l = &lines[row - 2];
			int w = width;

			pc_fg(&term, 14);
			printf("%-*.*s ", LABEL_W, LABEL_W, l->label);
			pc_reset(&term);
			if (l->bar && w > BAR_CELLS + 8) {
				pc_bar(&term, BAR_CELLS, 1, &l->part, &l->color,
				    l->total);
				putchar(' ');
				w -= BAR_CELLS + 1;
			}
			printf("%.*s", w, l->text);
		} else if (row - 2 == nlines && term.color) {
			/* The sixteen colors, two cells each when they fit. */
			for (i = 0; i < 16; i++) {
				pc_fg(&term, i);
				if (term.tier == PC_BANK) {
					putchar(PC_HBAR + 5);
					if (term.cols - indent >= 32)
						putchar(PC_HBAR + 5);
				} else
					fputs(term.cols - indent >= 32 ?
					    "##" : "#", stdout);
			}
			pc_reset(&term);
		}
		pc_nl(&term);
	}
	fflush(stdout);
	pc_done();
	return 0;
}
