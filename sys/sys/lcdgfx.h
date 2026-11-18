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
 * to the text configuration (RGB565, a 320-line scroll area with a
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
 * three bytes per pixel) or 0x55 (RGB565, two bytes). An open starts in
 * 0x66 and the close restores the console's 0x55. LCDG_INFO reports the
 * panel size, the atlas capacity in tiles (LCDG_ATLAS_N) and the bits per
 * pixel of the current format (18 or 16).
 *
 * The tile atlas holds LCDG_ATLAS_N tiles of 8x8 pixels, each pixel a palette
 * index of four bits: a tile is LCDG_TILE_BYTES (32) bytes, its eight pixel
 * lines top to bottom, each line four bytes, the leftmost pixel in the high
 * nibble of the first byte. The atlas is zero (every pixel palette entry 0)
 * at every open. The tile grid is LCDG_GRID columns and rows of tiles on the
 * panel; tile (tx, ty) covers pixels 8 tx .. 8 tx + 7 and 8 ty .. 8 ty + 7.
 *
 * LCDG_TILESET stores tiles first .. first + count - 1 and requires count at
 * least 1 and first + count at most LCDG_ATLAS_N. The following write(2)
 * calls supply count * 32 bytes, tile by tile; each write(2) may end inside
 * a tile, bytes beyond the last one are left unread, and the bytes that have
 * arrived stay in the atlas if another command or the close abandons the
 * upload. It draws nothing.
 *
 * LCDG_TILEWIN draws the tiles of a tw x th block whose top left tile is
 * (tx, ty); tw and th are at least 1 and the block lies inside the grid. The
 * following write(2) calls supply tw * th tile numbers, one byte each, row
 * by row from the top left. Each row of tiles is complete once its tw numbers
 * have arrived; the kernel then draws it as one panel window of tw * 8 by 8
 * pixels in which the eight pixel lines of every tile of the row are
 * interleaved (line 0 of each tile left to right, then line 1, and so on),
 * through the palette in force at that moment. A tile number of
 * LCDG_ATLAS_N or more fails the write(2) with EINVAL before its row is
 * drawn, abandons the rest of the block and leaves the rows drawn so far on
 * the panel. A write(2) beyond the last tile number transfers only the
 * numbers that remain.
 *
 * The block form of a tile program is one LCDG_TILEWIN per run of changed
 * tiles in a tile row (games/gametile.h). Either ioctl returns EINVAL,
 * without drawing, for a field out of range. As with LCDG_WINDOW, every
 * other command, a rejected one included, and the last close abandon a
 * stream that still wants data.
 *
 * The commands below exist in a kernel built with "options LCD_GFX_EXT"
 * (PICOCALC_SD); any other kernel answers them with ENOTTY.
 *
 * The controller's memory holds LCDG_RAM_LINES (480) lines and the glass
 * shows 320 of them: the lines from the scroll start on, wrapping inside
 * the scroll area LCDG_SCRDEF defines. LCDG_RAMFILL and LCDG_RAMWIN are
 * LCDG_FILL and LCDG_WINDOW with a line bound of LCDG_RAM_LINES, so a
 * program draws lines 320-479 while the glass shows 0-319 and brings them
 * into view with LCDG_SCRDEF and LCDG_SCROLL.
 *
 * LCDG_READ selects a rectangle of the controller's memory, lines up to
 * LCDG_RAM_LINES, and the following read(2) calls return its pixels row by
 * row from the top left as RGB565, two bytes per pixel, low byte first:
 * the upper five, six and five bits of the red, green and blue the
 * controller returns. Each read(2) carries an even number of bytes and one
 * beyond the last pixel returns only the pixels that remain; a read(2)
 * with no rectangle selected returns end of file. With fmt LCDG_RD_RAW the
 * rectangle is one line of at most LCDG_READ_MAX pixels and one read(2) of
 * at least LCDG_RD_RAW_BYTES(w) bytes returns the reply bytes of the
 * controller's memory read command as they arrived, for a program that
 * measures the reply's framing.
 *
 * LCDG_SPRITE draws a w x h block of palette indexes, lines up to
 * LCDG_RAM_LINES, in which the index `key` is transparent: the following
 * write(2) calls supply one byte per pixel, the low four bits a palette
 * entry, and the kernel sends each run of other indexes in a row as its own
 * panel window and leaves the pixels under a key index as they are.
 *
 * LCDG_WAITLINE returns once two successive reads of the controller's scan
 * line both lie in `line` .. `line` + LCDG_WAIT_SPAN - 1 without falling,
 * and stores the second in `at`; after `ms` milliseconds (1 to
 * LCDG_WAIT_MAX_MS) without such a pair it fails with ETIMEDOUT. A program
 * that starts a draw behind the scan line finishes it before the scan
 * returns to it.
 *
 * LCDG_POWER selects LCDG_PWR_ON, LCDG_PWR_IDLE (eight colors, the
 * controller's idle mode), LCDG_PWR_OFF (display off, memory kept and
 * writable) or LCDG_PWR_SLEEP (sleep in, memory kept). Entering and leaving
 * sleep each take 120 ms. In sleep every command but LCDG_POWER and
 * LCDG_XINFO, and write(2), fail with EBUSY. The last close and
 * a kernel fault report return the panel to LCDG_PWR_ON, so a mode lasts no
 * longer than the program that selected it.
 *
 * LCDG_DITHER is a probe of the controller, not a drawing command. It
 * selects COLMOD 0x77, with `on` set sends the command-2 unlock, MODESEL
 * (B9h) 02h and the lock, writes LCDG_DITHER_N pixels of LCDG_DITHER_BYTE in
 * every component to columns LCDG_DITHER_X on of the last memory line, and
 * returns the panel to the format in force. A LCDG_READ of those pixels
 * then shows what the memory took: the datasheet gives 24-bit input and its
 * dithering to eighteen bits for the parallel bus only, so the reply shows
 * whether the serial link accepts the format and whether B9h changes what
 * is stored. Dithering stays selected until the panel is next reset; it
 * acts on 24-bit input alone.
 *
 * LCDG_XINFO reports the controller's line count, the first line of the
 * text console's ring at the open (the memory line the console showed at
 * the top of the glass), the power mode and LCDG_READ_MAX.
 */
#define LCDG_ATLAS_N		128
#define LCDG_TILE_W		8
#define LCDG_TILE_H		8
#define LCDG_TILE_BYTES		32
#define LCDG_GRID		40
#define LCDG_WIDTH		320
#define LCDG_HEIGHT		320
#define LCDG_RAM_LINES		480	/* The controller's line count. */
#define LCDG_NPAL		16

#define LCDG_WIN_RGB565		0
#define LCDG_WIN_INDEX		1
#define LCDG_WIN_BITS		2

#define LCDG_COLMOD_RGB666	0x66
#define LCDG_COLMOD_RGB565	0x55

#define LCDG_RD_RGB565		0
#define LCDG_RD_RAW		1
#define LCDG_READ_MAX		20	/* Pixels of one memory read. */
#define LCDG_RD_LEAD_BITS	8	/* Reply bits ahead of the first pixel. */
/* Reply bytes of an n-pixel read: the lead, 24 bits a pixel and one spare. */
#define LCDG_RD_RAW_BYTES(n)	((LCDG_RD_LEAD_BITS + 24 * (n) + 7) / 8 + 1)

#define LCDG_DITHER_N		8
#define LCDG_DITHER_X		8
#define LCDG_DITHER_BYTE	0x82	/* Halfway between two 6-bit levels. */

#define LCDG_WAIT_SPAN		64
#define LCDG_WAIT_MAX_MS	1000

#define LCDG_PWR_ON		0
#define LCDG_PWR_IDLE		1
#define LCDG_PWR_OFF		2
#define LCDG_PWR_SLEEP		3

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

/* LCDG_TILESET: first + count <= LCDG_ATLAS_N, count >= 1. */
struct lcdg_tileset {
	u_char	first, count;
};

/* LCDG_TILEWIN: tw and th >= 1, tx + tw and ty + th <= LCDG_GRID. */
struct lcdg_tilewin {
	u_char	tx, ty, tw, th;
};

/* LCDG_READ: w and h at least 1, x + w <= 320, y + h <= LCDG_RAM_LINES. */
struct lcdg_read {
	u_short	x, y, w, h;
	u_char	fmt;
};

/* LCDG_SPRITE: the bounds of LCDG_READ; key is a palette index. */
struct lcdg_sprite {
	u_short	x, y, w, h;
	u_char	key;
};

struct lcdg_wait {
	u_short	line, ms;
	u_short	at;
};

struct lcdg_power {
	u_char	mode;
};

struct lcdg_dither {
	u_char	on;
};

struct lcdg_xinfo {
	u_short	ram_lines, text_top;
	u_char	power, read_max;
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
#define LCDG_TILESET	_IOW('l', 27, struct lcdg_tileset)
#define LCDG_TILEWIN	_IOW('l', 28, struct lcdg_tilewin)
#define LCDG_RAMFILL	_IOW('l', 29, struct lcdg_rect)
#define LCDG_RAMWIN	_IOW('l', 30, struct lcdg_window)
#define LCDG_READ	_IOW('l', 31, struct lcdg_read)
#define LCDG_SPRITE	_IOW('l', 32, struct lcdg_sprite)
#define LCDG_WAITLINE	_IOWR('l', 33, struct lcdg_wait)
#define LCDG_POWER	_IOW('l', 34, struct lcdg_power)
#define LCDG_XINFO	_IOR('l', 35, struct lcdg_xinfo)
#define LCDG_DITHER	_IOW('l', 36, struct lcdg_dither)

#endif /* _SYS_LCDGFX_H_ */
