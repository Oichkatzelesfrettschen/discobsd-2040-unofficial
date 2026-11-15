/*
 * Terminal drawing shared by picofetch and picotop: colors, bars, graph
 * cells and a two-by-three mosaic, in three tiers chosen at run time.
 *
 * PC_BANK   the PicoCalc console (sys/lcdcons.h). pc_init() uploads the
 *           shapes below into the console's glyph bank with
 *           LCDIOC_SETGLYPH and sets LITOUT so the tty passes the bank
 *           codes; a cell is 6 by 9 pixels, so a bar resolves a sixth of a
 *           cell, a graph column a ninth, and a mosaic cell holds six
 *           3 by 3 pixel squares. An uploaded glyph stays in the bank
 *           after the program exits, until the console's reset (ESC c)
 *           or another upload of the same code, so what picofetch drew
 *           stays on the screen.
 * PC_VT     any other terminal: the same layout from ASCII characters,
 *           with SGR colors when color is on.
 * PC_PLAIN  output that is not a terminal, or -p: ASCII and no escape
 *           sequence at all.
 *
 * Bank codes, all in the 0xa0-0xff range a raw byte selects:
 *   0xa0-0xa5  the leftmost 1 to 6 pixel columns of the cell
 *   0xa6-0xae  the lowest 1 to 9 pixel rows of the cell
 *   0xaf       the bar's empty track, the three middle rows
 *   0xb0-0xee  mosaic masks 1 to 63: bit 0 top left, bit 1 top right,
 *              bits 2 and 3 the middle pair, bits 4 and 5 the bottom pair
 *
 * Colors are the console's sixteen: 0-7 the SGR 30-37 colors, 8-15 their
 * bright variants. A background takes 0-7 only (sys/arch/rp2040/dev/
 * lcdterm.h), so a bar segment that must show as a background uses 0-7.
 */
#ifndef PICOFETCH_PCDRAW_H
#define PICOFETCH_PCDRAW_H

#include <stddef.h>

enum { PC_PLAIN, PC_VT, PC_BANK };

#define	PC_HBAR		0xa0		/* + columns - 1 */
#define	PC_VBAR		0xa6		/* + rows - 1 */
#define	PC_TRACK	0xaf
#define	PC_MOSAIC	0xb0		/* + mask - 1 */
#define	PC_GLYPH_ROWS	9

struct pcterm {
	int	tier;			/* PC_PLAIN, PC_VT or PC_BANK */
	int	color;			/* nonzero: SGR colors are written */
	int	rows, cols;		/* cells */
	int	xpixel, ypixel;		/* panel pixels, or 0 when unknown */
};

/*
 * pc_init() sizes the terminal on descriptor fd and picks the tier. color
 * is 1 to write colors, 0 not to, and -1 to write them on a terminal
 * unless NO_COLOR is set; plain forces PC_PLAIN. pc_done() restores the
 * tty's local mode word and the default colors, and is safe to call
 * twice and from a signal handler.
 */
void	pc_init(struct pcterm *t, int fd, int color, int plain);
void	pc_done(void);

/* The nine rows of a bank glyph; returns 0 for a code outside the set. */
int	pc_glyph_rows(int code, unsigned char rows[PC_GLYPH_ROWS]);

void	pc_fg(const struct pcterm *t, int color);	/* 0-15 */
void	pc_bg(const struct pcterm *t, int color);	/* 0-7 */
void	pc_bold(const struct pcterm *t);
void	pc_reverse(const struct pcterm *t);
void	pc_reset(const struct pcterm *t);

/*
 * A horizontal bar of `cells` cells showing nseg adjacent segments, segment
 * i being val[i] of total in color col[i] (0-7). The rest of the bar is
 * the empty track. Leaves the default colors selected.
 */
void	pc_bar(const struct pcterm *t, int cells, int nseg, const long *val,
	    const int *col, long total);

/* How many sixths of a cell the first `part` of `total` fills in `cells`. */
int	pc_sixths(int cells, long part, long total);

/* One graph cell filled to `level` ninths (0-9) from the bottom. */
void	pc_vcell(const struct pcterm *t, int level);

/* One mosaic cell, mask 0-63. */
void	pc_mosaic(const struct pcterm *t, int mask);

/*
 * End a line. LITOUT, which the bank tier sets, turns off the tty's
 * newline translation, so that tier writes the carriage return itself.
 */
void	pc_nl(const struct pcterm *t);

/* A size given in kbytes as "988K", "29.7M" or "29.1G": kbytes below 10,000,
 * megabytes below 1,000. */
void	pc_size(char *buf, size_t len, long kb);

/* A duration in seconds as "3d 04:05:06" or "04:05:06". */
void	pc_uptime(char *buf, size_t len, long secs);

/* A horizontal rule of n cells. */
void	pc_rule(const struct pcterm *t, int n);

#endif /* !PICOFETCH_PCDRAW_H */
