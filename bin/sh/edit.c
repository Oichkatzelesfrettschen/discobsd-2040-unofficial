/*
 * Interactive line editor for bin/sh.
 *
 * Engaged only for an interactive top-level command line (see the
 * isatty() gate in main.c); script and pipe input never reach this
 * file.  Everything here is static-buffer and integer-only: a 128
 * byte edit buffer, a 32-entry x 96-byte history ring (3 KB), and a
 * completion scan that stores at most one candidate name at a time.
 * No malloc beyond what opendir(3) itself performs.
 *
 * The module takes fdin/fdout/prompt/path as parameters and touches
 * no bin/sh global state, so the identical source builds standalone
 * for the host smoke test in tests/edit_test.c (HOSTBUILD).
 */
#ifdef HOSTBUILD
#include <termios.h>
#include <sys/ioctl.h>
#else
#include <sgtty.h>
#include <sys/ioctl.h>
#endif

#include <sys/types.h>
#include <sys/dir.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "edit.h"

#define ED_MAX      126     /* leaves room in a 128-byte fbuf for the NL */
#define ED_NAMEMAX  63      /* longest completion candidate name kept */
#define HIST_LINES  32
#define HIST_COLS   (ED_MAX + 1) /* long enough for any editable line:
                                   * 32 * 127 = 4064 bytes of ring */

/* ---- history ring ---- */

static char hist[HIST_LINES][HIST_COLS];
static int  hist_count = 0;
static int  hist_next  = 0;

static void
hist_push(s, n)
	char *s;
	int n;
{
	int last;

	if (n <= 0)
		return;
	if (n >= HIST_COLS)
		n = HIST_COLS - 1;
	if (hist_count > 0) {
		last = (hist_next - 1 + HIST_LINES) % HIST_LINES;
		if ((int)strlen(hist[last]) == n && memcmp(hist[last], s, n) == 0)
			return;
	}
	memcpy(hist[hist_next], s, n);
	hist[hist_next][n] = 0;
	hist_next = (hist_next + 1) % HIST_LINES;
	if (hist_count < HIST_LINES)
		hist_count++;
}

/* hpos == 0 is the most recently pushed line, hpos == hist_count-1 the oldest */
static int
hist_load(hpos, buf, max)
	int hpos;
	char *buf;
	int max;
{
	int idx, n;

	if (hpos < 0 || hpos >= hist_count)
		return 0;
	idx = (hist_next - 1 - hpos + 2 * HIST_LINES) % HIST_LINES;
	n = strlen(hist[idx]);
	if (n > max)
		n = max;
	memcpy(buf, hist[idx], n);
	return n;
}

/* ---- raw terminal mode ---- */

static int have_saved = 0;
#ifdef HOSTBUILD
static struct termios saved_tio;

static int
tty_raw(fd)
	int fd;
{
	struct termios t;

	if (!isatty(fd))
		return 0;
	if (!have_saved) {
		if (tcgetattr(fd, &saved_tio) < 0)
			return 0;
		have_saved = 1;
	}
	t = saved_tio;
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &t);
	return 1;
}

static void
tty_cooked(fd)
	int fd;
{
	if (have_saved)
		tcsetattr(fd, TCSANOW, &saved_tio);
}
#else
static struct sgttyb saved_sg;

/*
 * CBREAK, not RAW: RAW disables the kernel's interrupt-character
 * scan entirely (sys/kern/tty.c ttyinput()), which would silence
 * Ctrl-C at the prompt.  CBREAK only turns off canonical (line
 * erase/kill) processing and, with ECHO cleared below, our own
 * redraw replaces the kernel's echo -- SIGINT still reaches
 * edit_onintr() the normal way.
 *
 * TIOCSETN, not TIOCSETP, in both directions: TIOCSETP flushes the
 * input queue (sys/kern/tty.c), which threw away a command typed
 * ahead of the prompt, during the motd or a slow command.  TIOCSETN
 * keeps it: going into CBREAK the kernel moves the pending canonical
 * line into the raw queue the editor reads, and coming back it marks
 * the queue PENDIN for canonical reprocessing.
 */
static int
tty_raw(fd)
	int fd;
{
	struct sgttyb sg;

	if (!isatty(fd))
		return 0;
	if (!have_saved) {
		if (ioctl(fd, TIOCGETP, &saved_sg) < 0)
			return 0;
		have_saved = 1;
	}
	sg = saved_sg;
	sg.sg_flags |= CBREAK;
	sg.sg_flags &= ~(ECHO | CRMOD | RAW);
	ioctl(fd, TIOCSETN, &sg);
	return 1;
}

static void
tty_cooked(fd)
	int fd;
{
	if (have_saved)
		ioctl(fd, TIOCSETN, &saved_sg);
}
#endif

/*
 * True if at least one byte is already queued on fd.  Used only
 * after a bare ESC: sgtty CBREAK gives no VMIN/VTIME equivalent, so
 * without this a standalone Esc keypress blocks on a second read()
 * and then silently eats whatever the typist presses next (see
 * sys/arch/rp2040/doc/research/menu-shell.md's read_key()).  Once an
 * escape sequence is confirmed underway (an initial '[' has been
 * seen), the remaining one or two bytes are assumed to arrive in the
 * same burst and are read with an ordinary blocking read().
 */
static int
bytes_pending(fd)
	int fd;
{
	/*
	 * FIONREAD's argument is a device `long` (sys/sys/ioctl.h:
	 * `_IOR('f', 97, long)`) but an `int` under glibc; on a
	 * 64-bit host, handing ioctl(2) a `long *` when the kernel
	 * only writes 4 bytes leaves the upper bytes of `n' as stack
	 * garbage, and n>0 goes essentially random.
	 */
#ifdef HOSTBUILD
	int n;
#else
	long n;
#endif

	n = 0;
	if (ioctl(fd, FIONREAD, &n) < 0)
		return 0;
	return n > 0;
}

/* ---- SIGINT during editing: clear the line, do not unwind the shell ---- */

static volatile int got_intr;

static void
edit_onintr(sig)
	int sig;
{
	(void)sig;
	signal(SIGINT, edit_onintr); /* reinstall: this port's signal() is not BSD-reliable */
	got_intr = 1;
}

/*
 * SIGHUP (carrier drop on the USB CDC-ACM console) and SIGTERM both
 * have shell-level handling installed by stdsigs() before editline()
 * ever runs (fault.c's sigval[]: SIGHUP -> done(), an immediate
 * exit; SIGTERM -> fault(), which only marks trapnote for the next
 * sigchk()).  Neither of those handlers knows about our raw tty
 * mode, so a HUP or TERM that arrives mid-edit must not run them
 * directly -- it would exit (or, for TERM, eventually unwind through
 * sigchk() at some later point) with the tty still in CBREAK/no-echo.
 * We catch both here, restore the tty and the real dispositions
 * ourselves, then re-deliver the signal via kill(getpid(), sig) so
 * the shell's own handler runs exactly as it would have -- with a
 * cooked tty underneath it.  See the got_fatal check in editline().
 */
static volatile int got_fatal;

static void
edit_onfatal(sig)
	int sig;
{
	signal(sig, edit_onfatal);
	got_fatal = sig;
}

/* ---- small output helpers (no stdio) ---- */

static void
ed_write(fd, s, n)
	int fd;
	const char *s;
	int n;
{
	if (n > 0)
		write(fd, s, n);
}

static void
ed_puts(fd, s)
	int fd;
	const char *s;
{
	ed_write(fd, s, strlen(s));
}

/* small unsigned-int to decimal, returns length written into out (no NUL) */
static int
ed_itoa(v, out)
	int v;
	char *out;
{
	char tmp[6];
	int i, n;

	if (v <= 0) {
		out[0] = '0';
		return 1;
	}
	i = 0;
	while (v > 0 && i < (int)sizeof(tmp)) {
		tmp[i++] = (char)('0' + (v % 10));
		v /= 10;
	}
	n = i;
	while (i > 0) {
		i--;
		out[n - 1 - i] = tmp[i];
	}
	return n;
}

static void
redraw(fdout, prompt, buf, len, cur)
	int fdout;
	const char *prompt;
	char *buf;
	int len, cur;
{
	char num[6];
	int n, back;

	ed_write(fdout, "\r", 1);
	ed_puts(fdout, prompt);
	ed_write(fdout, buf, len);
	ed_puts(fdout, "\033[K");
	back = len - cur;
	if (back > 0) {
		ed_puts(fdout, "\033[");
		n = ed_itoa(back, num);
		ed_write(fdout, num, n);
		ed_write(fdout, "D", 1);
	}
}

/* ---- editing primitives on the static work buffer ---- */

/*
 * Inserts s[0..n) at *pcur, clamped to `max' total buffer bytes.
 * `fdout' is where a BEL goes if the insert had to be clamped -- a
 * line at the ED_MAX cap drops further keystrokes rather than
 * silently discarding them with no signal to the typist.
 */
static void
insert_text(fdout, buf, plen, pcur, max, s, n)
	int fdout;
	char *buf;
	int *plen, *pcur, max;
	const char *s;
	int n;
{
	int room, i;

	room = max - *plen;
	if (n > room) {
		n = room;
		ed_write(fdout, "\007", 1);
	}
	if (n <= 0)
		return;
	for (i = *plen; i > *pcur; i--)
		buf[i + n - 1] = buf[i - 1];
	for (i = 0; i < n; i++)
		buf[*pcur + i] = s[i];
	*plen += n;
	*pcur += n;
}

static void
delete_back(buf, plen, pcur)
	char *buf;
	int *plen, *pcur;
{
	int i;

	if (*pcur <= 0)
		return;
	for (i = *pcur - 1; i < *plen - 1; i++)
		buf[i] = buf[i + 1];
	(*plen)--;
	(*pcur)--;
}

static void
delete_fwd(buf, plen, pcur)
	char *buf;
	int *plen, *pcur;
{
	int i;

	if (*pcur >= *plen)
		return;
	for (i = *pcur; i < *plen - 1; i++)
		buf[i] = buf[i + 1];
	(*plen)--;
}

/* ---- tab completion ---- */

struct cstate {
	int  mode;               /* 0 = count/common-prefix, 1 = list */
	int  prefixlen;
	int  count;
	int  common_len;
	char sole[ED_NAMEMAX + 1];
	int  fdout;
	int  col;
};

static void
complete_cb(name, vctx)
	char *name;
	void *vctx;
{
	struct cstate *st = (struct cstate *)vctx;
	int l, k;

	if (st->mode == 0) {
		st->count++;
		l = strlen(name);
		if (l > ED_NAMEMAX)
			l = ED_NAMEMAX;
		if (st->count == 1) {
			memcpy(st->sole, name, l);
			st->sole[l] = 0;
			st->common_len = l;
		} else {
			k = 0;
			while (k < st->common_len && k < l && name[k] == st->sole[k])
				k++;
			st->common_len = k;
		}
	} else {
		l = strlen(name);
		if (st->col + l + 1 >= 80) {
			ed_puts(st->fdout, "\r\n");
			st->col = 0;
		}
		ed_write(st->fdout, name, l);
		ed_write(st->fdout, " ", 1);
		st->col += l + 1;
	}
}

/* Scan one directory (or, if is_pathlist, each ':'-separated PATH
 * component) for entries whose name has the given prefix, calling
 * cb(name, ctx) for each.  Dotfiles are hidden unless the prefix
 * itself starts with '.'.  Bounded: one DIRBLKSIZ read buffer inside
 * opendir(3), reused per directory, nothing accumulated here. */
static void
scan_matches(dirlist, is_pathlist, prefix, prefixlen, cb, ctx)
	char *dirlist;
	int is_pathlist;
	const char *prefix;
	int prefixlen;
	void (*cb)(char *, void *);
	void *ctx;
{
	char pbuf[64];
	char *p, *colon;
	int dl, more;
	DIR *dp;
	struct direct *de;

	p = dirlist;
	do {
		if (!is_pathlist) {
			dl = strlen(dirlist);
			more = 0;
		} else {
			colon = strchr(p, ':');
			dl = colon ? (int)(colon - p) : (int)strlen(p);
			more = colon != (char *)0;
		}
		if (dl == 0) {
			pbuf[0] = '.';
			pbuf[1] = 0;
		} else {
			if (dl >= (int)sizeof(pbuf))
				dl = (int)sizeof(pbuf) - 1;
			memcpy(pbuf, p, dl);
			pbuf[dl] = 0;
		}

		dp = opendir(pbuf);
		if (dp) {
			while ((de = readdir(dp)) != (struct direct *)0) {
				if (de->d_name[0] == '.' &&
				    (de->d_name[1] == 0 ||
				     (de->d_name[1] == '.' && de->d_name[2] == 0)))
					continue;
				if (prefixlen > 0) {
					if (strncmp(de->d_name, prefix, prefixlen) != 0)
						continue;
				} else if (de->d_name[0] == '.') {
					continue;
				}
				cb(de->d_name, ctx);
			}
			closedir(dp);
		}

		if (is_pathlist && more)
			p = colon + 1;
	} while (is_pathlist && more);
}

static void
complete(fdout, path, buf, plen, pcur, max)
	int fdout;
	const char *path;
	char *buf;
	int *plen, *pcur, max;
{
	int wstart, i, slash, is_first, is_pathlist;
	char dirbuf[64];
	const char *prefix;
	int prefixlen;
	struct cstate st;

	/* find start of the word ending at the cursor */
	wstart = *pcur;
	while (wstart > 0 && buf[wstart - 1] != ' ' && buf[wstart - 1] != '\t')
		wstart--;

	is_first = 1;
	for (i = 0; i < wstart; i++)
		if (buf[i] != ' ' && buf[i] != '\t') {
			is_first = 0;
			break;
		}

	slash = -1;
	for (i = wstart; i < *pcur; i++)
		if (buf[i] == '/')
			slash = i;

	if (slash < 0) {
		prefix = buf + wstart;
		prefixlen = *pcur - wstart;
		is_pathlist = is_first;
		if (!is_pathlist) {
			dirbuf[0] = '.';
			dirbuf[1] = 0;
		}
	} else {
		int dl = slash - wstart + 1; /* include the slash */

		if (dl >= (int)sizeof(dirbuf))
			dl = (int)sizeof(dirbuf) - 1;
		memcpy(dirbuf, buf + wstart, dl);
		dirbuf[dl] = 0;
		prefix = buf + slash + 1;
		prefixlen = *pcur - (slash + 1);
		is_pathlist = 0;
	}

	st.mode = 0;
	st.prefixlen = prefixlen;
	st.count = 0;
	st.common_len = 0;
	st.fdout = fdout;
	st.col = 0;

	if (is_pathlist)
		scan_matches((char *)path, 1, prefix, prefixlen, complete_cb, &st);
	else
		scan_matches(dirbuf, 0, prefix, prefixlen, complete_cb, &st);

	if (st.count == 0) {
		ed_write(fdout, "\007", 1);
	} else if (st.count == 1 || st.common_len > prefixlen) {
		/* caller redraws after complete() returns */
		insert_text(fdout, buf, plen, pcur, max,
		    st.sole + prefixlen, st.common_len - prefixlen);
	} else {
		ed_puts(fdout, "\r\n");
		st.mode = 1;
		st.col = 0;
		if (is_pathlist)
			scan_matches((char *)path, 1, prefix, prefixlen, complete_cb, &st);
		else
			scan_matches(dirbuf, 0, prefix, prefixlen, complete_cb, &st);
		ed_puts(fdout, "\r\n");
	}
}

/* ---- main entry point ---- */

int
editline(fdin, fdout, prompt, path, buf, bufsz)
	int fdin, fdout;
	const char *prompt, *path;
	char *buf;
	int bufsz;
{
	char work[ED_MAX + 1];
	int len, cur, max, hpos, raw, n, done;
	unsigned char c;
	void (*old_intr)(int);
	void (*old_hup)(int);
	void (*old_term)(int);

	len = 0;
	cur = 0;
	hpos = -1;
	done = 0;

	max = bufsz - 1;
	if (max > ED_MAX)
		max = ED_MAX;
	if (max < 0)
		max = 0;

	raw = tty_raw(fdin);
	got_intr = 0;
	got_fatal = 0;
	old_intr = signal(SIGINT, edit_onintr);
	old_hup  = signal(SIGHUP, edit_onfatal);
	old_term = signal(SIGTERM, edit_onfatal);

	redraw(fdout, prompt, work, len, cur);

	while (!done) {
		n = read(fdin, &c, 1);

		if (got_fatal) {
			int sig = got_fatal;

			got_fatal = 0;
			signal(SIGINT, old_intr);
			signal(SIGHUP, old_hup);
			signal(SIGTERM, old_term);
			if (raw)
				tty_cooked(fdin);

			kill(getpid(), sig);

			/*
			 * Only reached if the shell's own handler for
			 * `sig' returned instead of exiting -- an
			 * untrapped SIGTERM's fault() only marks
			 * trapnote for the next sigchk().  Resume
			 * editing exactly as a fresh call would.
			 */
			raw = tty_raw(fdin);
			got_intr = 0;
			got_fatal = 0;
			old_intr = signal(SIGINT, edit_onintr);
			old_hup  = signal(SIGHUP, edit_onfatal);
			old_term = signal(SIGTERM, edit_onfatal);
			ed_puts(fdout, "\r\n");
			redraw(fdout, prompt, work, len, cur);
			continue;
		}

		if (got_intr) {
			got_intr = 0;
			len = 0;
			cur = 0;
			hpos = -1;
			ed_puts(fdout, "\r\n");
			redraw(fdout, prompt, work, len, cur);
			continue;
		}

		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			len = -1; /* fd EOF/error: treat like Ctrl-D */
			break;
		}

		if (c == '\r' || c == '\n') {
			ed_puts(fdout, "\r\n");
			break;
		} else if (c == 004) {                    /* Ctrl-D */
			if (len == 0) {
				len = -1;
				break;
			}
		} else if (c == 010 || c == 0177) {        /* BS / DEL */
			delete_back(work, &len, &cur);
			redraw(fdout, prompt, work, len, cur);
		} else if (c == 001) {                     /* Ctrl-A: home */
			cur = 0;
			redraw(fdout, prompt, work, len, cur);
		} else if (c == 005) {                     /* Ctrl-E: end */
			cur = len;
			redraw(fdout, prompt, work, len, cur);
		} else if (c == 011) {                     /* Tab */
			complete(fdout, path, work, &len, &cur, max);
			redraw(fdout, prompt, work, len, cur);
		} else if (c == 033) {                     /* ESC [ ... */
			unsigned char c2, c3;

			if (!bytes_pending(fdin))
				continue;       /* standalone ESC: no-op, next byte is a fresh keystroke */
			if (read(fdin, &c2, 1) != 1 || c2 != '[')
				continue;
			if (read(fdin, &c3, 1) != 1)
				continue;
			if (c3 >= '0' && c3 <= '9') {
				unsigned char c4;
				int tilde_code = c3;

				if (read(fdin, &c4, 1) != 1)
					continue;
				while (c4 >= '0' && c4 <= '9') {
					if (read(fdin, &c4, 1) != 1)
						break;
				}
				if (tilde_code == '3')
					delete_fwd(work, &len, &cur);
				else if (tilde_code == '1' || tilde_code == '7')
					cur = 0;
				else if (tilde_code == '4' || tilde_code == '8')
					cur = len;
				redraw(fdout, prompt, work, len, cur);
				continue;
			}
			switch (c3) {
			case 'A':                       /* Up: history back */
				if (hpos + 1 < hist_count) {
					hpos++;
					len = hist_load(hpos, work, max);
					cur = len;
					redraw(fdout, prompt, work, len, cur);
				}
				break;
			case 'B':                       /* Down: history fwd */
				if (hpos >= 0) {
					hpos--;
					if (hpos < 0) {
						len = 0;
						cur = 0;
					} else {
						len = hist_load(hpos, work, max);
						cur = len;
					}
					redraw(fdout, prompt, work, len, cur);
				}
				break;
			case 'C':                       /* Right */
				if (cur < len) {
					cur++;
					redraw(fdout, prompt, work, len, cur);
				}
				break;
			case 'D':                       /* Left */
				if (cur > 0) {
					cur--;
					redraw(fdout, prompt, work, len, cur);
				}
				break;
			case 'H':                       /* Home (xterm) */
				cur = 0;
				redraw(fdout, prompt, work, len, cur);
				break;
			case 'F':                       /* End (xterm) */
				cur = len;
				redraw(fdout, prompt, work, len, cur);
				break;
			default:
				break;
			}
		} else if (c >= 040 && c < 0177) {         /* printable */
			insert_text(fdout, work, &len, &cur, max, (char *)&c, 1);
			redraw(fdout, prompt, work, len, cur);
		}
		/* other control characters: ignored */
	}

	signal(SIGINT, old_intr);
	signal(SIGHUP, old_hup);
	signal(SIGTERM, old_term);
	if (raw)
		tty_cooked(fdin);

	if (len < 0)
		return -1;

	hist_push(work, len);

	n = len;
	if (n > bufsz)
		n = bufsz;
	memcpy(buf, work, n);
	return n;
}
