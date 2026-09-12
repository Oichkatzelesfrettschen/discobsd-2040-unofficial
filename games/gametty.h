/*
 * Raw terminal mode and ANSI cursor helpers shared by the games in
 * games/fifteen, games/keen and games/bubble. The target build uses
 * this port's sgtty ioctls (TIOCGETP/TIOCSETP, RAW and ECHO in
 * include/sys/ioctl.h), the same mechanism usr.bin/re's r.ttyio.c
 * sets up through the sgtty branch of its TERMIOS #ifdef. A Linux or
 * BSD host has no sgtty ioctl wired to a pty, so the HOSTBUILD build
 * (see each game's Makefile "host" target) uses termios instead; both
 * paths leave canonical mode and echo off and read one byte at a time.
 *
 * Console is assumed 80 columns by 24 rows, ANSI/VT100 escapes, as
 * given by a USB CDC-ACM terminal emulator.
 */
#ifndef GAMES_GAMETTY_H
#define GAMES_GAMETTY_H

#include <unistd.h>

#ifdef HOSTBUILD
#include <termios.h>
static struct termios gtty_saved;

static void
gtty_raw(void)
{
	struct termios t;

	tcgetattr(0, &gtty_saved);
	t = gtty_saved;
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &t);
}

static void
gtty_restore(void)
{
	tcsetattr(0, TCSANOW, &gtty_saved);
}

#else /* !HOSTBUILD */
#include <sgtty.h>
#include <sys/ioctl.h>
static struct sgttyb gtty_saved;

static void
gtty_raw(void)
{
	struct sgttyb sg;

	ioctl(0, TIOCGETP, &gtty_saved);
	sg = gtty_saved;
	sg.sg_flags |= RAW;
	sg.sg_flags &= ~ECHO;
	ioctl(0, TIOCSETP, &sg);
}

static void
gtty_restore(void)
{
	ioctl(0, TIOCSETP, &gtty_saved);
}
#endif /* HOSTBUILD */

/* Cursor keys arrive as ESC [ A/B/C/D; gtty_getkey folds them to
 * single codes so callers never parse escapes themselves. */
#define GK_UP		'k'
#define GK_DOWN		'j'
#define GK_LEFT		'h'
#define GK_RIGHT	'l'

static int
gtty_getkey(void)
{
	char c, c2;

	if (read(0, &c, 1) != 1)
		return -1;
	if (c != 033)
		return c;
	if (read(0, &c, 1) != 1)
		return 033;
	if (c != '[')
		return 033;
	if (read(0, &c2, 1) != 1)
		return 033;
	switch (c2) {
	case 'A': return GK_UP;
	case 'B': return GK_DOWN;
	case 'C': return GK_RIGHT;
	case 'D': return GK_LEFT;
	default:  return 033;
	}
}

static void
gtty_clear(void)
{
	write(1, "\033[2J\033[H", 7);
}

static void
gtty_home(void)
{
	write(1, "\033[H", 3);
}

#endif /* GAMES_GAMETTY_H */
