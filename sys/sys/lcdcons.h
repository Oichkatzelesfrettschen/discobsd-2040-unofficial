#ifndef _SYS_LCDCONS_H_
#define _SYS_LCDCONS_H_

#include <sys/types.h>
#include <sys/ioctl.h>

/*
 * User-definable glyphs of the PicoCalc console (/dev/console on the
 * PICOCALC kernels).
 *
 * The console draws a cell whose code is 0x80-0xff from a RAM bank of 128
 * glyphs. Each glyph is nine pixel rows, the leftmost of the cell's six
 * columns in bit 7; the six columns occupy bits 7-2, so bits 1-0 of every
 * row must be zero. A tenth cell row repeats the ninth. LCDIOC_SETGLYPH
 * replaces one glyph and redraws the visible cells that show it.
 *
 * The bank starts with the DEC special graphics shapes in codes 0x80-0x9f,
 * which `ESC ( 0` selects in place of 0x5f-0x7e (0x5f + n draws code
 * 0x80 + n), and with the glyph of '?' in codes 0xa0-0xff. A raw byte
 * 0xa0-0xff written to the console draws its own code; a raw byte
 * 0x80-0x9f is a C1 control and draws '?'. The console's reset
 * (`ESC c`) restores the initial bank.
 *
 * The tty output path keeps only the low seven bits of each byte unless the
 * terminal is in RAW or LITOUT mode (ttyoutput() in sys/kern/tty.c), so a
 * program that writes bank codes 0xa0-0xff sets LITOUT (TIOCLBIS with
 * LLITOUT) or RAW first and restores its previous mode on exit. The DEC
 * set through `ESC ( 0` uses seven-bit codes and needs neither.
 *
 * The ioctl returns EINVAL for a code below 0x80 or a row with a bit set
 * in bits 1-0, EAGAIN when the panel is busy with another drawer, and
 * ENOTTY on a console that has no bank.
 */
#define LCD_GLYPH_FIRST		0x80
#define LCD_GLYPH_ROWS		9

struct lcd_glyph {
	u_char	code;			/* LCD_GLYPH_FIRST to 0xff. */
	u_char	rows[LCD_GLYPH_ROWS];	/* Top row first. */
};

#define LCDIOC_SETGLYPH		_IOW('l', 1, struct lcd_glyph)

#endif /* _SYS_LCDCONS_H_ */
