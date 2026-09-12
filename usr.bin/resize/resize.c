/*
 * resize -- set the console's window size to the terminal's real size.
 *
 * A serial line carries no window size and no resize event, so the kernel
 * opens the console at 80x24 (sys/arch/rp2040/dev/usb.c) and a full-screen
 * program reads that through TIOCGWINSZ. When the terminal emulator is a
 * different size, this asks it directly with the xterm cursor-report
 * protocol -- park the cursor far past the corner and request its position
 * with ESC [ 6 n -- and writes the reported rows and columns back with
 * TIOCSWINSZ (sys/kern/tty.c), which the kernel then reports to every
 * program and announces with SIGWINCH.
 *
 * The reply is read in raw mode so the line discipline neither echoes nor
 * waits for a newline, and a terminal that does not answer within the poll
 * leaves the 80x24 default in place.
 */
#include <sys/ioctl.h>
#include <sys/file.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#ifdef TERMIOS
#include <termios.h>
#else
#include <sgtty.h>
#endif

static int
query(int fd, int *rows, int *cols)
{
	char buf[32];
	unsigned int i;
	int n, tries;

	/* Park at 999,999 and request the cursor position. */
	if (write(fd, "\033[999;999H\033[6n", 14) != 14)
		return -1;

	/*
	 * The reply is ESC [ rows ; cols R. Each byte is awaited with an
	 * FIONREAD poll of at most half a second, so a silent terminal fails
	 * rather than blocking the raw read for good.
	 */
	i = 0;
	while (i < sizeof(buf) - 1) {
		tries = 50;
		n = 0;
		while (tries-- > 0) {
			if (ioctl(fd, FIONREAD, &n) == -1)
				return -1;
			if (n > 0)
				break;
			usleep(10000);
		}
		if (n <= 0)
			return -1;
		if (read(fd, buf + i, 1) != 1)
			return -1;
		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';
	if (buf[0] != '\033' || buf[1] != '[')
		return -1;
	if (sscanf(buf + 2, "%d;%d", rows, cols) != 2)
		return -1;
	return 0;
}

int
main(void)
{
	struct winsize ws;
	int rows, cols, ok;
#ifdef TERMIOS
	struct termios save, raw;
#else
	struct sgttyb save, raw;
#endif

	if (! isatty(0))
		return 1;

#ifdef TERMIOS
	if (tcgetattr(0, &save) < 0)
		return 1;
	raw = save;
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &raw);
#else
	if (ioctl(0, TIOCGETP, &save) < 0)
		return 1;
	raw = save;
	raw.sg_flags |= CBREAK;
	raw.sg_flags &= ~ECHO;
	ioctl(0, TIOCSETP, &raw);
#endif

	ok = query(0, &rows, &cols);

#ifdef TERMIOS
	tcsetattr(0, TCSANOW, &save);
#else
	ioctl(0, TIOCSETP, &save);
#endif

	if (ok < 0 || rows < 1 || cols < 1) {
		fprintf(stderr, "resize: no answer from the terminal; leaving the size unchanged\n");
		return 1;
	}
	if (ioctl(0, TIOCGWINSZ, &ws) < 0)
		memset(&ws, 0, sizeof(ws));
	ws.ws_row = rows;
	ws.ws_col = cols;
	if (ioctl(0, TIOCSWINSZ, &ws) < 0) {
		fprintf(stderr, "resize: cannot set the window size\n");
		return 1;
	}
	printf("%d rows, %d columns\n", rows, cols);
	return 0;
}
