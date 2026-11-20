/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * tcgetattr(3) over the sgtty ioctls, the termios view of one terminal.
 *
 * The kernel keeps one 32-bit flag word (t_flags), two character sets
 * (struct tchars, struct ltchars), the erase and kill characters and two
 * speeds. TIOCGETP returns the low 16 bits of the flag word, TIOCLGET the
 * high 16, so the four requests below read all of it. docs/research/
 * tty-modernization-plan.md section 2 holds the row behind every entry of
 * the tables: a termios bit that has an sgtty bit is reported from it, and a
 * bit the kernel cannot honor reports its fixed value (IXON set, CREAD set,
 * CS8, the rest clear), so a program that reads the state and writes it
 * back asks for nothing the kernel refuses.
 */
#include <termios.h>

#define W_IFLAG 0
#define W_OFLAG 1
#define W_CFLAG 2
#define W_LFLAG 3

/* All the sgtty state of one terminal. */
struct state {
	struct sgttyb sg;
	struct tchars tc;
	struct ltchars lc;
	unsigned int flags;		/* t_flags: local word above the sgtty word */
};

/* A termios bit that is one sgtty bit, or the absence of it when inv. */
struct row {
	unsigned char word;
	unsigned char inv;
	tcflag_t tbit;
	unsigned int sbit;
};

static const struct row rows[] = {
	{ W_IFLAG, 1, ISTRIP,  PASS8 },
	{ W_IFLAG, 1, IXANY,   DECCTQ },
	{ W_IFLAG, 0, IXOFF,   TANDEM },
	{ W_CFLAG, 0, CLOCAL,  NOHANG },
	{ W_CFLAG, 0, CRTSCTS, RTSCTS },
	{ W_CFLAG, 0, MDMBUF,  MDMBUF },
	{ W_LFLAG, 0, ECHO,    ECHO },
	{ W_LFLAG, 0, ECHOE,   CRTERA },
	{ W_LFLAG, 0, ECHOK,   CRTKIL },
	{ W_LFLAG, 0, NOFLSH,  NOFLSH },
	{ W_LFLAG, 0, TOSTOP,  TOSTOP },
	{ W_LFLAG, 0, ECHOCTL, CTLECH },
	{ W_LFLAG, 0, ECHOPRT, PRTERA },
	{ W_LFLAG, 0, FLUSHO,  FLUSHO },
	{ W_LFLAG, 0, PENDIN,  PENDIN },
};

/* The kernel's place for a control character: where each c_cc index lives. */
#define G_ERASE 0
#define G_KILL  1
#define G_TC    2
#define G_LTC   3

struct ccrow {
	unsigned char index;
	unsigned char group;
	unsigned char offset;
};

static const struct ccrow ccrows[] = {
	{ VERASE,   G_ERASE, 0 },
	{ VKILL,    G_KILL,  0 },
	{ VINTR,    G_TC,    0 },
	{ VQUIT,    G_TC,    1 },
	{ VSTART,   G_TC,    2 },
	{ VSTOP,    G_TC,    3 },
	{ VEOF,     G_TC,    4 },
	{ VEOL,     G_TC,    5 },
	{ VSUSP,    G_LTC,   0 },
	{ VDSUSP,   G_LTC,   1 },
	{ VREPRINT, G_LTC,   2 },
	{ VDISCARD, G_LTC,   3 },
	{ VWERASE,  G_LTC,   4 },
	{ VLNEXT,   G_LTC,   5 },
};

#define NROWS (sizeof rows / sizeof rows[0])
#define NCCROWS (sizeof ccrows / sizeof ccrows[0])

static unsigned char *
ccref(struct state *s, const struct ccrow *r)
{
	switch (r->group) {
	case G_ERASE:
		return (unsigned char *)&s->sg.sg_erase;
	case G_KILL:
		return (unsigned char *)&s->sg.sg_kill;
	case G_TC:
		return (unsigned char *)&s->tc + r->offset;
	default:
		return (unsigned char *)&s->lc + r->offset;
	}
}

/* Read every sgtty word of the terminal; -1 with errno from the kernel. */
static int
load(int fd, struct state *s)
{
	int local;

	if (ioctl(fd, TIOCGETP, &s->sg) < 0 ||
	    ioctl(fd, TIOCGETC, &s->tc) < 0 ||
	    ioctl(fd, TIOCGLTC, &s->lc) < 0 ||
	    ioctl(fd, TIOCLGET, &local) < 0)
		return -1;
	s->flags = ((unsigned int)s->sg.sg_flags & 0xffffU) |
	    (((unsigned int)local & 0xffffU) << 16);
	return 0;
}

/* The termios state the sgtty state stands for. */
static void
report(struct state *s, struct termios *t)
{
	const struct row *r;
	const struct ccrow *c;
	unsigned int f = s->flags;
	int i;

	t->c_iflag = IXON;
	t->c_oflag = 0;
	t->c_cflag = CS8 | CREAD;
	t->c_lflag = 0;
	for (r = rows; r < rows + NROWS; r++) {
		if (((f & r->sbit) != 0) != r->inv) {
			switch (r->word) {
			case W_IFLAG:
				t->c_iflag |= r->tbit;
				break;
			case W_CFLAG:
				t->c_cflag |= r->tbit;
				break;
			default:
				t->c_lflag |= r->tbit;
				break;
			}
		}
	}
	if (f & CRMOD) {
		t->c_iflag |= ICRNL;
		t->c_oflag |= ONLCR;
	}
	if ((f & (RAW | LITOUT)) == 0)
		t->c_oflag |= OPOST;
	if (f & XTABS)
		t->c_oflag |= TAB3;
	if ((f & (RAW | CBREAK)) == 0)
		t->c_lflag |= ICANON | IEXTEN;
	if ((f & RAW) == 0)
		t->c_lflag |= ISIG;

	for (i = 0; i < NCCS; i++)
		t->c_cc[i] = _POSIX_VDISABLE;
	for (c = ccrows; c < ccrows + NCCROWS; c++)
		t->c_cc[c->index] = *ccref(s, c);
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
	t->c_ispeed = (unsigned char)s->sg.sg_ispeed;
	t->c_ospeed = (unsigned char)s->sg.sg_ospeed;
}

int
tcgetattr(int fd, struct termios *t)
{
	struct state s;

	if (load(fd, &s) < 0)
		return -1;
	report(&s, t);
	return 0;
}
