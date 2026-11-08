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
 *
 * Raw key events. The tty delivers a key as the bytes an xterm sends, which
 * carry presses and the keyboard's auto-repeat but no release. A raw event
 * session reports the events the keyboard controller sent, as it sent them:
 * a state (LCD_KEY_PRESSED, LCD_KEY_HOLD or LCD_KEY_RELEASED) and the key
 * byte. A key held down reports again, as further PRESSED or HOLD events, at
 * the controller's repeat rate. The key byte is the printable ASCII character
 * as typed after Shift, Caps Lock and the symbol layer, or a controller code
 * (LCD_KEY_*); Shift, Ctrl, Alt and the other modifiers report as keys of
 * their own. The tty path is unchanged: every event also takes its normal
 * course into the input queue, so a program that reads events sets RAW and
 * clears ECHO, as the games do, and ignores or flushes the bytes.
 *
 * Keystrokes include passwords, so the driver records nothing until a
 * program asks. LCDIOC_KEYEVS with 1 starts a session, owned by the calling
 * process and the console file it is called on; with 0 the owner ends it.
 * Only the owning process may drain or end it (EPERM for any other process,
 * including one that shares the file, such as the parent shell). Starting
 * needs a descriptor open for reading (EBADF otherwise), a process in the
 * tty's foreground process group (EIO otherwise, as a background read of the
 * tty with SIGTTIN ignored) and a tty in RAW or CBREAK mode (EINVAL
 * otherwise: cooked input is never recorded); it returns EBUSY while another
 * open file owns a session. A program sets its mode first, with TIOCSETP and
 * no echo as the games do, and starts the session after. The
 * queue is empty when a session starts and when it ends. The session ends
 * when the owning process exits or execs (a new program image does not
 * inherit it), when the owning open file is closed for the last time, when
 * the owner ends it, when the tty's foreground group is no longer the group
 * the owner started in, and when the tty's mode word is not the one it had at
 * the start (RAW, CBREAK, ECHO, CRMOD and every other bit the user sets,
 * changed by any process through any descriptor, the owner included; the bits
 * the line discipline toggles itself, PENDIN and FLUSHO, do not count). A
 * mode-setting ioctl (TIOCSETP, TIOCSETN, TIOCLBIS, TIOCLBIC, TIOCLSET) from
 * any process other than the owner ends the session even when it sets the
 * flags it found. The driver records no event after the session ends, and
 * getpass(), which clears ECHO through /dev/tty and reads a cooked line, ends
 * it. A program that must change its mode mid-game ends its own session and
 * starts a new one after.
 *
 * LCDIOC_GETKEY removes up to LCD_KEYEV_N events, oldest first, into
 * ev[0 .. n - 1], and sets `lost` to the number dropped since the previous
 * call (255 at most); a program that sees lost nonzero rebuilds its key
 * state from what it reads next. It needs a descriptor open for reading
 * (EBADF) and a process in the foreground group (EIO), never stops the
 * caller with SIGTTIN, returns EINVAL when no session is active and EPERM to
 * a process that does not own it, and leaves the queue untouched when it
 * fails. n is zero when no event waits: the call
 * never blocks. The driver keeps the most recent LCD_KEYEV_QUEUE events;
 * when the queue is full the oldest is dropped and counted, so the newest
 * state always survives. Both commands apply to the console tty only.
 */
#define LCD_KEY_PRESSED		1
#define LCD_KEY_HOLD		2
#define LCD_KEY_RELEASED	3

#define LCD_KEY_BACKSPACE	0x08
#define LCD_KEY_TAB		0x09
#define LCD_KEY_ENTER		0x0a
#define LCD_KEY_CTRL		0xa5
#define LCD_KEY_ESC		0xb1
#define LCD_KEY_LEFT		0xb4
#define LCD_KEY_UP		0xb5
#define LCD_KEY_DOWN		0xb6
#define LCD_KEY_RIGHT		0xb7

#define LCD_KEYEV_N		6	/* Events one call returns. */
#define LCD_KEYEV_QUEUE		32	/* Events the driver keeps. */
#define LCD_GLYPH_FIRST		0x80
#define LCD_GLYPH_ROWS		9

struct lcd_glyph {
	u_char	code;			/* LCD_GLYPH_FIRST to 0xff. */
	u_char	rows[LCD_GLYPH_ROWS];	/* Top row first. */
};

#define LCDIOC_SETGLYPH		_IOW('l', 1, struct lcd_glyph)

struct lcd_keyev {
	u_char	state;			/* LCD_KEY_PRESSED, _HOLD or _RELEASED. */
	u_char	key;
};

struct lcd_keyevs {
	u_char	n;			/* Events in ev. */
	u_char	lost;			/* Dropped since the previous call. */
	struct lcd_keyev ev[LCD_KEYEV_N];
};

#define LCDIOC_GETKEY		_IOR('l', 2, struct lcd_keyevs)
#define LCDIOC_KEYEVS		_IOW('l', 3, int)	/* 1: start a session, 0: end it. */

#endif /* _SYS_LCDCONS_H_ */
