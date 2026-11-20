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

int
main(void)
{
	check_names();
	check_speeds();
	check_makeraw();
	return lsys_verdict("libc_termios");
}
