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
	/* The rows the kernel has no field for, and the read counts. */
	CHECK(t.c_cc[VEOL2] == _POSIX_VDISABLE);
	CHECK(t.c_cc[VSTATUS] == _POSIX_VDISABLE);
	CHECK(t.c_cc[VMIN] == 1 && t.c_cc[VTIME] == 0);

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
	/* A terminal read is the four requests and nothing else. */
	model_reset(ECHO);
	CHECK(tcgetattr(MODEL_FD, &t) == 0);
	CHECK(model.nlog == 4);
	CHECK(model.log[0] == TIOCGETP && model.log[1] == TIOCGETC &&
	    model.log[2] == TIOCGLTC && model.log[3] == TIOCLGET);
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
	return lsys_verdict("libc_termios");
}
