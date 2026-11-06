/*
 * Timed terminal loop shared by the games that wait on a clock as well as
 * on the keyboard (games/coil, games/puzzlebox). It owns the raw tty, the
 * buffered output, the escape-sequence parser, the wait for input or the
 * next deadline, and the exit paths; the game supplies three callbacks.
 *
 * gt_loop() waits in select for input, the game's next deadline, or a
 * stalled escape sequence, then calls app->event() with the clock reading,
 * the length of a wait that expired without input, and the key (0 for
 * none). A true return from event() leaves the loop. The loop itself acts
 * on two keys: q ends the game and Ctrl-L repaints. It leaves through
 * gt_leave() on q, end of input, a select failure or a signal.
 *
 * On the target the sgtty RAW flag delivers ^C as the byte 3, which no key
 * acts on, so q is the keyboard quit; SIGINT, SIGTERM, SIGHUP and SIGQUIT
 * come from kill(1) or a hangup and end the game the same way. SIGTSTP
 * restores the tty from its handler before the stop, and the loop
 * re-enters raw mode and repaints after SIGCONT. A host termios keeps ISIG
 * on, so ^C and ^\ raise the same signals.
 *
 * Arrow keys come back as GK_UP, GK_DOWN, GK_LEFT and GK_RIGHT (the codes
 * of gametty.h); a bare Esc, after GT_ESC_MS of silence, and the F1
 * sequences come back as GT_ESC; any other finished sequence is dropped.
 *
 * Including this header defines static functions and variables for one
 * game's translation unit; the including game defines them once.
 */
#ifndef GAMES_GAMETERM_H
#define GAMES_GAMETERM_H

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "gametty.h"

#define GT_ESC		033
#define GT_REPAINT	014
#define GT_OBUF		2048

struct gt_app {
	/* Milliseconds until the game's next timed action, or -1 for none. */
	int (*deadline)(void);
	/* Handle a wake-up; returns nonzero to leave the loop. */
	int (*event)(int key, unsigned long now, int expired_ms);
	/* Bring the screen in line with the game; full repaints everything. */
	void (*frame)(int full);
};

static char gt_obuf[GT_OBUF];
static int gt_olen;

/*
 * write(2) for output whose failure leaves nothing to do: the terminal is
 * gone or an error is already on its way out. The result is taken into a
 * variable because glibc marks write warn_unused_result under
 * _FORTIFY_SOURCE, and a cast to void does not silence that.
 */
static void
gt_put_fd(int fd, const void *p, size_t n)
{
	ssize_t r = write(fd, p, n);

	(void)r;
}

static void
gt_flush(void)
{
	if (gt_olen > 0)
		gt_put_fd(1, gt_obuf, (size_t)gt_olen);
	gt_olen = 0;
}

static void
gt_put(const char *s, int n)
{
	if (gt_olen + n > GT_OBUF)
		gt_flush();
	memcpy(gt_obuf + gt_olen, s, (size_t)n);
	gt_olen += n;
}

static void
gt_str(const char *s)
{
	gt_put(s, (int)strlen(s));
}

static void
gt_num(unsigned long v)
{
	char t[24];
	int n = 0;

	do {
		t[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n > 0)
		gt_put(&t[--n], 1);
}

/* Move to 1-based terminal row and column. */
static void
gt_goto(int row, int col)
{
	gt_str("\033[");
	gt_num((unsigned long)row);
	gt_put(";", 1);
	gt_num((unsigned long)col);
	gt_put("H", 1);
}

static unsigned long
gt_now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, 0);
	return (unsigned long)tv.tv_sec * 1000UL + (unsigned long)tv.tv_usec / 1000UL;
}

/*
 * The longest wait when nothing else bounds it. A signal that lands between
 * the loop's flag test and select cannot interrupt that select; the bound
 * makes the loop see the flag within this many ms. Without pselect in the
 * target libc the window cannot be closed atomically.
 */
#define GT_SIGNAL_POLL_MS	500

/* Wait for input for ms milliseconds, or without limit when ms < 0. */
static int
gt_wait(int ms)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(0, &fds);
	if (ms < 0)
		return select(1, &fds, 0, 0, 0);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000L;
	return select(1, &fds, 0, 0, &tv);
}

static int
gt_pending(void)
{
	long avail = 0;

	return ioctl(0, FIONREAD, &avail) == 0 && avail > 0;
}

#define GT_NONE	0	/* nothing to act on */
#define GT_MORE	(-2)	/* an escape sequence is incomplete */

/*
 * The longest silence inside an escape sequence, in ms. A terminal emits a
 * sequence back to back: the slowest link the console meets, 1200 baud,
 * spaces bytes 8.3 ms apart, and a USB packet carries the whole sequence at
 * once. A human pressing Esc and then another key takes well over 100 ms.
 * 40 ms sits between the two; it is also the delay before a bare Esc acts.
 */
#define GT_ESC_MS	40

/* Longest CSI parameter and intermediate run read before giving up. */
#define GT_CSI_MAX	8

enum { GT_E_IDLE, GT_E_ESC, GT_E_SS3, GT_E_CSI };

static int gt_esc_state = GT_E_IDLE;
static int gt_esc_num;		/* the first numeric parameter of a CSI */
static int gt_esc_len;		/* bytes consumed inside the CSI */
static unsigned long gt_esc_at;	/* clock reading at the last sequence byte */

/*
 * Feed one input byte to the escape parser. Returns the key, GT_NONE for a
 * byte or a finished sequence with no meaning, or GT_MORE while a sequence
 * is incomplete. The parser never reads; the caller supplies bytes as they
 * arrive and ends a stalled sequence with gt_esc_abandon().
 */
static int
gt_feed(int c)
{
	switch (gt_esc_state) {
	case GT_E_IDLE:
		if (c != 033)
			return c;
		gt_esc_state = GT_E_ESC;
		return GT_MORE;
	case GT_E_ESC:
		if (c == 'O') {
			gt_esc_state = GT_E_SS3;
			return GT_MORE;
		}
		if (c == '[') {
			gt_esc_state = GT_E_CSI;
			gt_esc_num = 0;
			gt_esc_len = 0;
			return GT_MORE;
		}
		gt_esc_state = GT_E_IDLE;
		return GT_NONE;
	case GT_E_SS3:
		gt_esc_state = GT_E_IDLE;
		return c == 'P' ? GT_ESC : GT_NONE;
	default:
		break;
	}
	if (c >= '0' && c <= '9') {
		gt_esc_num = gt_esc_num * 10 + (c - '0');
	} else if (c != ';') {
		switch (c) {
		case 'A': gt_esc_state = GT_E_IDLE; return GK_UP;
		case 'B': gt_esc_state = GT_E_IDLE; return GK_DOWN;
		case 'C': gt_esc_state = GT_E_IDLE; return GK_RIGHT;
		case 'D': gt_esc_state = GT_E_IDLE; return GK_LEFT;
		case '~':
			gt_esc_state = GT_E_IDLE;
			return gt_esc_num == 11 ? GT_ESC : GT_NONE;
		default:
			if (c >= 0x40 && c <= 0x7e) {
				gt_esc_state = GT_E_IDLE;
				return GT_NONE;
			}
		}
	}
	if (++gt_esc_len >= GT_CSI_MAX) {
		gt_esc_state = GT_E_IDLE;
		return GT_NONE;
	}
	return GT_MORE;
}

/* End a sequence that stalled for GT_ESC_MS. A lone Esc is a key press. */
static int
gt_esc_abandon(void)
{
	int k = gt_esc_state == GT_E_ESC ? GT_ESC : GT_NONE;

	gt_esc_state = GT_E_IDLE;
	return k;
}

/* Milliseconds left before the pending sequence stalls, at reading now. */
static int
gt_esc_left(unsigned long now)
{
	unsigned long gone = now >= gt_esc_at ? now - gt_esc_at : 0;

	return gone >= GT_ESC_MS ? 0 : GT_ESC_MS - (int)gone;
}

/*
 * Terminal ownership. gt_raw_on is 1 while the tty is in the game's raw
 * mode. Every way out of the loop ends in gt_leave(), which restores the
 * tty, resets the character attributes and clears the screen with the
 * cursor home. The exit-signal handlers only record the signal number; the
 * loop leaves after select returns EINTR.
 *
 * SIGTSTP is the one handler that acts: the stop happens inside the kill()
 * in gt_on_stop(), so the tty must be restored before it, from the handler.
 * tcsetattr is async-signal-safe (POSIX XSH 2.4.3) and the target's ioctl is
 * a direct trap. The handler marks gt_raw_on clear; the loop re-enters raw
 * mode and repaints when it next runs, which is after SIGCONT.
 */
static volatile sig_atomic_t gt_got_sig;
static volatile sig_atomic_t gt_got_cont;
static volatile sig_atomic_t gt_raw_on;

static const char gt_leave_seq[] = "\033[0m\033[2J\033[H";
static const char gt_park_seq[] = "\033[0m\033[31;1H\r\n";

static void
gt_on_exit_signal(int sig)
{
	if (!gt_got_sig)
		gt_got_sig = sig;
}

static void
gt_on_cont(int sig)
{
	(void)sig;
	gt_got_cont = 1;
}

static void
gt_on_stop(int sig)
{
	sigset_t set;

	(void)sig;
	if (gt_raw_on) {
		gtty_restore();
		gt_raw_on = 0;
	}
	gt_put_fd(1, gt_park_seq, sizeof gt_park_seq - 1);
	(void)signal(SIGTSTP, SIG_DFL);
	(void)sigemptyset(&set);
	(void)sigaddset(&set, SIGTSTP);
	(void)sigprocmask(SIG_UNBLOCK, &set, 0);
	(void)kill(getpid(), SIGTSTP);
	(void)signal(SIGTSTP, gt_on_stop);
}

/* Enter raw mode with the handled signals held, so a stop cannot see a
 * half-set tty and gt_raw_on never trails the mode it describes. */
static void
gt_enter(void)
{
	sigset_t all, old;

	(void)sigemptyset(&all);
	(void)sigaddset(&all, SIGINT);
	(void)sigaddset(&all, SIGTERM);
	(void)sigaddset(&all, SIGHUP);
	(void)sigaddset(&all, SIGQUIT);
	(void)sigaddset(&all, SIGTSTP);
	(void)sigprocmask(SIG_BLOCK, &all, &old);
	gtty_raw();
	gt_raw_on = 1;
	(void)sigprocmask(SIG_SETMASK, &old, 0);
}

/* The single exit path of the terminal. */
static void
gt_leave(void)
{
	if (gt_raw_on) {
		gtty_restore();
		gt_raw_on = 0;
	}
	gt_put_fd(1, gt_leave_seq, sizeof gt_leave_seq - 1);
}

/*
 * Run the game until it quits. Returns the process exit status: 0 after q
 * or end of input, 1 after a select failure, 128 plus the signal number
 * after a signal.
 */
static int
gt_loop(const struct gt_app *app)
{
	unsigned long now;
	unsigned char c = 0;
	int k, r, tmo, left, idle, status = 0;
	ssize_t nread;
	const char *err = 0;

	signal(SIGINT, gt_on_exit_signal);
	signal(SIGTERM, gt_on_exit_signal);
	signal(SIGHUP, gt_on_exit_signal);
	signal(SIGQUIT, gt_on_exit_signal);
	signal(SIGTSTP, gt_on_stop);
	signal(SIGCONT, gt_on_cont);
	gt_enter();
	app->frame(1);
	for (;;) {
		if (gt_got_sig)
			break;
		if (!gt_raw_on) {
			gt_enter();	/* resumed after SIGTSTP */
			gt_got_cont = 1;
		}
		if (gt_got_cont) {
			gt_got_cont = 0;
			app->frame(1);
		}
		tmo = app->deadline();
		if (gt_esc_state != GT_E_IDLE) {
			left = gt_esc_left(gt_now_ms());
			if (tmo < 0 || left < tmo)
				tmo = left;
		}
		idle = tmo < 0;
		if (idle)
			tmo = GT_SIGNAL_POLL_MS;
		r = gt_wait(tmo);
		if (gt_got_sig)
			break;
		if (r < 0) {
			if (errno == EINTR)
				continue;
			err = "select failed\n";
			status = 1;
			break;
		}
		now = gt_now_ms();
		k = GT_NONE;
		if (r > 0) {
			do {
				nread = read(0, &c, 1);
				if (nread != 1)
					break;
				k = gt_feed(c);
				if (k == GT_MORE)
					gt_esc_at = now;
			} while (k == GT_MORE && gt_pending());
			if (nread == 0 || (nread < 0 && errno != EINTR))
				break;
			if (nread < 0 || k == GT_MORE)
				k = GT_NONE;
		} else if (gt_esc_state != GT_E_IDLE && gt_esc_left(now) == 0) {
			k = gt_esc_abandon();
		}
		if (r == 0 && idle && k == GT_NONE)
			continue;
		if (k == 'q')
			break;
		if (k == GT_REPAINT) {
			app->frame(1);
			k = GT_NONE;
		}
		if (app->event(k, now, r == 0 ? tmo : 0))
			break;
		app->frame(0);
	}
	gt_leave();
	if (err)
		gt_put_fd(2, err, strlen(err));
	if (gt_got_sig)
		return 128 + (int)gt_got_sig;
	return status;
}

#pragma GCC diagnostic pop

#endif /* GAMES_GAMETERM_H */
