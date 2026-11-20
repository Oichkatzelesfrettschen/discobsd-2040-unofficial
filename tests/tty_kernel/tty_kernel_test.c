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
 * Host gate for the tty.c code that holds VMIN and VTIME. extract.py lifts
 * ttychars() and the TIOCGMINTIME and TIOCSMINTIME cases of ttioctl() out of
 * the production source; they compile here against the kernel's own struct
 * tty, so the two padding bytes the fields occupy and the request that
 * reads and writes them are the shipped ones.
 *
 * The kernel headers are included without the host's (-nostdinc); the
 * reporting half of the gate is the host-side libc_shim.c of
 * tests/libc_sysctl.
 */
#define KERNEL 1
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/tty.h>

#include "libc_shim.h"

#ifndef TTY_EXTRACTED
#define TTY_EXTRACTED "tty_extracted.c"
#endif
#include TTY_EXTRACTED

_Static_assert(sizeof(struct tty) == 100, "struct tty size changed");
_Static_assert(__builtin_offsetof(struct tty, t_vmin) == 98,
    "t_vmin offset changed");
_Static_assert(__builtin_offsetof(struct tty, t_vtime) == 99,
    "t_vtime offset changed");

/* A tty whose every byte is a value no default produces. */
static void
fill(struct tty *tp)
{
	unsigned char *p = (unsigned char *)tp;
	unsigned i;

	for (i = 0; i < sizeof *tp; i++)
		p[i] = 0xa5;
}

static int
same_bytes(const void *a, const void *b, unsigned n)
{
	const unsigned char *p = a, *q = b;

	while (n-- > 0)
		if (*p++ != *q++)
			return 0;
	return 1;
}

static void
check_default(void)
{
	struct tty t;
	int v = -1;

	/* ttychars() gives a fresh tty the read the shim has always reported. */
	fill(&t);
	ttychars(&t);
	CHECK(t.t_vmin == 1 && t.t_vtime == 0);
	CHECK(mintime_request(&t, TIOCGMINTIME, (caddr_t)&v) == 0);
	CHECK(v == 1);
}

static void
check_round_trip(void)
{
	struct tty t, before;
	int v, got;

	/* Every VMIN and VTIME pair reads back as it was written. */
	for (v = 0; v <= 0xffff; v++) {
		ttychars(&t);
		got = v;
		CHECK(mintime_request(&t, TIOCSMINTIME, (caddr_t)&got) == 0);
		CHECK(t.t_vmin == (v & 0xff) && t.t_vtime == ((v >> 8) & 0xff));
		got = -1;
		CHECK(mintime_request(&t, TIOCGMINTIME, (caddr_t)&got) == 0);
		CHECK(got == v);
	}

	/* Bits above the two bytes are ignored. */
	ttychars(&t);
	got = 0x7f12ab34;
	CHECK(mintime_request(&t, TIOCSMINTIME, (caddr_t)&got) == 0);
	CHECK(t.t_vmin == 0x34 && t.t_vtime == 0xab);
	got = -1;
	CHECK(mintime_request(&t, TIOCGMINTIME, (caddr_t)&got) == 0);
	CHECK(got == 0xab34);
	got = -1;
	CHECK(mintime_request(&t, TIOCSMINTIME, (caddr_t)&got) == 0);
	CHECK(t.t_vmin == 0xff && t.t_vtime == 0xff);

	/* The request touches no other byte of the tty. */
	fill(&t);
	ttychars(&t);
	t.t_flags = 0x12345678;
	t.t_winsize.ws_row = 24;
	t.t_winsize.ws_col = 80;
	t.t_ospeed = 9;
	before = t;
	got = 0x0203;
	CHECK(mintime_request(&t, TIOCSMINTIME, (caddr_t)&got) == 0);
	before.t_vmin = 3;
	before.t_vtime = 2;
	CHECK(same_bytes(&t, &before, sizeof t));
	got = 0;
	CHECK(mintime_request(&t, TIOCGMINTIME, (caddr_t)&got) == 0);
	CHECK(same_bytes(&t, &before, sizeof t));

	/* A request the cases do not own is left to the rest of ttioctl(). */
	CHECK(mintime_request(&t, TIOCGWINSZ, (caddr_t)&got) == -1);
	CHECK(same_bytes(&t, &before, sizeof t));
}

static void
check_request_codes(void)
{
	/* One int in each direction, in the tty group, distinct from all else. */
	CHECK(TIOCGMINTIME != TIOCSMINTIME);
	CHECK((TIOCGMINTIME & IOC_OUT) && !(TIOCGMINTIME & IOC_IN));
	CHECK((TIOCSMINTIME & IOC_IN) && !(TIOCSMINTIME & IOC_OUT));
	CHECK(((TIOCGMINTIME >> 16) & IOCPARM_MASK) == sizeof(int));
	CHECK(((TIOCSMINTIME >> 16) & IOCPARM_MASK) == sizeof(int));
	CHECK(TIOCGMINTIME != TIOCGWINSZ && TIOCSMINTIME != TIOCSWINSZ);
	CHECK(TIOCGMINTIME != TIOCGETP && TIOCSMINTIME != TIOCSETP);
}

int
main(void)
{
	check_default();
	check_round_trip();
	check_request_codes();
	return lsys_verdict("tty_kernel");
}
