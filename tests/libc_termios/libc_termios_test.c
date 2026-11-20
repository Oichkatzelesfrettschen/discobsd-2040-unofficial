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
 * Host gate for the termios library members. S01 holds the header's names
 * and values, and the speed and raw-mode helpers; later steps add the
 * attribute mapping against a model of the sgtty ioctls in the same file.
 *
 * The tree's headers and the host's stay apart as libc_shim.h states: this
 * file reaches the tree's <termios.h>, <sys/ioctl.h> and <errno.h> only.
 */
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "libc_shim.h"
#include "termios_model.h"

int test_errno;

/* order.c: the same includes in the other order, compiled apart. */
unsigned order_termios_first(void);
unsigned order_ioctl_first(void);

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

struct bit {
	const char *name;
	unsigned long value;
};

#define BIT(n) { #n, (unsigned long)(n) }

static const struct bit iflag_bits[] = {
	BIT(IGNBRK), BIT(BRKINT), BIT(IGNPAR), BIT(PARMRK), BIT(INPCK),
	BIT(ISTRIP), BIT(INLCR), BIT(IGNCR), BIT(ICRNL), BIT(IXON),
	BIT(IXANY), BIT(IXOFF),
};

/* Single bits of c_oflag, and the delay fields as masks. */
static const struct bit oflag_bits[] = {
	BIT(OPOST), BIT(ONLCR), BIT(OCRNL), BIT(ONOCR), BIT(ONLRET),
	BIT(OFILL), BIT(OFDEL), BIT(NLDLY), BIT(CRDLY), BIT(TABDLY),
	BIT(BSDLY), BIT(VTDLY), BIT(FFDLY),
};

static const struct bit cflag_bits[] = {
	BIT(CSIZE), BIT(CSTOPB), BIT(CREAD), BIT(PARENB), BIT(PARODD),
	BIT(HUPCL), BIT(CLOCAL), BIT(CRTSCTS), BIT(MDMBUF),
};

static const struct bit lflag_bits[] = {
	BIT(ISIG), BIT(ICANON), BIT(ECHO), BIT(ECHOE), BIT(ECHOK),
	BIT(ECHONL), BIT(NOFLSH), BIT(TOSTOP), BIT(ECHOCTL), BIT(ECHOPRT),
	BIT(FLUSHO), BIT(PENDIN), BIT(IEXTEN),
};

static const struct bit cc_index[] = {
	BIT(VINTR), BIT(VQUIT), BIT(VERASE), BIT(VKILL), BIT(VEOF),
	BIT(VTIME), BIT(VMIN), BIT(VSTART), BIT(VSTOP), BIT(VSUSP),
	BIT(VEOL), BIT(VREPRINT), BIT(VDISCARD), BIT(VWERASE),
	BIT(VLNEXT), BIT(VEOL2), BIT(VDSUSP), BIT(VSTATUS),
};

/* Every named bit of one word is nonzero and shares no bit with another. */
static void
check_disjoint(const char *word, const struct bit *b, int n)
{
	int i, j;

	for (i = 0; i < n; i++) {
		CHECK(b[i].value != 0);
		for (j = i + 1; j < n; j++) {
			if (b[i].value & b[j].value) {
				lsys_note(word);
				lsys_note(b[i].name);
				lsys_note(b[j].name);
			}
			CHECK((b[i].value & b[j].value) == 0);
		}
	}
}

static void
check_names(void)
{
	int i, j;

	check_disjoint("c_iflag", iflag_bits, COUNT(iflag_bits));
	check_disjoint("c_oflag", oflag_bits, COUNT(oflag_bits));
	check_disjoint("c_cflag", cflag_bits, COUNT(cflag_bits));
	check_disjoint("c_lflag", lflag_bits, COUNT(lflag_bits));

	/* A delay value is a member of its own field and no other. */
	CHECK((NL1 & ~NLDLY) == 0 && NL0 == 0);
	CHECK((CR1 & ~CRDLY) == 0 && (CR2 & ~CRDLY) == 0 &&
	    (CR3 & ~CRDLY) == 0 && CR3 == (CR1 | CR2) && CR0 == 0);
	CHECK((TAB1 & ~TABDLY) == 0 && (TAB2 & ~TABDLY) == 0 &&
	    (TAB3 & ~TABDLY) == 0 && TAB3 == (TAB1 | TAB2) && TAB0 == 0);
	CHECK((BS1 & ~BSDLY) == 0 && BS0 == 0);
	CHECK((VT1 & ~VTDLY) == 0 && VT0 == 0);
	CHECK((FF1 & ~FFDLY) == 0 && FF0 == 0);
	CHECK((CS5 & ~CSIZE) == 0 && (CS6 & ~CSIZE) == 0 &&
	    (CS7 & ~CSIZE) == 0 && (CS8 & ~CSIZE) == 0);
	CHECK(CS5 != CS6 && CS5 != CS7 && CS5 != CS8 && CS6 != CS7 &&
	    CS6 != CS8 && CS7 != CS8 && CS8 == CSIZE);

	/* The c_cc indices are distinct and inside the array. */
	for (i = 0; i < COUNT(cc_index); i++) {
		CHECK(cc_index[i].value < NCCS);
		for (j = i + 1; j < COUNT(cc_index); j++)
			CHECK(cc_index[i].value != cc_index[j].value);
	}

	/* The three tables of tcsetattr(), tcflush() and tcflow(). */
	CHECK(TCSANOW == 0 && TCSADRAIN == 1 && TCSAFLUSH == 2);
	CHECK(TCIFLUSH != TCOFLUSH && TCIFLUSH != TCIOFLUSH &&
	    TCOFLUSH != TCIOFLUSH);
	CHECK(TCOOFF != TCOON && TCIOFF != TCION && TCOOFF != TCIOFF);

	/* The sgtty and termios words share ECHO and the like by name. */
	CHECK(ECHO == 0x8 && NOFLSH == 0x80000000UL);
	CHECK(order_termios_first() == order_ioctl_first());
	CHECK(order_termios_first() ==
	    (ECHO | NOFLSH | TOSTOP | FLUSHO | PENDIN | MDMBUF));

	/* A struct termios is four words, the cc array and two speeds. */
	CHECK(sizeof(struct termios) ==
	    4 * sizeof(tcflag_t) + 20 + 2 * sizeof(speed_t));
	CHECK(_POSIX_VDISABLE == 0xff);
}

static void
check_speeds(void)
{
	struct termios t;
	speed_t s;

	t.c_ispeed = B9600;
	t.c_ospeed = B115200;
	CHECK(cfgetispeed(&t) == B9600 && cfgetospeed(&t) == B115200);

	/* Every speed name the sgtty tables carry round-trips. */
	for (s = B0; s <= B4000000; s++) {
		test_errno = 0;
		CHECK(cfsetispeed(&t, s) == 0 && cfgetispeed(&t) == s);
		CHECK(cfsetospeed(&t, s) == 0 && cfgetospeed(&t) == s);
		CHECK(test_errno == 0);
	}

	/* A setter changes its own speed and no other. */
	t.c_ispeed = B300;
	t.c_ospeed = B1200;
	CHECK(cfsetispeed(&t, B9600) == 0);
	CHECK(t.c_ispeed == B9600 && t.c_ospeed == B1200);
	CHECK(cfsetospeed(&t, B19200) == 0);
	CHECK(t.c_ispeed == B9600 && t.c_ospeed == B19200);
	CHECK(cfsetspeed(&t, B57600) == 0);
	CHECK(t.c_ispeed == B57600 && t.c_ospeed == B57600);

	/* A value past the last name is refused and changes nothing. */
	t.c_ispeed = B300;
	t.c_ospeed = B1200;
	test_errno = 0;
	CHECK(cfsetispeed(&t, B4000000 + 1) == -1 && test_errno == EINVAL);
	test_errno = 0;
	CHECK(cfsetospeed(&t, B4000000 + 1) == -1 && test_errno == EINVAL);
	test_errno = 0;
	CHECK(cfsetspeed(&t, 0xffffffffU) == -1 && test_errno == EINVAL);
	CHECK(t.c_ispeed == B300 && t.c_ospeed == B1200);
}

static void
check_makeraw(void)
{
	struct termios t;
	int i;

	t.c_iflag = ~0U;
	t.c_oflag = ~0U;
	t.c_cflag = ~0U;
	t.c_lflag = ~0U;
	for (i = 0; i < NCCS; i++)
		t.c_cc[i] = 0x55;
	t.c_ispeed = B9600;
	t.c_ospeed = B9600;
	cfmakeraw(&t);

	CHECK((t.c_iflag & (IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
	    IGNCR | ICRNL | IXON)) == 0);
	CHECK((t.c_oflag & OPOST) == 0);
	CHECK((t.c_lflag & (ECHO | ECHONL | ICANON | ISIG | IEXTEN)) == 0);
	CHECK((t.c_cflag & CSIZE) == CS8 && (t.c_cflag & PARENB) == 0);
	CHECK(t.c_cc[VMIN] == 1 && t.c_cc[VTIME] == 0);

	/* Every bit it does not name stays as it was. */
	CHECK((t.c_iflag & (IXANY | IXOFF | INPCK | IGNPAR)) ==
	    (IXANY | IXOFF | INPCK | IGNPAR));
	CHECK((t.c_oflag & ONLCR) != 0);
	CHECK((t.c_lflag & (ECHOE | ECHOK | NOFLSH | TOSTOP)) ==
	    (ECHOE | ECHOK | NOFLSH | TOSTOP));
	CHECK((t.c_cflag & (CLOCAL | HUPCL | CREAD | CSTOPB)) ==
	    (CLOCAL | HUPCL | CREAD | CSTOPB));
	CHECK(t.c_cc[VINTR] == 0x55 && t.c_cc[VERASE] == 0x55);
	CHECK(t.c_ispeed == B9600 && t.c_ospeed == B9600);
}


/* The state the kernel opens a terminal in, with distinct control characters. */
static void
model_reset(unsigned int flags)
{
	static const struct model zero;

	model = zero;
	model.flags = flags;
	model.vmin = 1;
	model.vtime = 0;
	model.sg.sg_ispeed = B9600;
	model.sg.sg_ospeed = B9600;
	model.sg.sg_erase = 0x7f;
	model.sg.sg_kill = 0x15;
	model.tc.t_intrc = 0x03;
	model.tc.t_quitc = 0x1c;
	model.tc.t_startc = 0x11;
	model.tc.t_stopc = 0x13;
	model.tc.t_eofc = 0x04;
	model.tc.t_brkc = (char)0xff;
	model.lc.t_suspc = 0x1a;
	model.lc.t_dsuspc = 0x19;
	model.lc.t_rprntc = 0x12;
	model.lc.t_flushc = 0x0f;
	model.lc.t_werasc = 0x17;
	model.lc.t_lnextc = 0x16;
}

/* What the report of tcgetattr is for an sgtty state with no flag set. */
#define ZERO_IFLAG (IXON | ISTRIP | IXANY)
#define ZERO_OFLAG OPOST
#define ZERO_CFLAG (CS8 | CREAD)
#define ZERO_LFLAG (ICANON | IEXTEN | ISIG)

/*
 * One sgtty flag bit on its own: the termios bits it adds to and removes
 * from the report of the zero state. The table is the plan's section 2 read
 * row by row, bit by bit of the sgtty word; a bit with no row adds and
 * removes nothing.
 */
struct delta {
	unsigned int sbit;
	tcflag_t if_add, if_del, of_add, of_del, cf_add, lf_add, lf_del;
};

static const struct delta deltas[] = {
	{ TANDEM, IXOFF, 0, 0, 0, 0, 0, 0 },
	{ CBREAK, 0, 0, 0, 0, 0, 0, ICANON | IEXTEN },
	{ 0x00000004, 0, 0, 0, 0, 0, 0, 0 },
	{ ECHO, 0, 0, 0, 0, 0, ECHO, 0 },
	{ CRMOD, ICRNL, 0, ONLCR, 0, 0, 0, 0 },
	{ RAW, 0, 0, 0, OPOST, 0, 0, ICANON | IEXTEN | ISIG },
	{ ODDP, 0, 0, 0, 0, 0, 0, 0 },
	{ EVENP, 0, 0, 0, 0, 0, 0, 0 },
	{ 0x00000100, 0, 0, 0, 0, 0, 0, 0 },
	{ 0x00000200, 0, 0, 0, 0, 0, 0, 0 },
	{ XTABS, 0, 0, TAB3, 0, 0, 0, 0 },
	{ 0x00000800, 0, 0, 0, 0, 0, 0, 0 },
	{ 0x00001000, 0, 0, 0, 0, 0, 0, 0 },
	{ 0x00002000, 0, 0, 0, 0, 0, 0, 0 },
	{ 0x00004000, 0, 0, 0, 0, 0, 0, 0 },
	{ 0x00008000, 0, 0, 0, 0, 0, 0, 0 },
	{ CRTBS, 0, 0, 0, 0, 0, 0, 0 },
	{ PRTERA, 0, 0, 0, 0, 0, ECHOPRT, 0 },
	{ CRTERA, 0, 0, 0, 0, 0, ECHOE, 0 },
	{ 0x00080000, 0, 0, 0, 0, 0, 0, 0 },
	{ MDMBUF, 0, 0, 0, 0, MDMBUF, 0, 0 },
	{ LITOUT, 0, 0, 0, OPOST, 0, 0, 0 },
	{ TOSTOP, 0, 0, 0, 0, 0, TOSTOP, 0 },
	{ FLUSHO, 0, 0, 0, 0, 0, FLUSHO, 0 },
	{ NOHANG, 0, 0, 0, 0, CLOCAL, 0, 0 },
	{ RTSCTS, 0, 0, 0, 0, CRTSCTS, 0, 0 },
	{ CRTKIL, 0, 0, 0, 0, 0, ECHOK, 0 },
	{ PASS8, 0, ISTRIP, 0, 0, 0, 0, 0 },
	{ CTLECH, 0, 0, 0, 0, 0, ECHOCTL, 0 },
	{ PENDIN, 0, 0, 0, 0, 0, PENDIN, 0 },
	{ DECCTQ, 0, IXANY, 0, 0, 0, 0, 0 },
	{ NOFLSH, 0, 0, 0, 0, 0, NOFLSH, 0 },
};

static void
expect_zero_state(struct termios *e)
{
	e->c_iflag = ZERO_IFLAG;
	e->c_oflag = ZERO_OFLAG;
	e->c_cflag = ZERO_CFLAG;
	e->c_lflag = ZERO_LFLAG;
}

static int
same_flags(const struct termios *a, const struct termios *b)
{
	return a->c_iflag == b->c_iflag && a->c_oflag == b->c_oflag &&
	    a->c_cflag == b->c_cflag && a->c_lflag == b->c_lflag;
}

static void
check_get_flags(void)
{
	struct termios t, e;
	unsigned int all = 0;
	int i;

	/* All 32 bits of the word are covered once. */
	for (i = 0; i < COUNT(deltas); i++) {
		CHECK((all & deltas[i].sbit) == 0);
		all |= deltas[i].sbit;
	}
	CHECK(all == 0xffffffffU);

	model_reset(0);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	expect_zero_state(&e);
	CHECK(same_flags(&t, &e));

	for (i = 0; i < COUNT(deltas); i++) {
		const struct delta *d = &deltas[i];

		model_reset(d->sbit);
		expect_zero_state(&e);
		e.c_iflag = (e.c_iflag | d->if_add) & ~d->if_del;
		e.c_oflag = (e.c_oflag | d->of_add) & ~d->of_del;
		e.c_cflag |= d->cf_add;
		e.c_lflag = (e.c_lflag | d->lf_add) & ~d->lf_del;
		CHECK(tcgetattr(MODEL_FD, &t) == 0);
		if (!same_flags(&t, &e)) {
			lsys_note("sgtty bit with a wrong termios report:");
			CHECK(d->sbit == 0);
		}
		CHECK(same_flags(&t, &e));
	}

	/* The kernel's own default for a console: ECHO, CRMOD, CRTERA and the like. */
	model_reset(ECHO | CRMOD | CRTBS | CRTERA | CRTKIL | CTLECH | DECCTQ);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	CHECK(t.c_iflag == (IXON | ISTRIP | ICRNL));
	CHECK(t.c_oflag == (OPOST | ONLCR));
	CHECK(t.c_cflag == (CS8 | CREAD));
	CHECK(t.c_lflag == (ICANON | IEXTEN | ISIG | ECHO | ECHOE | ECHOK |
	    ECHOCTL));

	/* Every flag set: no bit the table does not name reaches the report. */
	model_reset(0xffffffffU);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	CHECK(t.c_iflag == (IXON | ICRNL | IXOFF));
	CHECK(t.c_oflag == (ONLCR | TAB3));
	CHECK(t.c_cflag == (CS8 | CREAD | MDMBUF | CLOCAL | CRTSCTS));
	CHECK(t.c_lflag == (ECHO | ECHOE | ECHOPRT | TOSTOP | FLUSHO | ECHOK |
	    ECHOCTL | PENDIN | NOFLSH));
}

static void
check_get_rest(void)
{
	struct termios t;
	int i;

	/* Control characters land at their termios index. */
	model_reset(0);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	CHECK(t.c_cc[VERASE] == 0x7f && t.c_cc[VKILL] == 0x15);
	CHECK(t.c_cc[VINTR] == 0x03 && t.c_cc[VQUIT] == 0x1c);
	CHECK(t.c_cc[VSTART] == 0x11 && t.c_cc[VSTOP] == 0x13);
	CHECK(t.c_cc[VEOF] == 0x04 && t.c_cc[VEOL] == 0xff);
	CHECK(t.c_cc[VSUSP] == 0x1a && t.c_cc[VDSUSP] == 0x19);
	CHECK(t.c_cc[VREPRINT] == 0x12 && t.c_cc[VDISCARD] == 0x0f);
	CHECK(t.c_cc[VWERASE] == 0x17 && t.c_cc[VLNEXT] == 0x16);
	/* The rows the kernel has no field for. */
	CHECK(t.c_cc[VEOL2] == _POSIX_VDISABLE);
	CHECK(t.c_cc[VSTATUS] == _POSIX_VDISABLE);
	/* The read counts a tty starts with. */
	CHECK(t.c_cc[VMIN] == 1 && t.c_cc[VTIME] == 0);
	model.vmin = 0xff;
	model.vtime = 0x80;
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	CHECK(t.c_cc[VMIN] == 0xff && t.c_cc[VTIME] == 0x80);
	/* A kernel without the request reads as 1 and 0. */
	model.fail_request = TIOCGMINTIME;
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	CHECK(t.c_cc[VMIN] == 1 && t.c_cc[VTIME] == 0);
	model.fail_request = 0;
	model.vmin = 1;
	model.vtime = 0;

	/* A character the kernel holds above 0x7f is not sign-extended. */
	model.tc.t_eofc = (char)0x84;
	model.sg.sg_erase = (char)0xe9;
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	CHECK(t.c_cc[VEOF] == 0x84 && t.c_cc[VERASE] == 0xe9);

	/* Speeds are the sgtty indices, stored in a signed char. */
	for (i = B0; i <= B4000000; i++) {
		model.sg.sg_ispeed = (char)i;
		model.sg.sg_ospeed = (char)(B4000000 - i);
		CHECK(tcgetattr(MODEL_FD, &t) == 0);
		CHECK(t.c_ispeed == (speed_t)i);
		CHECK(t.c_ospeed == (speed_t)(B4000000 - i));
	}
	model.sg.sg_ispeed = (char)0xf0;
	CHECK(tcgetattr(MODEL_FD, &t) == 0 && t.c_ispeed == 0xf0);
}

static void
memset_termios(struct termios *t)
{
	unsigned char *p = (unsigned char *)t;
	unsigned i;

	for (i = 0; i < sizeof *t; i++)
		p[i] = 0x5a;
}

static void
check_get_errors(void)
{
	static const unsigned int requests[] = {
		TIOCGETP, TIOCGETC, TIOCGLTC, TIOCLGET
	};
	struct termios t;
	int i;

	/* Each of the four reads can fail; the caller's struct stays whole. */
	for (i = 0; i < COUNT(requests); i++) {
		model_reset(ECHO);
		model.fail_request = requests[i];
		memset_termios(&t);
		test_errno = 0;
		CHECK(tcgetattr(MODEL_FD, &t) == -1 && test_errno == ENOTTY);
		CHECK(t.c_iflag == 0x5a5a5a5aU && t.c_lflag == 0x5a5a5a5aU);
	}
	model_reset(ECHO);
	test_errno = 0;
	CHECK(tcgetattr(MODEL_FD + 1, &t) == -1 && test_errno == EBADF);
	/* A terminal read is the five requests and nothing else. */
	model_reset(ECHO);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	CHECK(model.nlog == 5);
	CHECK(model.log[0] == TIOCGETP && model.log[1] == TIOCGETC &&
	    model.log[2] == TIOCGLTC && model.log[3] == TIOCLGET &&
	    model.log[4] == TIOCGMINTIME);
}

/* ---- tcsetattr ---- */

static struct model before;

static void
snapshot(void)
{
	before = model;
	model.nlog = 0;
}

static int
bytes_equal(const void *a, const void *b, unsigned n)
{
	const unsigned char *p = a, *q = b;

	while (n-- > 0)
		if (*p++ != *q++)
			return 0;
	return 1;
}

/* The state the kernel holds is as the snapshot had it. */
static int
state_equal(const struct model *a, const struct model *b)
{
	return a->flags == b->flags && a->hupcl == b->hupcl &&
	    a->vmin == b->vmin && a->vtime == b->vtime &&
	    a->sg.sg_ispeed == b->sg.sg_ispeed &&
	    a->sg.sg_ospeed == b->sg.sg_ospeed &&
	    a->sg.sg_erase == b->sg.sg_erase &&
	    a->sg.sg_kill == b->sg.sg_kill &&
	    bytes_equal(&a->tc, &b->tc, sizeof a->tc) &&
	    bytes_equal(&a->lc, &b->lc, sizeof a->lc);
}

static int
is_set_request(unsigned int r)
{
	return r == TIOCSETP || r == TIOCSETN || r == TIOCSETC ||
	    r == TIOCSLTC || r == TIOCLSET || r == TIOCHPCL;
}

static int
sets_issued(void)
{
	int i, n = 0;

	for (i = 0; i < model.nlog; i++)
		n += is_set_request(model.log[i]);
	return n;
}

static unsigned int
rnd(unsigned int *seed)
{
	*seed = *seed * 1664525U + 1013904223U;
	return *seed >> 8;
}

/* Sgtty states the table checks start from. */
static const unsigned int bases[] = {
	0,
	ECHO | CRMOD,
	ECHO | CRMOD | CRTBS | CRTERA | CRTKIL | CTLECH | DECCTQ,
	CBREAK | ECHO,
	RAW,
	RAW | LITOUT | PASS8,
	0xffffffffU & ~(RAW | CBREAK),
	0x00080000U | CRTBS | ODDP | EVENP | 0x00008000U | 0x00000100U,
};

struct rt_row {
	int word;
	tcflag_t bit;
	unsigned int sbit;
	int inv;
};

static const struct rt_row rt_rows[] = {
	{ 0, ISTRIP, PASS8, 1 }, { 0, IXANY, DECCTQ, 1 },
	{ 0, IXOFF, TANDEM, 0 },
	{ 2, CLOCAL, NOHANG, 0 }, { 2, CRTSCTS, RTSCTS, 0 },
	{ 2, MDMBUF, MDMBUF, 0 },
	{ 3, ECHO, ECHO, 0 }, { 3, ECHOE, CRTERA, 0 }, { 3, ECHOK, CRTKIL, 0 },
	{ 3, NOFLSH, NOFLSH, 0 }, { 3, TOSTOP, TOSTOP, 0 },
	{ 3, ECHOCTL, CTLECH, 0 }, { 3, ECHOPRT, PRTERA, 0 },
	{ 3, FLUSHO, FLUSHO, 0 }, { 3, PENDIN, PENDIN, 0 },
};

static tcflag_t *
wordp(struct termios *t, int word)
{
	switch (word) {
	case 0:
		return &t->c_iflag;
	case 1:
		return &t->c_oflag;
	case 2:
		return &t->c_cflag;
	default:
		return &t->c_lflag;
	}
}

static int
termios_equal(const struct termios *a, const struct termios *b)
{
	int i;

	if (!same_flags(a, b))
		return 0;
	for (i = 0; i < NCCS; i++)
		if (a->c_cc[i] != b->c_cc[i])
			return 0;
	return a->c_ispeed == b->c_ispeed && a->c_ospeed == b->c_ospeed;
}

/* The one change a table row makes: get, change, set, get, compare. */
static void
check_rt_bits(void)
{
	struct termios t, t2, e;
	int i, b, target;

	for (i = 0; i < COUNT(rt_rows); i++) {
		const struct rt_row *r = &rt_rows[i];

		for (b = 0; b < COUNT(bases); b++) {
			for (target = 0; target <= 1; target++) {
				unsigned int want;

				model_reset(bases[b]);
				CHECK(tcgetattr(MODEL_FD, &t) == 0);
				e = t;
				if (target)
					*wordp(&e, r->word) |= r->bit;
				else
					*wordp(&e, r->word) &= ~r->bit;
				snapshot();
				CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
				CHECK(tcgetattr(MODEL_FD, &t2) == 0);
				CHECK(termios_equal(&t2, &e));
				/* Only the row's own sgtty bit moved. */
				want = (target != r->inv) ?
				    before.flags | r->sbit :
				    before.flags & ~r->sbit;
				CHECK(model.flags == want);
				CHECK(bytes_equal(&model.tc, &before.tc,
				    sizeof model.tc));
				CHECK(bytes_equal(&model.lc, &before.lc,
				    sizeof model.lc));
			}
		}
	}
}

static void
check_rt_pairs(void)
{
	struct termios t, t2, e;
	int b, target, icanon, isig, m;
	unsigned int want;

	for (b = 0; b < COUNT(bases); b++) {
		/* ICRNL and ONLCR move together onto CRMOD. */
		for (target = 0; target <= 1; target++) {
			model_reset(bases[b]);
			CHECK(tcgetattr(MODEL_FD, &t) == 0);
			e = t;
			e.c_iflag = target ? e.c_iflag | ICRNL : e.c_iflag & ~ICRNL;
			e.c_oflag = target ? e.c_oflag | ONLCR : e.c_oflag & ~ONLCR;
			CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
			CHECK(tcgetattr(MODEL_FD, &t2) == 0);
			CHECK(termios_equal(&t2, &e));
			CHECK(((model.flags & CRMOD) != 0) == target);
			CHECK((model.flags & ~CRMOD) == (bases[b] & ~CRMOD));

			/* TAB3 is XTABS and TAB0 clears it. */
			model_reset(bases[b]);
			CHECK(tcgetattr(MODEL_FD, &t) == 0);
			e = t;
			e.c_oflag = (e.c_oflag & ~TABDLY) | (target ? TAB3 : TAB0);
			CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
			CHECK(tcgetattr(MODEL_FD, &t2) == 0);
			CHECK(termios_equal(&t2, &e));
			CHECK(((model.flags & XTABS) != 0) == target);
			CHECK((model.flags & ~XTABS) == (bases[b] & ~XTABS));
		}

		/* OPOST is LITOUT clear, and RAW hides it. */
		for (target = 0; target <= 1; target++) {
			model_reset(bases[b]);
			CHECK(tcgetattr(MODEL_FD, &t) == 0);
			e = t;
			e.c_oflag = target ? e.c_oflag | OPOST : e.c_oflag & ~OPOST;
			CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
			CHECK(tcgetattr(MODEL_FD, &t2) == 0);
			if (bases[b] & RAW) {
				CHECK((t2.c_oflag & OPOST) == 0);
				if (target)
					CHECK((model.flags & LITOUT) == 0);
			} else {
				CHECK(termios_equal(&t2, &e));
				CHECK(((model.flags & LITOUT) == 0) == target);
			}
			CHECK((model.flags & ~LITOUT) == (bases[b] & ~LITOUT));
		}

		/* ICANON and ISIG are the pair (RAW, CBREAK), in every direction. */
		for (m = 0; m < 3; m++) {
			icanon = m == 0;
			isig = m != 2;
			model_reset(bases[b]);
			CHECK(tcgetattr(MODEL_FD, &t) == 0);
			e = t;
			e.c_lflag &= ~(ICANON | ISIG | IEXTEN);
			if (icanon)
				e.c_lflag |= ICANON | IEXTEN;
			if (isig)
				e.c_lflag |= ISIG;
			CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
			CHECK(tcgetattr(MODEL_FD, &t2) == 0);
			CHECK((t2.c_lflag & (ICANON | ISIG | IEXTEN)) ==
			    (e.c_lflag & (ICANON | ISIG | IEXTEN)));
			want = bases[b] & ~(RAW | CBREAK);
			if (m == 1)
				want |= CBREAK;
			if (m == 2)
				want |= RAW;
			CHECK((model.flags & ~(PENDIN | LITOUT)) ==
			    (want & ~(PENDIN | LITOUT)));
		}
	}
}

static void
check_rt_chars(void)
{
	static const unsigned char values[] = { 0x01, 0x7f, 0x80, 0xe9, 0xff };
	struct termios t, t2, e;
	int idx[14], k, v, i;

	idx[0] = VERASE; idx[1] = VKILL; idx[2] = VINTR; idx[3] = VQUIT;
	idx[4] = VSTART; idx[5] = VSTOP; idx[6] = VEOF; idx[7] = VEOL;
	idx[8] = VSUSP; idx[9] = VDSUSP; idx[10] = VREPRINT;
	idx[11] = VDISCARD; idx[12] = VWERASE; idx[13] = VLNEXT;
	for (k = 0; k < 14; k++) {
		for (v = 0; v < COUNT(values); v++) {
			model_reset(ECHO | CRMOD);
			CHECK(tcgetattr(MODEL_FD, &t) == 0);
			e = t;
			e.c_cc[idx[k]] = values[v];
			snapshot();
			CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
			CHECK(tcgetattr(MODEL_FD, &t2) == 0);
			CHECK(termios_equal(&t2, &e));
			CHECK(model.flags == before.flags);
		}
	}
	/* VMIN and VTIME are separate bytes of one request. */
	for (k = 0; k < 2; k++) {
		for (v = 0; v < COUNT(values); v++) {
			model_reset(ECHO);
			CHECK(tcgetattr(MODEL_FD, &t) == 0);
			e = t;
			e.c_cc[k ? VTIME : VMIN] = values[v];
			CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
			CHECK(tcgetattr(MODEL_FD, &t2) == 0);
			CHECK(termios_equal(&t2, &e));
			CHECK(model.vmin == e.c_cc[VMIN]);
			CHECK(model.vtime == e.c_cc[VTIME]);
			CHECK(model.vmin == (k ? 1 : values[v]));
		}
	}
	/* Every speed name, input and output separately. */
	for (i = B0; i <= B4000000; i++) {
		model_reset(ECHO);
		CHECK(tcgetattr(MODEL_FD, &t) == 0);
		e = t;
		e.c_ispeed = (speed_t)i;
		e.c_ospeed = (speed_t)(B4000000 - i);
		CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
		CHECK(tcgetattr(MODEL_FD, &t2) == 0);
		/* An input speed of 0 follows the output speed. */
		if (i == B0)
			e.c_ispeed = e.c_ospeed;
		CHECK(termios_equal(&t2, &e));
	}
}

/* Input speed 0 means "the output speed" and survives a read and a write. */
static void
check_zero_ispeed(void)
{
	struct termios t, e;

	model_reset(ECHO);
	model.sg.sg_ispeed = B300;
	model.sg.sg_ospeed = B9600;
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	e = t;
	CHECK(cfsetispeed(&e, B0) == 0 && cfsetospeed(&e, B19200) == 0);
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
	CHECK(model.sg.sg_ispeed == B19200 && model.sg.sg_ospeed == B19200);
	/* Input speed 0 alone, with the output speed unchanged. */
	model.sg.sg_ispeed = B300;
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	e = t;
	e.c_ispeed = 0;
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
	CHECK(model.sg.sg_ispeed == B19200);

	/* A terminal already at input speed 0 stays there when written back. */
	model_reset(ECHO);
	model.sg.sg_ispeed = B0;
	model.sg.sg_ospeed = B9600;
	CHECK(tcgetattr(MODEL_FD, &t) == 0 && t.c_ispeed == B0);
	snapshot();
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &t) == 0);
	CHECK(state_equal(&model, &before) && sets_issued() == 0);
	t.c_ospeed = B19200;
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &t) == 0);
	CHECK(model.sg.sg_ispeed == B0 && model.sg.sg_ospeed == B19200);
}

/*
 * Reading the state and writing it back changes no sgtty word, including
 * the bits no termios row names, and issues no set request: TIOCSETN drains
 * and flushes input whenever RAW is set, even for an unchanged write.
 */
static void
check_set_idempotent(void)
{
	struct termios t;
	unsigned int seed = 12345, f;
	int i;

	for (i = 0; i < 600; i++) {
		f = i < COUNT(bases) ? bases[i] : rnd(&seed) ^ (rnd(&seed) << 16);
		model_reset(f);
		model.sg.sg_ispeed = (char)(rnd(&seed) % (B4000000 + 1));
		model.sg.sg_ospeed = (char)(rnd(&seed) % (B4000000 + 1));
		model.sg.sg_erase = (char)rnd(&seed);
		model.sg.sg_kill = (char)rnd(&seed);
		model.tc.t_eofc = (char)rnd(&seed);
		model.lc.t_lnextc = (char)rnd(&seed);
		model.vmin = (int)(rnd(&seed) & 0xff);
		model.vtime = (int)(rnd(&seed) & 0xff);
		CHECK(tcgetattr(MODEL_FD, &t) == 0);
		snapshot();
		CHECK(tcsetattr(MODEL_FD, TCSANOW, &t) == 0);
		CHECK(state_equal(&model, &before));
		CHECK(sets_issued() == 0);
		CHECK(model.flush_in == 0 && model.drains == 0);
	}
}

struct rf_row {
	const char *name;
	int word;
	tcflag_t set, clr;
	unsigned int base;
};

/* Requests for rows the kernel cannot honor, from a cooked or a raw base. */
static const struct rf_row rf_rows[] = {
	{ "BRKINT", 0, BRKINT, 0, ECHO },
	{ "IGNBRK", 0, IGNBRK, 0, ECHO },
	{ "IGNCR", 0, IGNCR, 0, ECHO },
	{ "IGNPAR", 0, IGNPAR, 0, ECHO },
	{ "INLCR", 0, INLCR, 0, ECHO },
	{ "INPCK", 0, INPCK, 0, ECHO },
	{ "PARMRK", 0, PARMRK, 0, ECHO },
	{ "IXON off", 0, 0, IXON, ECHO },
	{ "unnamed iflag bit", 0, 0x40000000U, 0, ECHO },
	{ "OCRNL", 1, OCRNL, 0, ECHO },
	{ "ONOCR", 1, ONOCR, 0, ECHO },
	{ "ONLRET", 1, ONLRET, 0, ECHO },
	{ "OFILL", 1, OFILL, 0, ECHO },
	{ "OFDEL", 1, OFDEL, 0, ECHO },
	{ "NL1", 1, NL1, 0, ECHO },
	{ "CR1", 1, CR1, 0, ECHO },
	{ "CR2", 1, CR2, 0, ECHO },
	{ "CR3", 1, CR3, 0, ECHO },
	{ "BS1", 1, BS1, 0, ECHO },
	{ "VT1", 1, VT1, 0, ECHO },
	{ "FF1", 1, FF1, 0, ECHO },
	{ "TAB1", 1, TAB1, TABDLY, ECHO },
	{ "TAB2", 1, TAB2, TABDLY, ECHO },
	{ "CS5", 2, CS5, CSIZE, ECHO },
	{ "CS6", 2, CS6, CSIZE, ECHO },
	{ "CS7", 2, CS7, CSIZE, ECHO },
	{ "CREAD off", 2, 0, CREAD, ECHO },
	{ "CSTOPB", 2, CSTOPB, 0, ECHO },
	{ "PARENB", 2, PARENB, 0, ECHO },
	{ "PARODD", 2, PARODD, 0, ECHO },
	{ "unnamed cflag bit", 2, 0x00040000U, 0, ECHO },
	{ "ECHONL", 3, ECHONL, 0, ECHO },
	{ "IEXTEN off while ICANON", 3, 0, IEXTEN, ECHO },
	{ "IEXTEN on while not ICANON", 3, IEXTEN, 0, CBREAK | ECHO },
	{ "IEXTEN on under RAW", 3, IEXTEN, 0, RAW },
	{ "ICANON without ISIG", 3, 0, ISIG, ECHO },
	{ "unnamed lflag bit", 3, 0x00010000U, 0, ECHO },
};

struct rf_cc {
	const char *name;
	int index;
	unsigned char value;
};

static const struct rf_cc rf_ccs[] = {
	{ "VEOL2", VEOL2, 0x01 },
	{ "VSTATUS", VSTATUS, 0x14 },
	{ "VMIN 0 without the request", VMIN, 0 },
	{ "VMIN 2 without the request", VMIN, 2 },
	{ "VTIME 1 without the request", VTIME, 1 },
};

/*
 * A request that asks only for what the kernel cannot honor fails with EINVAL
 * and changes nothing; the same request beside an honored change succeeds,
 * applies that change and leaves the refused rows as they were.
 */
static void
check_refusals(void)
{
	struct termios t, e;
	int i;

	for (i = 0; i < COUNT(rf_rows) + COUNT(rf_ccs) + 2; i++) {
		const char *name;
		unsigned int base;

		model_reset(CRMOD | ECHO);
		base = CRMOD | ECHO;
		CHECK(tcgetattr(MODEL_FD, &t) == 0);
		if (i < COUNT(rf_rows)) {
			const struct rf_row *r = &rf_rows[i];

			name = r->name;
			base = r->base | CRMOD;
			model_reset(base);
			CHECK(tcgetattr(MODEL_FD, &t) == 0);
			e = t;
			*wordp(&e, r->word) &= ~r->clr;
			*wordp(&e, r->word) |= r->set;
		} else if (i < COUNT(rf_rows) + COUNT(rf_ccs)) {
			const struct rf_cc *c = &rf_ccs[i - COUNT(rf_rows)];

			name = c->name;
			/* VMIN and VTIME refuse only on a kernel without them. */
			model.fail_request = TIOCGMINTIME;
			CHECK(tcgetattr(MODEL_FD, &t) == 0);
			e = t;
			e.c_cc[c->index] = c->value;
		} else {
			name = i == COUNT(rf_rows) + COUNT(rf_ccs) ?
			    "ispeed past B4000000" : "ospeed past B4000000";
			e = t;
			if (i == COUNT(rf_rows) + COUNT(rf_ccs))
				e.c_ispeed = B4000000 + 1;
			else
				e.c_ospeed = 0xffffffffU;
		}
		/* Refused alone: EINVAL, no request issued, state untouched. */
		snapshot();
		test_errno = 0;
		if (tcsetattr(MODEL_FD, TCSANOW, &e) != -1 ||
		    test_errno != EINVAL || sets_issued() != 0 ||
		    !state_equal(&model, &before)) {
			lsys_note("a refused row was not refused:");
			lsys_note(name);
		}
		CHECK(model.nlog == 5);
		CHECK(state_equal(&model, &before));
		CHECK(sets_issued() == 0);
		test_errno = 0;
		CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == -1 &&
		    test_errno == EINVAL);

		/* Beside an honored change: success, the change and no more. */
		e.c_lflag ^= TOSTOP;
		snapshot();
		CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
		CHECK((model.flags ^ before.flags) == TOSTOP);
		CHECK(bytes_equal(&model.tc, &before.tc, sizeof model.tc));
		CHECK(bytes_equal(&model.lc, &before.lc, sizeof model.lc));
		CHECK(model.sg.sg_ispeed == before.sg.sg_ispeed &&
		    model.sg.sg_ospeed == before.sg.sg_ospeed &&
		    model.sg.sg_erase == before.sg.sg_erase);
	}
}

static void
check_set_special(void)
{
	struct termios t, e;
	int i;

	/*
	 * A split ICRNL and ONLCR changes nothing, so alone it is refused; beside
	 * an honored change the call succeeds and CRMOD stays as it was.
	 */
	for (i = 0; i < 4; i++) {
		model_reset(i & 1 ? CRMOD : 0);
		CHECK(tcgetattr(MODEL_FD, &t) == 0);
		e = t;
		if (i & 2) {
			e.c_iflag |= ICRNL;
			e.c_oflag &= ~ONLCR;
		} else {
			e.c_iflag &= ~ICRNL;
			e.c_oflag |= ONLCR;
		}
		snapshot();
		test_errno = 0;
		CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == -1 &&
		    test_errno == EINVAL);
		CHECK(state_equal(&model, &before) && sets_issued() == 0);
		e.c_lflag ^= TOSTOP;
		CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
		CHECK((model.flags ^ before.flags) == TOSTOP);
	}

	/* HUPCL is set only: a set request reaches TIOCHPCL, a clear one does not. */
	model_reset(ECHO);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	e = t;
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0 && model.hupcl == 0);
	e.c_cflag |= HUPCL;
	snapshot();
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
	CHECK(model.hupcl == 1 && model.nlog == 6 && sets_issued() == 1);
	CHECK(model.log[5] == TIOCHPCL);
	CHECK(tcgetattr(MODEL_FD, &t) == 0 && (t.c_cflag & HUPCL) == 0);

	/* An action that is none of the three fails before any request. */
	model_reset(ECHO);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	snapshot();
	test_errno = 0;
	CHECK(tcsetattr(MODEL_FD, 7, &t) == -1 && test_errno == EINVAL);
	test_errno = 0;
	CHECK(tcsetattr(MODEL_FD, -1, &t) == -1 && test_errno == EINVAL);
	CHECK(model.nlog == 0);

	/* A bad descriptor fails in the first read. */
	test_errno = 0;
	CHECK(tcsetattr(MODEL_FD + 1, TCSANOW, &t) == -1 &&
	    test_errno == EBADF);
}

/* The requests go out as SETC, SETLTC, LSET, SETN, SMINTIME, HPCL, and only the changed. */
static void
check_set_order(void)
{
	struct termios t, e;
	static const unsigned int failing[] = {
		TIOCGETP, TIOCGETC, TIOCGLTC, TIOCLGET,
		TIOCSETC, TIOCSLTC, TIOCLSET, TIOCSETN, TIOCSMINTIME, TIOCHPCL
	};
	int i, j;

	model_reset(CBREAK | ECHO | CRMOD);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	e = t;
	e.c_cc[VINTR] = 0x01;
	e.c_cc[VSUSP] = 0x02;
	e.c_cc[VMIN] = 4;
	e.c_cc[VTIME] = 9;
	e.c_lflag = (e.c_lflag | ICANON | IEXTEN) & ~ECHO & ~TOSTOP;
	e.c_lflag |= TOSTOP;
	e.c_ispeed = B300;
	e.c_cflag |= HUPCL;
	snapshot();
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
	CHECK(model.nlog == 11);
	CHECK(model.log[5] == TIOCSETC && model.log[6] == TIOCSLTC &&
	    model.log[7] == TIOCLSET && model.log[8] == TIOCSETN &&
	    model.log[9] == TIOCSMINTIME && model.log[10] == TIOCHPCL);
	CHECK(model.vmin == 4 && model.vtime == 9);
	/* TIOCSETN set PENDIN after TIOCLSET; the later write must keep it. */
	CHECK((model.flags & PENDIN) != 0 && (model.flags & CBREAK) == 0);
	CHECK((model.flags & ECHO) == 0 && (model.flags & TOSTOP) != 0);
	CHECK(model.sg.sg_ispeed == B300 && model.tc.t_intrc == 0x01 &&
	    model.lc.t_suspc == 0x02);

	/* Each of the requests can fail, and the call reports it. */
	for (i = 0; i < COUNT(failing); i++) {
		model_reset(CBREAK | ECHO | CRMOD);
		model.fail_request = failing[i];
		/* The reads must fail too, so use the state of the last run. */
		if (i < 4) {
			CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == -1);
			continue;
		}
		model.fail_request = 0;
		CHECK(tcgetattr(MODEL_FD, &t) == 0);
		model.fail_request = failing[i];
		test_errno = 0;
		CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == -1 &&
		    test_errno == ENOTTY);
		for (j = 0; j < model.nlog; j++)
			CHECK(model.log[j] != 0);
	}
}

/* Entering RAW flushes input once; an unchanged write issues nothing. */
static void
check_raw_flush(void)
{
	struct termios t, e;

	model_reset(ECHO | CRMOD);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	e = t;
	e.c_lflag &= ~(ICANON | ISIG | IEXTEN);
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
	CHECK((model.flags & RAW) != 0 && model.flush_in == 1);
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
	CHECK(model.flush_in == 1);
	e.c_lflag ^= ECHO;
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
	CHECK(model.flush_in == 2);
}

static void
check_drain(void)
{
	model_reset(ECHO);
	CHECK(tcdrain(MODEL_FD) == 0);
	CHECK(model.nlog == 1 && model.log[0] == TIOCDRAIN && model.drained == 1);

	/* A signal ends the wait: the request's error reaches the caller. */
	model_reset(ECHO);
	model.drain_errno = EINTR;
	test_errno = 0;
	CHECK(tcdrain(MODEL_FD) == -1 && test_errno == EINTR);
	CHECK(model.drained == 0);

	/* A kernel without the request, and a descriptor that is no terminal. */
	model_reset(ECHO);
	model.fail_request = TIOCDRAIN;
	test_errno = 0;
	CHECK(tcdrain(MODEL_FD) == -1 && test_errno == ENOTTY);
	model_reset(ECHO);
	test_errno = 0;
	CHECK(tcdrain(MODEL_FD + 1) == -1 && test_errno == EBADF);
	CHECK(model.nlog == 1);
}

/* TCSADRAIN, TCSAFLUSH, tcflush and tcflow against queues. */
static void
check_actions(void)
{
	struct termios t, e, t2;
	static const int queues[] = { TCIFLUSH, TCOFLUSH, TCIOFLUSH };
	int i, a;

	/* TCSANOW leaves queued input alone, also when it changes a flag. */
	model_reset(ECHO | CRMOD);
	model.inq = 5;
	model.outq = 7;
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	e = t;
	e.c_lflag ^= ECHOE;
	CHECK(tcsetattr(MODEL_FD, TCSANOW, &e) == 0);
	CHECK(model.inq == 5 && model.outq == 7 && model.drained == 0);

	/* TCSADRAIN drains first, then makes the TCSANOW change. */
	model_reset(ECHO | CRMOD);
	model.inq = 5;
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	e = t;
	e.c_lflag ^= ECHOE;
	snapshot();
	CHECK(tcsetattr(MODEL_FD, TCSADRAIN, &e) == 0);
	CHECK(model.log[0] == TIOCDRAIN && model.drained == 1);
	CHECK(model.inq == 5);
	CHECK(tcgetattr(MODEL_FD, &t2) == 0 && termios_equal(&t2, &e));
	/* A drain that fails stops the call before it reads or writes. */
	model_reset(ECHO | CRMOD);
	model.drain_errno = EINTR;
	snapshot();
	test_errno = 0;
	CHECK(tcsetattr(MODEL_FD, TCSADRAIN, &e) == -1 && test_errno == EINTR);
	CHECK(model.nlog == 1 && state_equal(&model, &before));
	/* A request nothing of which is honored still fails with EINVAL. */
	model_reset(ECHO | CRMOD);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	t.c_cflag |= PARENB;
	test_errno = 0;
	CHECK(tcsetattr(MODEL_FD, TCSADRAIN, &t) == -1 && test_errno == EINVAL);

	/* TCSAFLUSH discards input and waits for output, changed or not. */
	for (a = 0; a < 2; a++) {
		model_reset(ECHO | CRMOD);
		model.inq = 5;
		model.outq = 7;
		CHECK(tcgetattr(MODEL_FD, &t) == 0);
		e = t;
		if (a)
			e.c_lflag ^= ECHOE;
		snapshot();
		CHECK(tcsetattr(MODEL_FD, TCSAFLUSH, &e) == 0);
		CHECK(model.inq == 0 && model.drains == 1);
		/* TIOCSETP drains by itself; a separate drain request is extra. */
		CHECK(model.drained == 0);
		CHECK(model.log[model.nlog - 1] == TIOCSETP);
		CHECK(!a || model.log[model.nlog - 2] == TIOCLSET);
		CHECK(tcgetattr(MODEL_FD, &t2) == 0 && termios_equal(&t2, &e));
		CHECK(a || (model.flags == before.flags));
	}
	/* A refused request flushes nothing. */
	model_reset(ECHO | CRMOD);
	model.inq = 5;
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	t.c_cflag |= CSTOPB;
	CHECK(tcsetattr(MODEL_FD, TCSAFLUSH, &t) == -1 && model.inq == 5);

	/* tcflush: each selector discards its queue and no other. */
	for (i = 0; i < 3; i++) {
		model_reset(ECHO);
		model.inq = 5;
		model.outq = 7;
		CHECK(tcflush(MODEL_FD, queues[i]) == 0);
		CHECK((model.inq == 0) == (queues[i] != TCOFLUSH));
		CHECK((model.outq == 0) == (queues[i] != TCIFLUSH));
		CHECK(model.nlog == 1 && model.log[0] == TIOCFLUSH);
	}
	model_reset(ECHO);
	test_errno = 0;
	CHECK(tcflush(MODEL_FD, 3) == -1 && test_errno == EINVAL);
	CHECK(tcflush(MODEL_FD, -1) == -1 && model.nlog == 0);
	test_errno = 0;
	CHECK(tcflush(MODEL_FD + 1, TCIFLUSH) == -1 && test_errno == EBADF);

	/* tcflow: output off and on; the input-side actions are unsupported. */
	model_reset(ECHO);
	CHECK(tcflow(MODEL_FD, TCOOFF) == 0 && model.stopped == 1);
	CHECK(model.log[0] == TIOCSTOP);
	CHECK(tcflow(MODEL_FD, TCOON) == 0 && model.stopped == 0);
	CHECK(model.log[1] == TIOCSTART);
	model.stopped = 1;
	model.nlog = 0;
	test_errno = 0;
	CHECK(tcflow(MODEL_FD, TCIOFF) == -1 && test_errno == EINVAL);
	test_errno = 0;
	CHECK(tcflow(MODEL_FD, TCION) == -1 && test_errno == EINVAL);
	CHECK(tcflow(MODEL_FD, 9) == -1 && tcflow(MODEL_FD, -1) == -1);
	CHECK(model.nlog == 0 && model.stopped == 1);
	test_errno = 0;
	CHECK(tcflow(MODEL_FD + 1, TCOON) == -1 && test_errno == EBADF);
}

static void
check_pgrp_winsize(void)
{
	struct winsize w, got;

	/* The foreground group reads back as written, in the kernel's range. */
	model_reset(ECHO);
	model.pgrp = 42;
	CHECK(tcgetpgrp(MODEL_FD) == 42);
	CHECK(tcsetpgrp(MODEL_FD, 0) == 0 && tcgetpgrp(MODEL_FD) == 0);
	CHECK(tcsetpgrp(MODEL_FD, 32767) == 0 && tcgetpgrp(MODEL_FD) == 32767);
	test_errno = 0;
	CHECK(tcsetpgrp(MODEL_FD, 32768) == -1 && test_errno == EINVAL);
	test_errno = 0;
	CHECK(tcsetpgrp(MODEL_FD, -1) == -1 && test_errno == EINVAL);
	CHECK(tcgetpgrp(MODEL_FD) == 32767);
	test_errno = 0;
	CHECK(tcgetpgrp(MODEL_FD + 1) == -1 && test_errno == EBADF);
	test_errno = 0;
	CHECK(tcsetpgrp(MODEL_FD + 1, 5) == -1 && test_errno == EBADF);

	/* A group id of zero is a valid answer, not a failure. */
	model.pgrp = 0;
	CHECK(tcgetpgrp(MODEL_FD) == 0);

	/* The window size reads back whole; SIGWINCH only on a change. */
	model_reset(ECHO);
	w.ws_row = 32;
	w.ws_col = 53;
	w.ws_xpixel = 320;
	w.ws_ypixel = 320;
	CHECK(tcsetwinsize(MODEL_FD, &w) == 0 && model.winch == 1);
	CHECK(tcgetwinsize(MODEL_FD, &got) == 0);
	CHECK(got.ws_row == 32 && got.ws_col == 53 && got.ws_xpixel == 320 &&
	    got.ws_ypixel == 320);
	CHECK(tcsetwinsize(MODEL_FD, &w) == 0 && model.winch == 1);
	w.ws_ypixel = 0;
	CHECK(tcsetwinsize(MODEL_FD, &w) == 0 && model.winch == 2);
	CHECK(model.win.ws_ypixel == 0);
	/* The caller's struct is read, not written. */
	w.ws_row = 24;
	CHECK(tcsetwinsize(MODEL_FD, &w) == 0 && w.ws_row == 24);
	test_errno = 0;
	CHECK(tcgetwinsize(MODEL_FD + 1, &got) == -1 && test_errno == EBADF);
	test_errno = 0;
	CHECK(tcsetwinsize(MODEL_FD + 1, &w) == -1 && test_errno == EBADF);
	CHECK(model.winch == 3);
}

static void
check_break(void)
{
	int d;

	/* A break is 0.25 to 0.5 seconds of TIOCSBRK, then released. */
	for (d = 0; d < 3; d++) {
		model_reset(ECHO);
		CHECK(tcsendbreak(MODEL_FD, d) == 0);
		CHECK(model.breaking == 0);
		CHECK(model.break_us >= 250000 && model.break_us <= 500000);
		CHECK(model.slept_us == model.break_us);
		CHECK(model.nlog == 2 && model.log[0] == TIOCSBRK &&
		    model.log[1] == TIOCCBRK);
	}

	/* A driver without the request fails before the sleep. */
	model_reset(ECHO);
	model.no_break = 1;
	test_errno = 0;
	CHECK(tcsendbreak(MODEL_FD, 0) == -1 && test_errno == ENOTTY);
	CHECK(model.slept_us == 0 && model.nlog == 1);
	test_errno = 0;
	CHECK(tcsendbreak(MODEL_FD + 1, 0) == -1 && test_errno == EBADF);

	/* A release that fails is reported. */
	model_reset(ECHO);
	model.fail_request = TIOCCBRK;
	test_errno = 0;
	CHECK(tcsendbreak(MODEL_FD, 0) == -1 && test_errno == ENOTTY);
	CHECK(model.breaking == 1);
}

int
main(void)
{
	check_names();
	check_speeds();
	check_makeraw();
	check_get_flags();
	check_get_rest();
	check_get_errors();
	check_rt_bits();
	check_rt_pairs();
	check_rt_chars();
	check_set_idempotent();
	check_refusals();
	check_set_special();
	check_set_order();
	check_raw_flush();
	check_drain();
	check_actions();
	check_zero_ispeed();
	check_pgrp_winsize();
	check_break();
	return lsys_verdict("libc_termios");
}
