#ifndef _SYS_LCDGFX_H_
#define _SYS_LCDGFX_H_

#include <sys/types.h>
#include <sys/ioctl.h>

/*
 * The PicoCalc's pixel device, /dev/lcdgfx (character major 10, minor 1 on
 * the PICOCALC kernels; the console tty is minor 0).
 *
 * Opening the device switches the 320x320 panel from the text console to
 * graphics mode; the console keeps updating its cell store but draws
 * nothing. The last close, which process exit performs, returns the panel
 * to the text configuration (RGB666, a 320-line scroll area with a
 * 160-line bottom fixed area) and repaints every cell. A second open fails
 * with EBUSY. Input stays on the console tty: a program opens /dev/tty for
 * the keyboard and /dev/lcdgfx for the panel.
 *
 * Coordinates are panel pixels, (0,0) at the top left of the screen, x to
 * the right and y downward. Every field is unsigned, so a negative
 * coordinate is a value above 65,535 - n and is out of range like any other.
 * Every command rejects, with EINVAL and without drawing, a coordinate
 * outside the panel or a shape that leaves it; the kernel does not clip, so
 * a program clips its own shapes. Colors are
 * RGB565 values (0xf800 red, 0x07e0 green, 0x001f blue), widened to RGB666
 * by bit replication while the panel runs COLMOD 0x66. A payload is at most
 * 12 bytes.
 *
 * LCDG_WINDOW opens a rectangle and the following write(2) calls supply its
 * pixels, row by row from the top left:
 *
 *	LCDG_WIN_RGB565	two bytes per pixel, low byte first (a u_short array);
 *			each write(2) carries an even number of bytes
 *	LCDG_WIN_INDEX	one byte per pixel, the low four bits select a palette
 *			entry
 *	LCDG_WIN_BITS	one bit per pixel, most significant bit first, each row
 *			padded to a whole byte; a set bit draws palette entry fg
 *			and a clear bit entry bg of the LCDG_WINDOW payload
 *
 * A write(2) beyond the last pixel of the window transfers only the pixels
 * that remain, and a write(2) with no window open fails with EINVAL. Any
 * other command, and the last close, abandon a window with pixels missing.
 *
 * The palette holds 16 RGB565 entries and starts as the console's colors
 * (0-7 the SGR 30-37 colors, 8-15 their bright variants) at every open.
 * LCDG_PALETTE keeps the upper five (red, blue) or six (green) bits of
 * each 8-bit component.
 *
 * LCDG_SCRDEF programs VSCRDEF with a top fixed area of `top` lines and a
 * bottom fixed area of `bottom` lines, so the scroll area is 480 - top -
 * bottom lines of the controller's 480, at least one; it also moves the
 * scroll start to the first line of the scroll area. LCDG_SCROLL then sets
 * the scroll start (VSCSAD) to `line`, which lies inside the scroll area.
 * Open leaves top 0, bottom 160 and scroll start 0, so the 320 visible
 * lines are the first 320 RAM lines.
 *
 * LCDG_COLMOD selects the pixel format the panel receives: 0x66 (RGB666,
 * three bytes per pixel) or 0x55 (RGB565, two bytes). The close restores
 * 0x66. LCDG_INFO reports the panel size, the atlas capacity in tiles (0:
 * the device has no tile atlas) and the bits per pixel of the current
 * format (18 or 16).
 */
#define LCDG_WIDTH		320
#define LCDG_HEIGHT		320
#define LCDG_RAM_LINES		480	/* The controller's line count. */
#define LCDG_NPAL		16

#define LCDG_WIN_RGB565		0
#define LCDG_WIN_INDEX		1
#define LCDG_WIN_BITS		2

#define LCDG_COLMOD_RGB666	0x66
#define LCDG_COLMOD_RGB565	0x55

/* LCDG_FILL and LCDG_FRAME: x0 <= x1 and y0 <= y1, both corners drawn. */
struct lcdg_rect {
	u_short	x0, y0, x1, y1;
	u_short	color;
};

/* LCDG_LINE: both endpoints drawn, in either order. */
struct lcdg_line {
	u_short	x0, y0, x1, y1;
	u_short	color;
};

struct lcdg_pixel {
	u_short	x, y;
	u_short	color;
};

/* LCDG_CIRCLE: the whole circle, x - r .. x + r and y - r .. y + r, lies on the panel. */
struct lcdg_circle {
	u_short	x, y, r;
	u_short	color;
};

/* LCDG_WINDOW: w and h are at least 1; fg and bg matter to LCDG_WIN_BITS. */
struct lcdg_window {
	u_short	x, y, w, h;
	u_char	fmt;
	u_char	fg, bg;
};

struct lcdg_palette {
	u_char	index, r, g, b;
};

struct lcdg_scroll {
	u_short	line;
};

struct lcdg_scrdef {
	u_short	top, bottom;
};

struct lcdg_colmod {
	u_char	fmt;
};

struct lcdg_info {
	u_short	w, h;
	u_char	tiles, bpp;
};

#define LCDG_FILL	_IOW('l', 16, struct lcdg_rect)
#define LCDG_PIXEL	_IOW('l', 17, struct lcdg_pixel)
#define LCDG_LINE	_IOW('l', 18, struct lcdg_line)
#define LCDG_FRAME	_IOW('l', 19, struct lcdg_rect)
#define LCDG_CIRCLE	_IOW('l', 20, struct lcdg_circle)
#define LCDG_WINDOW	_IOW('l', 21, struct lcdg_window)
#define LCDG_PALETTE	_IOW('l', 22, struct lcdg_palette)
#define LCDG_SCROLL	_IOW('l', 23, struct lcdg_scroll)
#define LCDG_SCRDEF	_IOW('l', 24, struct lcdg_scrdef)
#define LCDG_COLMOD	_IOW('l', 25, struct lcdg_colmod)
#define LCDG_INFO	_IOR('l', 26, struct lcdg_info)

#endif /* _SYS_LCDGFX_H_ */
