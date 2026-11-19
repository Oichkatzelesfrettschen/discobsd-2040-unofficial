/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * word_term.c: the terminal. Raw mode goes through termios on a host
 * build (TERMIOS) and through the 2.11BSD sgtty calls on DiscoBSD. Keys
 * arrive as bytes and leave as one key code: an escape sequence is read
 * to its final byte, and a lone ESC is told from the start of a sequence
 * by a 60 ms select() for the next byte, the same on both builds. The
 * screen is two cell arrays: the frame being composed and the frame on
 * the terminal; scr_flush() sends only the cells that differ, with ECMA-48
 * cursor addressing, SGR attributes and colors, and the DEC line-drawing
 * set for the end mark; the line and corner glyph codes are reserved for a
 * window frame that the screen does not draw yet.
 */

#include "word.h"

#ifdef TERMIOS
#include <termios.h>
#else
#include <sgtty.h>
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int scr_rows = 24, scr_cols = 80, scr_color = 1;

static volatile sig_atomic_t rawmode;
#ifdef TERMIOS
static struct termios orig_tio;
#else
static struct sgttyb orig_sg;
static struct tchars orig_tc;
static struct ltchars orig_ltc;
#endif

/* Cell: the character in the low byte, the attributes in the high. */
static uint16_t nxt[WORD_ROWS_MAX][WORD_COLS_MAX];
static uint16_t cur[WORD_ROWS_MAX][WORD_COLS_MAX];

static char obuf[512];
static size_t olen;

static void
oflush(void)
{
	size_t off = 0;

	while (off < olen) {
		ssize_t k = write(STDOUT_FILENO, obuf + off, olen - off);

		if (k < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		off += (size_t)k;
	}
	olen = 0;
}

static void
oput(const char *s, size_t n)
{
	while (n > 0) {
		size_t k = sizeof(obuf) - olen;

		if (k > n)
			k = n;
		memcpy(obuf + olen, s, k);
		olen += k;
		s += k;
		n -= k;
		if (olen == sizeof(obuf))
			oflush();
	}
}

static void
ostr(const char *s)
{
	oput(s, strlen(s));
}

void
tty_size(void)
{
#ifdef TIOCGWINSZ
	struct winsize ws;

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row >= 8 &&
	    ws.ws_col >= 30) {
		scr_rows = ws.ws_row;
		scr_cols = ws.ws_col;
	}
#endif
	if (scr_rows > WORD_ROWS_MAX)
		scr_rows = WORD_ROWS_MAX;
	if (scr_cols > WORD_COLS_MAX)
		scr_cols = WORD_COLS_MAX;
}

#define TTY_RESTORE_SEQ "\033[0m\033(B\033[?7h\033[?25h\033[2J\033[H\033[?1049l"

/* Put the terminal back; write(2), tcsetattr(3) and ioctl(2) only. */
static void
tty_reset_modes(void)
{
	ssize_t r = write(STDOUT_FILENO, TTY_RESTORE_SEQ,
	    sizeof(TTY_RESTORE_SEQ) - 1);

	(void)r;
#ifdef TERMIOS
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);
#else
	ioctl(STDIN_FILENO, TIOCSETP, &orig_sg);
	ioctl(STDIN_FILENO, TIOCSETC, &orig_tc);
	ioctl(STDIN_FILENO, TIOCSLTC, &orig_ltc);
#endif
	rawmode = 0;
}

/*
 * A terminating signal restores the terminal and then takes its default
 * action, so a kill leaves neither raw mode nor the alternate screen.
 */
static void
on_term_signal(int sig)
{
	if (rawmode)
		tty_reset_modes();
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
}

static void
catch_term_signals(void)
{
	static const int sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
	static int done;
	unsigned i;

	if (done)
		return;
	done = 1;
	for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
		if (signal(sigs[i], on_term_signal) == SIG_IGN)
			signal(sigs[i], SIG_IGN);
}

int
tty_raw(void)
{
	if (rawmode)
		return 0;
	if (!isatty(STDIN_FILENO)) {
		errno = ENOTTY;
		return -1;
	}
#ifdef TERMIOS
	struct termios t;

	if (tcgetattr(STDIN_FILENO, &orig_tio) != 0)
		return -1;
	t = orig_tio;
	t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	t.c_oflag &= ~(tcflag_t)OPOST;
	t.c_cflag |= (tcflag_t)CS8;
	t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0)
		return -1;
#else
	struct sgttyb sg;
	struct tchars tc;
	struct ltchars ltc;

	if (ioctl(STDIN_FILENO, TIOCGETP, &orig_sg) != 0 ||
	    ioctl(STDIN_FILENO, TIOCGETC, &orig_tc) != 0 ||
	    ioctl(STDIN_FILENO, TIOCGLTC, &orig_ltc) != 0)
		return -1;
	sg = orig_sg;
	sg.sg_flags &= ~(ECHO | CRMOD | XTABS | RAW);
	sg.sg_flags |= CBREAK;
	tc = orig_tc;
	tc.t_intrc = tc.t_quitc = tc.t_startc = tc.t_stopc = tc.t_eofc = -1;
	ltc = orig_ltc;
	ltc.t_suspc = ltc.t_dsuspc = ltc.t_rprntc = ltc.t_flushc = -1;
	ltc.t_werasc = ltc.t_lnextc = -1;
	if (ioctl(STDIN_FILENO, TIOCSETP, &sg) != 0)
		return -1;
	ioctl(STDIN_FILENO, TIOCSETC, &tc);
	ioctl(STDIN_FILENO, TIOCSLTC, &ltc);
#endif
	rawmode = 1;
	catch_term_signals();
	ostr("\033[?1049h\033[?7l");
	scr_invalidate();
	return 0;
}

void
tty_restore(void)
{
	if (!rawmode)
		return;
	oflush();
	tty_reset_modes();
}

/* One byte, waiting at most ms milliseconds when ms is not negative. */
static int
getbyte(int ms)
{
	unsigned char c;
	ssize_t n;

	if (ms >= 0) {
		fd_set rd;
		struct timeval tv;

		FD_ZERO(&rd);
		FD_SET(STDIN_FILENO, &rd);
		tv.tv_sec = 0;
		tv.tv_usec = ms * 1000;
		if (select(STDIN_FILENO + 1, &rd, NULL, NULL, &tv) <= 0)
			return -1;
	}
	for (;;) {
		n = read(STDIN_FILENO, &c, 1);
		if (n == 1)
			return c;
		if (n == 0 || errno != EINTR)
			return -2;
	}
}

static int
mod_of(int m)
{
	int k = 0;

	m--;
	if (m & 1)
		k |= KM_SHIFT;
	if (m & 2)
		k |= KM_ALT;
	if (m & 4)
		k |= KM_CTRL;
	return k;
}

static int
tilde_key(int n)
{
	static const struct { uint8_t n; uint16_t k; } map[] = {
		{ 1, K_HOME }, { 2, K_INS }, { 3, K_DEL }, { 4, K_END },
		{ 5, K_PGUP }, { 6, K_PGDN }, { 7, K_HOME }, { 8, K_END },
		{ 11, K_F1 }, { 12, K_F2 }, { 13, K_F3 }, { 14, K_F4 },
		{ 15, K_F5 }, { 17, K_F6 }, { 18, K_F7 }, { 19, K_F8 },
		{ 20, K_F9 }, { 21, K_F10 }, { 23, K_F11 }, { 24, K_F12 }
	};
	size_t i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (map[i].n == n)
			return map[i].k;
	return K_NONE;
}

static int
final_key(int c)
{
	switch (c) {
	case 'A': return K_UP;
	case 'B': return K_DOWN;
	case 'C': return K_RIGHT;
	case 'D': return K_LEFT;
	case 'H': return K_HOME;
	case 'F': return K_END;
	case 'P': return K_F1;
	case 'Q': return K_F2;
	case 'R': return K_F3;
	case 'S': return K_F4;
	case 'Z': return K_BTAB;
	default:  return K_NONE;
	}
}

int
key_read(void)
{
	int c = getbyte(-1), p[2] = { 0, 0 }, np = 0, k;

	if (c < 0)
		return -1;
	if (c == '\n')
		return K_ENTER;
	if (c == 8)
		return K_BS;
	if (c != K_ESC)
		return c;
	c = getbyte(60);
	if (c < 0)
		return K_ESC;
	if (c != '[' && c != 'O') {
		if (c == K_ESC)
			return K_ESC;
		return KM_ALT | c;
	}
	if (c == 'O') {
		c = getbyte(60);
		k = c < 0 ? K_NONE : final_key(c);
		return k == K_NONE ? K_ESC : k;
	}
	for (;;) {
		c = getbyte(60);
		if (c < 0)
			return K_ESC;
		if (c >= '0' && c <= '9') {
			/* A parameter past 9999 stays there and names no key. */
			if (p[np] < 1000)
				p[np] = p[np] * 10 + (c - '0');
			else
				p[np] = 9999;
			continue;
		}
		if (c == ';' && np == 0) {
			np = 1;
			continue;
		}
		break;
	}
	if (c == '~') {
		k = tilde_key(p[0]);
		return k == K_NONE ? K_ESC : k | (np ? mod_of(p[1]) : 0);
	}
	k = final_key(c);
	if (k == K_NONE)
		return K_ESC;
	return k | (np ? mod_of(p[1]) : 0);
}

void
scr_clear(void)
{
	int r, c;

	for (r = 0; r < scr_rows; r++)
		for (c = 0; c < scr_cols; c++)
			nxt[r][c] = ' ';
}

void
scr_invalidate(void)
{
	memset(cur, 0xff, sizeof(cur));
	ostr("\033[0m\033[H\033[2J");
}

void
scr_put(int row, int col, int ch, int attr)
{
	if (row < 0 || row >= scr_rows || col < 0 || col >= scr_cols)
		return;
	if (ch >= 0x7f || (ch < 0x20 && ch > G_LR))
		ch = '?';
	nxt[row][col] = (uint16_t)((attr << 8) | ch);
}

int
scr_puts(int row, int col, const char *s, int attr, int max)
{
	int n = 0;

	while (*s != '\0' && n < max && col + n < scr_cols)
		scr_put(row, col + n++, (unsigned char)*s++, attr);
	return n;
}

void
scr_fill(int row, int col, int n, int attr)
{
	while (n-- > 0 && col < scr_cols)
		scr_put(row, col++, ' ', attr);
}

static void
sgr(int attr)
{
	static const char *const fg[] = {
		"", "44;97", "1;36", "1;33", "1;31", "33", "46;30"
	};
	char b[32];
	int cls = (attr >> 3) & 7, col = scr_color && cls > 0 && cls < 7;

	snprintf(b, sizeof(b), "\033[0%s%s%s%s%sm",
	    (attr & A_BOLD) ? ";1" : "", (attr & A_UL) ? ";4" : "",
	    (attr & A_REV) && !(col && (cls == C_SEL || cls == C_BAR)) ?
	    ";7" : "", col ? ";" : "", col ? fg[cls] : "");
	ostr(b);
}

void
scr_flush(int crow, int ccol)
{
	static const char acs[] = " \140qxlkmj";
	char b[40];
	int r, c, attr = -1, prow = -1, pcol = -1, alt = 0;

	ostr("\033[?25l");
	for (r = 0; r < scr_rows; r++) {
		for (c = 0; c < scr_cols; c++) {
			uint16_t v = nxt[r][c];
			int ch = v & 0xff, a = v >> 8;

			if (v == cur[r][c])
				continue;
			if (r != prow || c != pcol) {
				snprintf(b, sizeof(b), "\033[%d;%dH", r + 1, c + 1);
				ostr(b);
			}
			if (a != attr) {
				sgr(a);
				attr = a;
			}
			if (ch < 0x20) {
				if (!alt)
					ostr("\033(0");
				alt = 1;
				ch = acs[ch & 7];
			} else if (alt) {
				ostr("\033(B");
				alt = 0;
			}
			b[0] = (char)ch;
			oput(b, 1);
			cur[r][c] = v;
			prow = r;
			pcol = c + 1;
		}
	}
	if (alt)
		ostr("\033(B");
	ostr("\033[0m");
	snprintf(b, sizeof(b), "\033[%d;%dH\033[?25h", crow + 1, ccol + 1);
	ostr(b);
	oflush();
}
