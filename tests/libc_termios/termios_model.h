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
 * A model of the kernel's sgtty ioctls on one terminal, for the termios
 * library gate. It implements each request the way sys/kern/tty.c does for
 * the state the library reads and writes, and records every request it
 * receives so a test can tell which ones the library issued.
 *
 *   TIOCGETP, TIOCSETP, TIOCSETN: the speeds, erase, kill and the low 16
 *   bits of the flag word. TIOCSETP and TIOCSETN differ as sys/kern/tty.c
 *   has them: TIOCSETP always waits for output and flushes input, TIOCSETN
 *   does so only while RAW is or becomes set, and a CBREAK change without
 *   RAW sets PENDIN. The flag word keeps its high 16 bits across both.
 *   TIOCGETC, TIOCSETC, TIOCGLTC, TIOCSLTC: the two character sets.
 *   TIOCLGET, TIOCLSET: the high 16 bits of the flag word, as a 16-bit int.
 *   TIOCGMINTIME, TIOCSMINTIME: VMIN and VTIME, which ttychars() starts at
 *   1 and 0.
 */
#ifndef TERMIOS_MODEL_H
#define TERMIOS_MODEL_H

#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define MODEL_FD 3
#define MODEL_LOG 64

struct model {
	unsigned int flags;		/* t_flags */
	struct sgttyb sg;		/* speeds, erase and kill; flags unused */
	struct tchars tc;
	struct ltchars lc;
	int hupcl;			/* TS_HUPCLS */
	int flush_in;			/* TIOCSETP/SETN/FLUSH input flushes */
	int flush_out;
	int drains;			/* ttywait() calls */
	int stopped;			/* TS_TTSTOP */
	int pgrp;
	struct winsize win;
	int winch;			/* SIGWINCH deliveries */
	unsigned int log[MODEL_LOG];	/* requests received, in order */
	int nlog;
	unsigned int fail_request;	/* a request that fails with ENOTTY, or 0 */
	int vmin, vtime;		/* t_vmin, t_vtime */
	int drain_errno;		/* nonzero: TIOCDRAIN fails with it (a signal) */
	int drained;			/* TIOCDRAIN requests that completed */
	int breaking;			/* TIOCSBRK is in force */
	long break_us;			/* microseconds slept while it was */
	long slept_us;			/* all microseconds slept */
	int no_break;			/* the driver lacks a break request */
	int log_flush;			/* the argument of the last TIOCFLUSH */
	int inq, outq;			/* bytes queued for reading and for output */
};

static struct model model;

/* The kernel compares the old and new sizes with bcmp(). */
static int
bytes_differ(const void *a, const void *b, unsigned n)
{
	const unsigned char *p = a, *q = b;

	while (n-- > 0)
		if (*p++ != *q++)
			return 1;
	return 0;
}

static int
model_ioctl(int fd, unsigned int request, void *data)
{
	unsigned int newflags;

	if (model.nlog < MODEL_LOG)
		model.log[model.nlog++] = request;
	if (fd != MODEL_FD) {
		errno = EBADF;
		return -1;
	}
	if (request == model.fail_request) {
		errno = ENOTTY;
		return -1;
	}
	switch (request) {
	case TIOCGETP: {
		struct sgttyb *sg = data;

		*sg = model.sg;
		sg->sg_flags = (short)model.flags;
		return 0;
	}
	case TIOCSETP:
	case TIOCSETN: {
		struct sgttyb *sg = data;

		model.sg.sg_erase = sg->sg_erase;
		model.sg.sg_kill = sg->sg_kill;
		model.sg.sg_ispeed = sg->sg_ispeed;
		model.sg.sg_ospeed = sg->sg_ospeed;
		newflags = (model.flags & 0xffff0000U) |
		    ((unsigned int)sg->sg_flags & 0xffffU);
		if ((model.flags & RAW) || (newflags & RAW) ||
		    request == TIOCSETP) {
			model.drains++;
			model.flush_in++;
			model.inq = 0;
		} else if ((model.flags & CBREAK) != (newflags & CBREAK) &&
		    !(newflags & CBREAK)) {
			model.flags |= PENDIN;
			newflags |= PENDIN;
		}
		model.flags = newflags;
		if (model.flags & RAW)
			model.stopped = 0;
		return 0;
	}
	case TIOCGETC:
		*(struct tchars *)data = model.tc;
		return 0;
	case TIOCSETC:
		model.tc = *(struct tchars *)data;
		return 0;
	case TIOCGLTC:
		*(struct ltchars *)data = model.lc;
		return 0;
	case TIOCSLTC:
		model.lc = *(struct ltchars *)data;
		return 0;
	case TIOCLGET:
		*(int *)data = (int)(model.flags >> 16);
		return 0;
	case TIOCLSET:
		model.flags = (model.flags & 0xffffU) |
		    (((unsigned int)*(int *)data & 0xffffU) << 16);
		return 0;
	case TIOCGMINTIME:
		*(int *)data = model.vmin | (model.vtime << 8);
		return 0;
	case TIOCSMINTIME:
		model.vmin = *(int *)data & 0xff;
		model.vtime = (*(int *)data >> 8) & 0xff;
		return 0;
	case TIOCFLUSH: {
		int what = *(int *)data;

		model.log_flush = what;
		if (what == 0)
			what = FREAD | FWRITE;
		what &= FREAD | FWRITE;
		if (what & FREAD)
			model.inq = 0;
		if (what & FWRITE)
			model.outq = 0;
		return 0;
	}
	case TIOCSTOP:
		model.stopped = 1;
		return 0;
	case TIOCSTART:
		model.stopped = 0;
		return 0;
	case TIOCGPGRP:
		*(int *)data = model.pgrp;
		return 0;
	case TIOCSPGRP:
		if (*(int *)data < 0 || *(int *)data > 32767) {
			errno = EINVAL;
			return -1;
		}
		model.pgrp = *(int *)data;
		return 0;
	case TIOCGWINSZ:
		*(struct winsize *)data = model.win;
		return 0;
	case TIOCSWINSZ:
		if (!bytes_differ(&model.win, data, sizeof model.win))
			return 0;
		model.win = *(struct winsize *)data;
		model.winch++;
		return 0;
	case TIOCSBRK:
	case TIOCCBRK:
		if (model.no_break) {
			errno = ENOTTY;
			return -1;
		}
		model.breaking = request == TIOCSBRK;
		return 0;
	case TIOCDRAIN:
		if (model.drain_errno) {
			errno = model.drain_errno;
			return -1;
		}
		model.drained++;
		return 0;
	case TIOCHPCL:
		model.hupcl = 1;
		return 0;
	default:
		errno = ENOTTY;
		return -1;
	}
}

/* usleep() is renamed to this: time passes, and a break is timed. */
void
test_usleep(long us)
{
	model.slept_us += us;
	if (model.breaking)
		model.break_us += us;
}

/* The library's ioctl() is renamed to this (-Dioctl=test_ioctl). */
int
test_ioctl(int fd, int request, ...)
{
	va_list ap;
	void *data;

	data = 0;
	/* Only a request that carries a parameter has an argument to read. */
	if (request & (IOC_IN | IOC_OUT)) {
		va_start(ap, request);
		data = va_arg(ap, void *);
		va_end(ap);
	}
	return model_ioctl(fd, (unsigned int)request, data);
}

#endif /* TERMIOS_MODEL_H */
