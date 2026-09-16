/*
 * KL11 console on the process's own terminal. Input is polled with
 * FIONREAD between instructions, so the emulator never blocks in read;
 * output goes straight to fd 1 and the transmitter is ready again at once.
 * The terminal runs raw for the emulator's lifetime and is restored on
 * exit; Ctrl-_ (037), or "~." at the start of a line, leaves the
 * emulator, since V6 uses DEL and Ctrl-\ itself and discobsd-term owns
 * Ctrl-].
 *
 *   TKS 0777560  receiver status   bit 7 done, bit 6 interrupt enable
 *   TKB 0777562  receiver buffer   reading it clears done
 *   TPS 0777564  transmitter status bit 7 ready, bit 6 interrupt enable
 *   TPB 0777566  transmitter buffer
 */
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__linux__) || defined(__APPLE__)
#include <termios.h>
static struct termios saved;
#else
#include <sgtty.h>
static struct sgttyb saved;
#endif

#include "pdp11.h"

#define EXITKEY 037

static uint16_t TKS, TKB, TPS, TPB;
static int tty_raw;
static int input_eof;
static int at_line_start = 1;	/* "~." at the start of a line also exits */
static int tilde_pending;
static int pushback = -1;

void
cons_reset(void)
{
	TKS = 0;
	TPS = 1 << 7;
	TKB = 0;
	TPB = 0;
}

int
cons_open(void)
{
#if defined(__linux__) || defined(__APPLE__)
	struct termios t;

	if (tcgetattr(0, &saved) < 0)
		return -1;
	t = saved;
	t.c_iflag &= ~(ICRNL | IXON | IXOFF | ISTRIP | INLCR);
	t.c_oflag &= ~OPOST;
	t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSANOW, &t) < 0)
		return -1;
#else
	struct sgttyb t;

	if (ioctl(0, TIOCGETP, &saved) < 0)
		return -1;
	t = saved;
	t.sg_flags |= RAW;
	t.sg_flags &= ~ECHO;
	if (ioctl(0, TIOCSETP, &t) < 0)
		return -1;
#endif
	tty_raw = 1;
	return 0;
}

void
cons_close(void)
{
	if (!tty_raw)
		return;
#if defined(__linux__) || defined(__APPLE__)
	tcsetattr(0, TCSANOW, &saved);
#else
	ioctl(0, TIOCSETP, &saved);
#endif
	tty_raw = 0;
}

static void
addchar(uint8_t c)
{
	TKB = c;
	TKS |= 0x80;
	if (TKS & (1 << 6))
		cpu_interrupt(INTTTYIN, 4);
}

/*
 * Take one character if the receiver is empty. A character the guest has
 * not read yet stays in TKB and the tty keeps the rest queued, so nothing
 * is lost when the guest is slow. "~." at the start of a line leaves, the
 * ssh way, for a terminal or browser that keeps Ctrl-_ for itself: the
 * tilde is held until the next byte says whether it was the escape, and
 * that byte waits in pushback until the guest has taken the tilde.
 */
void
cons_poll(void)
{
	int n;		/* FIONREAD fills an int; a long kept half of it uninitialized */
	uint8_t c;

	if (input_eof || (TKS & 0x80))
		return;
	if (pushback >= 0) {
		c = pushback;
		pushback = -1;
		at_line_start = (c == '\r' || c == '\n');
		addchar(c);
		return;
	}
	if (ioctl(0, FIONREAD, &n) < 0 || n <= 0)
		return;
	if (read(0, &c, 1) != 1) {
		input_eof = 1;
		return;
	}
	if (c == EXITKEY)
		fatal("exit", 0);
	if (tilde_pending) {
		tilde_pending = 0;
		if (c == '.')
			fatal("exit", 0);
		addchar('~');
		pushback = c;
		return;
	}
	if (at_line_start && c == '~') {
		tilde_pending = 1;
		at_line_start = 0;
		return;
	}
	at_line_start = (c == '\r' || c == '\n');
	addchar(c);
}

uint16_t
cons_read16(uint32_t a)
{
	switch (a) {
	case 0777560:
		return TKS;
	case 0777562:
		if (TKS & 0x80) {
			TKS &= 0xff7e;
			return TKB;
		}
		return 0;
	case 0777564:
		return TPS;
	case 0777566:
		return 0;
	}
	TRAP(INTBUS);
	return 0;
}

void
cons_write16(uint32_t a, uint16_t v)
{
	uint8_t c;

	switch (a) {
	case 0777560:
		if (v & (1 << 6))
			TKS |= 1 << 6;
		else
			TKS &= ~(1 << 6);
		return;
	case 0777564:
		if (v & (1 << 6))
			TPS |= 1 << 6;
		else
			TPS &= ~(1 << 6);
		return;
	case 0777566:
		TPB = v & 0xff;
		c = TPB & 0x7f;
		if (write(1, &c, 1) < 0)
			fatal("console write", 0);
		TPS |= 0x80;
		if (TPS & (1 << 6))
			cpu_interrupt(INTTTYOUT, 4);
		return;
	}
	TRAP(INTBUS);
}
