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

#ifndef _SHIM_VT_TTY_H_
#define _SHIM_VT_TTY_H_

#include <sys/ioctl.h>

/*
 * A model of the line discipline's state that lcdcons.c reads and writes:
 * the output queue as a byte string, the state word and the entry points
 * the console calls. The TS_ values are those of sys/sys/tty.h; the test's
 * Makefile rule compares each against that header.
 */
struct uio;

struct clist {
	int		c_cc;
	int		c_head;
	unsigned char	c_buf[1024];
};

struct tty {
	struct clist	t_outq;
	caddr_t		t_addr;
	long		t_state;
	long		t_flags;
	int		t_pgrp;
	dev_t		t_dev;
	char		t_ispeed, t_ospeed;
	void		(*t_oproc)(struct tty *);
	struct winsize	t_winsize;
};

#define	TS_TIMEOUT	0x000001L
#define	TS_ISOPEN	0x000004L
#define	TS_CARR_ON	0x000010L
#define	TS_BUSY		0x000020L
#define	TS_ASLEEP	0x000040L
#define	TS_XCLUDE	0x000080L
#define	TS_TTSTOP	0x000100L
#define	TS_HIDDEN	0x008000L

#define	getc(q)	shim_vt_getc(q)
int	shim_vt_getc(struct clist *);
void	ttyinput(int c, struct tty *tp);
void	ttyowake(struct tty *tp);
void	ttstart(struct tty *tp);
void	ttychars(struct tty *tp);
int	ttyopen(dev_t dev, struct tty *tp);
void	ttyclose(struct tty *tp);
void	ttyflush(struct tty *tp, int rw);
void	ttywflush(struct tty *tp);
int	ttread(struct tty *tp, struct uio *uio, int flag);
int	ttwrite(struct tty *tp, struct uio *uio, int flag);
int	ttioctl(struct tty *tp, u_int cmd, caddr_t addr, int flag);
int	ttyselect(struct tty *tp, int rw);

#endif
