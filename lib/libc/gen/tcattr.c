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
 * tcgetattr(3) and tcsetattr(3) over the sgtty ioctls, the termios view of
 * one terminal.
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
 *
 * tcsetattr starts from the state the kernel holds and applies the rows it
 * can honor, so a bit with no sgtty equivalent keeps its value. A request
 * that differs from the report only in rows the kernel cannot honor fails
 * with EINVAL; one that has any honored part succeeds and leaves the rest
 * as it was (POSIX.1-2024 tcsetattr: EINVAL only when no part of the request
 * can be honored). The kernel offers no atomic change, so the requests go
 * out in an order that keeps their side effects apart, and only for the
 * words that changed.
 */
#include <errno.h>
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
	int mintime;			/* VMIN | VTIME << 8 (TIOCGMINTIME) */
	int mintime_ok;			/* the kernel has TIOCGMINTIME */
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
	/* A kernel without VMIN and VTIME reads as 1 and 0 and keeps them. */
	s->mintime_ok = ioctl(fd, TIOCGMINTIME, &s->mintime) == 0;
	if (!s->mintime_ok)
		s->mintime = 1;
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
	t->c_cc[VMIN] = s->mintime & 0xff;
	t->c_cc[VTIME] = (s->mintime >> 8) & 0xff;
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

static int
differs(const void *a, const void *b, unsigned int n)
{
	const unsigned char *p = a;
	const unsigned char *q = b;

	while (n-- > 0)
		if (*p++ != *q++)
			return 1;
	return 0;
}

static tcflag_t
wordof(const struct termios *t, int word)
{
	switch (word) {
	case W_IFLAG:
		return t->c_iflag;
	case W_OFLAG:
		return t->c_oflag;
	case W_CFLAG:
		return t->c_cflag;
	default:
		return t->c_lflag;
	}
}

/*
 * RAW and CBREAK stand for the pair (ICANON, ISIG): cooked mode has both,
 * CBREAK keeps signals and drops line editing, RAW drops both. Line editing
 * without signals has no sgtty state.
 */
static int
set_mode(unsigned int *f, int icanon, int isig)
{
	if (icanon && !isig)
		return 0;
	*f &= ~(RAW | CBREAK);
	if (!icanon)
		*f |= isig ? CBREAK : RAW;
	return 1;
}

int
tcsetattr(int fd, int action, const struct termios *t)
{
	struct state cur, n;
	struct termios have;
	const struct row *r;
	const struct ccrow *c;
	tcflag_t known[4];
	unsigned int f, honored, refused;
	int want, icanon, isig, ican, sig, i, local;
	tcflag_t tab;

	if (action != TCSANOW) {
		errno = EINVAL;
		return -1;
	}
	if (load(fd, &cur) < 0)
		return -1;
	n = cur;
	report(&cur, &have);
	f = cur.flags;
	honored = 0;
	refused = 0;
	known[W_IFLAG] = ICRNL;
	known[W_OFLAG] = OPOST | ONLCR | TABDLY;
	known[W_CFLAG] = HUPCL;
	known[W_LFLAG] = ICANON | ISIG | IEXTEN;

	/* One termios bit that is one sgtty bit. */
	for (r = rows; r < rows + NROWS; r++) {
		known[r->word] |= r->tbit;
		want = (wordof(t, r->word) & r->tbit) != 0;
		if (want == ((wordof(&have, r->word) & r->tbit) != 0))
			continue;
		honored = 1;
		if (want != r->inv)
			f |= r->sbit;
		else
			f &= ~r->sbit;
	}

	/* ICRNL and ONLCR are CRMOD together; a split request leaves it. */
	want = (t->c_iflag & ICRNL) != 0;
	if (want != ((t->c_oflag & ONLCR) != 0))
		honored = 1;
	else if (want != ((have.c_iflag & ICRNL) != 0)) {
		honored = 1;
		if (want)
			f |= CRMOD;
		else
			f &= ~CRMOD;
	}

	/* ICANON and ISIG, then IEXTEN against the ICANON that results. */
	icanon = (have.c_lflag & ICANON) != 0;
	isig = (have.c_lflag & ISIG) != 0;
	ican = (t->c_lflag & ICANON) != 0;
	sig = (t->c_lflag & ISIG) != 0;
	if (ican != icanon || sig != isig) {
		if (set_mode(&f, ican, sig)) {
			honored = 1;
			icanon = ican;
		} else
			refused = 1;
	}
	if (((t->c_lflag & IEXTEN) != 0) != icanon)
		refused = 1;

	/*
	 * OPOST is LITOUT clear. RAW hides it, so a request is judged against
	 * what the report shows and LITOUT changes only where it can show.
	 */
	want = (t->c_oflag & OPOST) != 0;
	if (want != ((have.c_oflag & OPOST) != 0))
		honored = 1;
	if (want)
		f &= ~LITOUT;
	else if ((f & RAW) == 0)
		f |= LITOUT;

	/* Tab expansion is the one delay field the kernel has. */
	tab = t->c_oflag & TABDLY;
	if (tab != (have.c_oflag & TABDLY)) {
		if (tab == TAB3 || tab == TAB0) {
			honored = 1;
			if (tab == TAB3)
				f |= XTABS;
			else
				f &= ~XTABS;
		} else
			refused = 1;
	}

	/* The kernel offers hang-up on last close and no way to read it back. */
	if (t->c_cflag & HUPCL)
		honored = 1;

	/* Every other differing bit names a row the kernel cannot honor. */
	for (i = 0; i < 4; i++)
		if ((wordof(t, i) ^ wordof(&have, i)) & ~known[i])
			refused = 1;

	for (c = ccrows; c < ccrows + NCCROWS; c++) {
		if (t->c_cc[c->index] != have.c_cc[c->index]) {
			honored = 1;
			*ccref(&n, c) = t->c_cc[c->index];
		}
	}
	if (t->c_cc[VEOL2] != _POSIX_VDISABLE ||
	    t->c_cc[VSTATUS] != _POSIX_VDISABLE)
		refused = 1;
	if (t->c_cc[VMIN] != have.c_cc[VMIN] ||
	    t->c_cc[VTIME] != have.c_cc[VTIME]) {
		if (cur.mintime_ok) {
			honored = 1;
			n.mintime = t->c_cc[VMIN] | (t->c_cc[VTIME] << 8);
		} else
			refused = 1;
	}

	if (t->c_ispeed > B4000000)
		refused = 1;
	else if (t->c_ispeed != have.c_ispeed) {
		honored = 1;
		n.sg.sg_ispeed = (char)t->c_ispeed;
	}
	if (t->c_ospeed > B4000000)
		refused = 1;
	else if (t->c_ospeed != have.c_ospeed) {
		honored = 1;
		n.sg.sg_ospeed = (char)t->c_ospeed;
	}

	if (refused && !honored) {
		errno = EINVAL;
		return -1;
	}

	/*
	 * The local word goes before TIOCSETN: that request may set PENDIN
	 * itself, and a later TIOCLSET would write the old value back.
	 */
	n.flags = f;
	n.sg.sg_flags = (short)(f & 0xffffU);
	local = (int)(f >> 16);
	if (differs(&n.tc, &cur.tc, sizeof n.tc) &&
	    ioctl(fd, TIOCSETC, &n.tc) < 0)
		return -1;
	if (differs(&n.lc, &cur.lc, sizeof n.lc) &&
	    ioctl(fd, TIOCSLTC, &n.lc) < 0)
		return -1;
	if ((f >> 16) != (cur.flags >> 16) &&
	    ioctl(fd, TIOCLSET, &local) < 0)
		return -1;
	if (differs(&n.sg, &cur.sg, sizeof n.sg) &&
	    ioctl(fd, TIOCSETN, &n.sg) < 0)
		return -1;
	if (n.mintime != cur.mintime &&
	    ioctl(fd, TIOCSMINTIME, &n.mintime) < 0)
		return -1;
	if ((t->c_cflag & HUPCL) && ioctl(fd, TIOCHPCL) < 0)
		return -1;
	return 0;
}
