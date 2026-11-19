/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The POSIX side of the uLisp system interface (platform.h): console,
 * files, clock, interrupts and the workspace allocation. It uses file
 * descriptors alone, so the program links no stdio.
 */
#include <sys/types.h>
#include <sys/time.h>
#ifdef ULISP_BOARD
#include <sys/dir.h>		/* 4.3BSD names the entry struct direct. */
#define dirent	direct
#else
#include <dirent.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#ifdef ULISP_SGTTY
#include <sgtty.h>
#else
#include <termios.h>
#endif

#include "platform.h"

#ifndef ULISP_SHARE
#define ULISP_SHARE	"/usr/share/ulisp"
#endif

/*
 * Bytes of C stack kept clear of the workspace on DiscoBSD, where the
 * break and the stack share one window, and the part of them eval() leaves
 * unused as it recurses: the garbage collector, the printer and the system
 * calls run below the deepest eval() frame.
 */
#define STACK_RESERVE	(24 * 1024)
#define STACK_MARGIN	(3 * 1024)
#define HOST_CELLS	22280		/* The workspace of uLisp on a Pico. */
#define HOST_STACK	(512 * 1024)

#define IOBUF	96

struct sysfile {
	int	fd;
	short	pos, len;	/* Read: next and end. Write: len bytes held. */
	bool	writing, failed;
	char	buf[IOBUF];
};

static struct sysfile files[SYS_NFILES];
static struct sysfile conout = { .fd = 1, .writing = true };
static struct sysfile conin;
static volatile sig_atomic_t interrupted;
static bool interactive;
static bool hlok, hlon, nohl;	/* Colors allowed here; the editor reads now; -n */
static int outcol;		/* Characters written since the last line end */
static struct hl_state hlst;	/* What the lines read so far left open */
static struct timeval started;
static unsigned int tone;
static uint32_t seed;
#ifdef ULISP_BOARD
/*
 * opendir() takes its stream from malloc(), which would move the break past
 * the workspace into the span kept for the stack. readdir() needs only the
 * descriptor and the block buffer, so the stream is static here.
 */
static DIR dirstream;
#endif
static DIR *dir;

static bool
fileflush(struct sysfile *f)
{
	int done = 0;

	while (done < f->len) {
		ssize_t n = write(f->fd, f->buf + done, (size_t)(f->len - done));

		if (n <= 0) {
			f->failed = true;
			break;
		}
		done += (int)n;
	}
	f->len = 0;
	return !f->failed;
}

static void
fileputc(struct sysfile *f, char c)
{
	if (f->len == IOBUF)
		(void)fileflush(f);
	f->buf[f->len++] = c;
}

static int
filegetc(struct sysfile *f)
{
	if (f->pos == f->len) {
		ssize_t n = read(f->fd, f->buf, IOBUF);

		if (n <= 0)
			return -1;
		f->pos = 0;
		f->len = (short)n;
	}
	return (unsigned char)f->buf[f->pos++];
}

static void
onsignal(int sig)
{
	if (sig == SIGINT)
		interrupted = 1;
	else
		ulisp_fail("memory fault");
}

static void
catch(int sig)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = onsignal;
#ifdef SA_NODEFER
	/* ulisp_fail() leaves the handler by longjmp(). */
	sa.sa_flags = SA_NODEFER;
#endif
	(void)sigaction(sig, &sa, NULL);
}

void *
sys_init(unsigned int *cells, size_t cellsize, uintptr_t *stacklimit)
{
	char here;
	void *workspace;

	for (int i = 0; i < SYS_NFILES; i++)
		files[i].fd = -1;
	interactive = isatty(0) != 0;
	{
		const char *term = getenv("TERM");

		hlok = interactive && !nohl && isatty(1) && term != NULL &&
		    strcmp(term, "dumb") != 0;
	}
	(void)gettimeofday(&started, NULL);
	seed = (uint32_t)started.tv_usec * 2654435761u + (uint32_t)getpid();
	catch(SIGINT);
	catch(SIGSEGV);
	catch(SIGBUS);
	catch(SIGILL);
#ifdef ULISP_BOARD
	{
		uintptr_t now = (uintptr_t)sbrk(0);
		uintptr_t low = (now + 7) & ~(uintptr_t)7;
		uintptr_t high = (uintptr_t)&here;
		size_t most;

		if (high < low + STACK_RESERVE + cellsize)
			return NULL;
		most = (high - low - STACK_RESERVE) / cellsize;
		if (*cells == 0 || *cells > most)
			*cells = (unsigned int)most;
		/* sbrk() reports failure as -1; this libc's brk() returns the
		 * new break on success, not zero. */
		if (sbrk((int)(low + *cells * cellsize - now)) == (void *)-1)
			return NULL;
		workspace = (void *)low;
		*stacklimit = low + *cells * cellsize + STACK_MARGIN;
	}
#else
	if (*cells == 0)
		*cells = HOST_CELLS;
	workspace = malloc(*cells * cellsize);
	*stacklimit = (uintptr_t)&here - HOST_STACK;
#endif
	return workspace;
}

void
sys_exit(int status)
{
	sys_flush();
	for (int i = 0; i < SYS_NFILES; i++)
		(void)sys_fclose(i);
	dev_release();
	_exit(status);
}

void
sys_putc(char c)
{
	fileputc(&conout, c);
	outcol = (c == '\n' || c == '\r') ? 0 : outcol + 1;
	if (c == '\n')
		sys_flush();
}

void
sys_flush(void)
{
	(void)fileflush(&conout);
	conout.failed = false;
}

/*
 * The line editor behind the colors: with the terminal in unechoed character
 * mode it takes a line into conin.buf, prints it with the SGR color of every
 * character's class (hilite.c), and on each key repaints what a key changed.
 * Its keys are the printable ones, backspace and DEL, ^U (kill the line),
 * ^D at an empty line (end of input), and Return; an escape sequence, such
 * as an arrow key, is dropped and ^C discards the line. A line that fills
 * the buffer is handed over whole and the next one continues it.
 */
#ifdef ULISP_SGTTY
static struct sgttyb savedtty;
static bool ttykeep;		/* TIOCSETN, which keeps typed-ahead input */
#else
static struct termios savedtty;
#endif

/*
 * Puts the terminal into unechoed character mode, with the saved state to
 * return to; false when it is not a terminal. Only keep is a choice, and
 * only for sgtty: TIOCSETP discards what was typed ahead of the call.
 */
static bool
ttyraw(bool keep)
{
#ifdef ULISP_SGTTY
	struct sgttyb raw;

	if (ioctl(0, TIOCGETP, &savedtty) != 0)
		return false;
	raw = savedtty;
	raw.sg_flags |= CBREAK;
	raw.sg_flags &= ~ECHO;
	ttykeep = keep;
	(void)ioctl(0, keep ? TIOCSETN : TIOCSETP, &raw);
#else
	struct termios raw;

	(void)keep;
	if (tcgetattr(0, &savedtty) != 0)
		return false;
	raw = savedtty;
	raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	(void)tcsetattr(0, TCSANOW, &raw);
#endif
	return true;
}

static void
ttyrestore(void)
{
#ifdef ULISP_SGTTY
	(void)ioctl(0, ttykeep ? TIOCSETN : TIOCSETP, &savedtty);
#else
	(void)tcsetattr(0, TCSANOW, &savedtty);
#endif
}

static void
putstr(const char *s)
{
	while (*s != '\0')
		fileputc(&conout, *s++);
}

static void
putnum(int v)
{
	if (v >= 10)
		putnum(v / 10);
	fileputc(&conout, (char)('0' + v % 10));
}

/* The SGR attributes of each class; bit 7 of a class asks for reverse video. */
static void
setclass(int k)
{
	static const uint8_t sgr[] = { 0, 1, 32, 90, 35, 93, 93, 95, 94, 36, 33, 41 };

	putstr("\033[0");
	if (k & 0x80)
		putstr(";7");
	if (sgr[k & 0x7f] != 0) {
		fileputc(&conout, ';');
		putnum(sgr[k & 0x7f]);
	}
	fileputc(&conout, 'm');
}

static void
paint(const char *s, const uint8_t *cls, int from, int n)
{
	int now = HL_PLAIN;

	for (int i = from; i < n; i++) {
		if (cls[i] != now)
			setclass(now = cls[i]);
		fileputc(&conout, s[i]);
	}
	if (now != HL_PLAIN)
		setclass(HL_PLAIN);
}

/* True when a line was read into conin.buf; false at end of input. */
static bool
editline(void)
{
	uint8_t cls[IOBUF], shown[IOBUF];
	struct hl_state first = hlst;
	struct winsize ws;
	char *line = conin.buf;
	int n = 0, shownn = 0, plen = outcol, esc = 0, width = 80;
	bool eof = false;
	unsigned char key;

	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
		width = ws.ws_col;
	for (;;) {
		if (read(0, &key, 1) != 1) {
			if (errno != EINTR) {
				eof = true;
				break;
			}
			n = 0;			/* An interrupt discards the line. */
			esc = 0;
		} else if (esc != 0) {
			esc = (esc == 1 && (key == '[' || key == 'O')) ? 2 :
			    (esc == 2 && (key < 0x40 || key > 0x7e)) ? 2 : 0;
			continue;
		} else if (key == 033) {
			esc = 1;
			continue;
		} else if (key == '\r' || key == '\n') {
			break;
		} else if (key == 4) {
			if (n > 0)
				continue;
			eof = true;
			break;
		} else if (key == 8 || key == 0x7f) {
			if (n == 0)
				continue;
			n--;
		} else if (key == 21) {
			n = 0;
		} else if (key >= ' ' && key < 0x7f) {
			line[n++] = (char)key;
		} else
			continue;
		hlst = first;
		hl_scan(&hlst, line, n, cls, ulisp_symclass);
		/* A closing parenthesis shows the one it closes. */
		if (n > 0 && line[n - 1] == ')' && cls[n - 1] == HL_PAREN) {
			int depth = 0;

			for (int m = n - 1; m >= 0; m--)
				if (cls[m] == HL_PAREN &&
				    (depth += (line[m] == ')') ? 1 : -1) == 0) {
					cls[m] |= 0x80;
					break;
				}
		}
		if (n != shownn + 1 || memcmp(cls, shown, (size_t)shownn) != 0) {
			/* Back to the first character and clear from there. */
			if (shownn > 0) {
				int up = (plen + shownn - 1) / width - plen / width;

				if (up > 0) {
					putstr("\033[");
					putnum(up);
					fileputc(&conout, 'A');
				}
				putstr("\033[");
				putnum(plen % width + 1);
				putstr("G\033[J");
			}
			shownn = 0;
		}
		paint(line, cls, shownn, n);
		memcpy(shown, cls, (size_t)n);
		shownn = n;
		sys_flush();
		if (n >= IOBUF - 2) {
			outcol = plen + n;
			ttyrestore();
			conin.pos = 0;
			conin.len = (short)n;
			return true;
		}
	}
	ttyrestore();
	if (eof)
		return false;
	line[n++] = '\n';		/* Plain: hlst is already what the line leaves open. */
	fileputc(&conout, '\n');
	sys_flush();
	outcol = 0;
	conin.pos = 0;
	conin.len = (short)n;
	return true;
}

void
sys_hilite(bool on)
{
	hlon = on;
	if (on)
		memset(&hlst, 0, sizeof hlst);
}

void
sys_nohilite(void)
{
	nohl = true;
}

int
sys_getc(void)
{
	int c;

	if (conin.pos == conin.len) {
		sys_flush();
		/*
		 * A line typed while the panel shows graphics is the request
		 * to see the console again.
		 */
		if (hlok && hlon && ttyraw(true))
			c = editline() ? filegetc(&conin) : -1;
		else
			c = filegetc(&conin);
		dev_release();
	} else
		c = filegetc(&conin);
	interrupted = 0;
	return c;
}

int
sys_getkey(void)
{
	unsigned char c;
	bool tty;
	ssize_t n;

	if (conin.pos < conin.len)
		return filegetc(&conin);
	sys_flush();
	tty = ttyraw(false);
	n = read(0, &c, 1);
	if (tty)
		ttyrestore();
	return n == 1 ? c : 0;
}

bool
sys_interactive(void)
{
	return interactive;
}

bool
sys_break(void)
{
	if (!interrupted)
		return false;
	interrupted = 0;
	return true;
}

unsigned long
sys_micros(void)
{
	struct timeval now;

	(void)gettimeofday(&now, NULL);
	return (unsigned long)(now.tv_sec - started.tv_sec) * 1000000ul +
	    (unsigned long)now.tv_usec - (unsigned long)started.tv_usec;
}

unsigned long
sys_millis(void)
{
	struct timeval now;

	(void)gettimeofday(&now, NULL);
	return (unsigned long)(now.tv_sec - started.tv_sec) * 1000ul +
	    (unsigned long)(now.tv_usec / 1000) -
	    (unsigned long)(started.tv_usec / 1000);
}

void
sys_tone(unsigned int hz)
{
	tone = hz;
}

/*
 * Waits ms, in slices short enough that an interrupt ends the wait. A tone
 * selected by sys_tone() sounds for the same time: the sound device takes
 * samples at their playing rate, so writing them is the wait.
 */
void
sys_delay(unsigned long ms)
{
	sys_flush();
	while (ms > 0 && !interrupted) {
		unsigned long slice = ms > 20 ? 20 : ms;

		if (tone == 0 || !dev_tone(tone, slice))
			(void)usleep((unsigned int)slice * 1000);
		ms -= slice;
	}
}

uint32_t
sys_random(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

bool
sys_fopen(int file, const char *path, int mode)
{
	static const int flags[] = {
		[SYS_READ] = O_RDONLY,
		[SYS_APPEND] = O_WRONLY | O_CREAT | O_APPEND,
		[SYS_WRITE] = O_WRONLY | O_CREAT | O_TRUNC,
	};
	struct sysfile *f = &files[file];

	(void)sys_fclose(file);
	f->fd = open(path, flags[mode], 0666);
	f->pos = f->len = 0;
	f->writing = mode != SYS_READ;
	f->failed = false;
	return f->fd >= 0;
}

bool
sys_fclose(int file)
{
	struct sysfile *f = &files[file];
	bool ok = true;

	if (f->fd < 0)
		return true;
	if (f->writing)
		ok = fileflush(f);
	if (close(f->fd) != 0)
		ok = false;
	f->fd = -1;
	return ok;
}

int
sys_fgetc(int file)
{
	struct sysfile *f = &files[file];

	return (f->fd < 0 || f->writing) ? -1 : filegetc(f);
}

void
sys_fputc(int file, char c)
{
	struct sysfile *f = &files[file];

	if (f->fd >= 0 && f->writing)
		fileputc(f, c);
}

bool
sys_fseek(int file, size_t offset)
{
	struct sysfile *f = &files[file];

	if (f->fd < 0 || f->writing || lseek(f->fd, (off_t)offset, SEEK_SET) < 0)
		return false;
	f->pos = f->len = 0;
	return true;
}

bool
sys_fread(int file, void *data, size_t bytes)
{
	struct sysfile *f = &files[file];
	char *p = data;

	while (bytes > 0) {
		ssize_t n = read(f->fd, p, bytes);

		if (n <= 0)
			return false;
		p += n;
		bytes -= (size_t)n;
	}
	return true;
}

bool
sys_fwrite(int file, const void *data, size_t bytes)
{
	struct sysfile *f = &files[file];
	const char *p = data;

	while (bytes > 0) {
		ssize_t n = write(f->fd, p, bytes);

		if (n <= 0)
			return false;
		p += n;
		bytes -= (size_t)n;
	}
	return true;
}

static void
dirend(void)
{
	if (dir == NULL)
		return;
#ifdef ULISP_BOARD
	(void)close(dir->dd_fd);
#else
	(void)closedir(dir);
#endif
	dir = NULL;
}

bool
sys_diropen(const char *path)
{
	dirend();
#ifdef ULISP_BOARD
	memset(&dirstream, 0, sizeof dirstream);
	dirstream.dd_fd = open(path, O_RDONLY, 0);
	if (dirstream.dd_fd >= 0)
		dir = &dirstream;
#else
	dir = opendir(path);
#endif
	return dir != NULL;
}

const char *
sys_dirnext(void)
{
	struct dirent *entry;

	while (dir != NULL && (entry = readdir(dir)) != NULL)
		if (entry->d_name[0] != '.')
			return entry->d_name;
	dirend();
	return NULL;
}

const char *
sys_library(void)
{
	const char *path = getenv("ULISPLIB");

	return path != NULL ? path : ULISP_SHARE "/library.lisp";
}

/*
 * The documentation file (mkdoc.c writes it) is a count of entries, one
 * more offset than entries, and the text, with every number two bytes, low
 * byte first; entry i is the text from offset i to offset i + 1.
 */
int
sys_docopen(unsigned int index)
{
	const char *path = getenv("ULISPDOC");
	struct sysfile *f = &files[SYS_FTMP];
	unsigned char b[4];
	unsigned int count, from, to;

	if (!sys_fopen(SYS_FTMP, path != NULL ? path : ULISP_SHARE "/ulisp.doc",
	    SYS_READ))
		return -1;
	if (!sys_fread(SYS_FTMP, b, 2))
		goto bad;
	count = b[0] | (unsigned int)b[1] << 8;
	if (index >= count ||
	    lseek(f->fd, (off_t)(2 + 2 * index), SEEK_SET) < 0 ||
	    !sys_fread(SYS_FTMP, b, 4))
		goto bad;
	from = b[0] | (unsigned int)b[1] << 8;
	to = b[2] | (unsigned int)b[3] << 8;
	if (to < from ||
	    lseek(f->fd, (off_t)(2 + 2 * (count + 1) + from), SEEK_SET) < 0)
		goto bad;
	return (int)(to - from);
bad:
	(void)sys_fclose(SYS_FTMP);
	return -1;
}
