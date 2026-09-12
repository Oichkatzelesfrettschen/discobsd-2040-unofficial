/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Public domain (Unlicense); see LICENSE in this directory.
 *
 * Terminal layer for one console: a fixed 80x24 ANSI/VT100 terminal on
 * the USB CDC-ACM line, driven with raw escapes and no termcap. The
 * target build enters raw mode through this port's sgtty ioctls
 * (TIOCGETP/TIOCSETP, RAW and ECHO in include/sys/ioctl.h), the same
 * mechanism games/gametty.h uses; HOSTBUILD swaps in termios because a
 * Linux or BSD host wires no sgtty ioctl to a pty. Both paths leave
 * canonical mode and echo off and read one byte at a time.
 */

#include <stdio.h>
#include <unistd.h>
#include "stevie.h"

#ifdef HOSTBUILD
#include <termios.h>
static struct termios tty_saved;
#else
#include <sgtty.h>
#include <sys/ioctl.h>
static struct sgttyb tty_saved;
#endif

static int tty_israw = 0;

windinit()
{
#ifdef HOSTBUILD
	struct termios t;

	tcgetattr(0, &tty_saved);
	t = tty_saved;
	t.c_lflag &= ~(ICANON | ECHO | ISIG);
	t.c_iflag &= ~(IXON | ICRNL);
	t.c_oflag &= ~OPOST;
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &t);
#else
	struct sgttyb sg;

	ioctl(0, TIOCGETP, &tty_saved);
	sg = tty_saved;
	sg.sg_flags |= RAW;
	sg.sg_flags &= ~ECHO;
	ioctl(0, TIOCSETP, &sg);
#endif
	tty_israw = 1;

	Columns = 80;
	Rows = 24;
}

/*
 * windrestore returns the console to the mode the shell handed over.
 * Upstream has no restore path at all, so every :q left the terminal
 * raw and unechoed; windexit and every error exit route through here.
 */
windrestore()
{
	if (!tty_israw)
		return;
	tty_israw = 0;
	fflush(stdout);
#ifdef HOSTBUILD
	tcsetattr(0, TCSANOW, &tty_saved);
#else
	ioctl(0, TIOCSETP, &tty_saved);
#endif
}

/*
 * Rows and columns are 0-based everywhere in the editor and 1-based in
 * CUP, and ECMA-48 clamps parameter 0 up to 1, so the upstream
 * "\033[%d;%dH" collided rows 0 and 1 and put the status line one row
 * above the bottom.
 */
windgoto(r,c)
int r,c;
{
	printf("\033[%d;%dH", r+1, c+1);
}

windexit(r)
int r;
{
	windrestore();
	exit(r);
}

windclear()
{
	printf("\033[2J");
}

/*
 * Nothing the editor paints ends in a newline, so stdout only reaches
 * the terminal when this flushes it. Upstream left windrefresh empty,
 * which works only where the tty is unbuffered.
 */
windrefresh()
{
	fflush(stdout);
}

/*
 * Read one byte straight from the descriptor. vpeekc and anyinput only
 * inspect the stuffin() replay buffer, so no readahead poll is needed.
 */
windgetc()
{
	char c;

	windrefresh();
	/* A console that cannot be read is gone. Upstream returned EOF
	 * here, which normal() answers with default: beep() and loops,
	 * so a dropped CDC-ACM line became an endless beep. */
	if (read(0, &c, 1) != 1)
		windexit(1);
	return (unsigned char)c;
}

windstr(s)
char *s;
{
	printf("%s",s);
}

windputc(c)
int c;
{
	putchar(c);
}

beep()
{
	putchar('\007');
	windrefresh();
}
