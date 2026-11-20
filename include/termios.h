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
 * POSIX.1-2024 (Issue 8) terminal interface, chapter 11, over the sgtty
 * ioctls of <sys/ioctl.h>. The kernel keeps its sgtty state; the library
 * maps it onto struct termios (lib/libc/gen/tcattr.c) and
 * docs/research/tty-modernization-plan.md section 2 holds the row for every
 * bit and index below, with what the kernel does with it.
 *
 * <sys/ioctl.h> supplies the speed names B0 to B4000000 (<sys/ttydev.h>),
 * _POSIX_VDISABLE (<sys/ttychars.h>) and struct winsize. It also defines the
 * sgtty flag words, so a name both interfaces share (ECHO, NOFLSH, TOSTOP,
 * FLUSHO, PENDIN, MDMBUF) keeps the single definition it has there and takes
 * its termios value from it: c_lflag and c_cflag are 32 bits wide and each
 * of those values is a free bit of its word.
 */
#ifndef _TERMIOS_H_
#define _TERMIOS_H_

#include <sys/ioctl.h>

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

#define NCCS 19

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

/* c_iflag */
#define IGNBRK  0x00000001
#define BRKINT  0x00000002
#define IGNPAR  0x00000004
#define PARMRK  0x00000008
#define INPCK   0x00000010
#define ISTRIP  0x00000020
#define INLCR   0x00000040
#define IGNCR   0x00000080
#define ICRNL   0x00000100
#define IXON    0x00000400
#define IXANY   0x00000800
#define IXOFF   0x00001000

/* c_oflag */
#define OPOST   0x00000001
#define ONLCR   0x00000004
#define OCRNL   0x00000008
#define ONOCR   0x00000010
#define ONLRET  0x00000020
#define OFILL   0x00000040
#define OFDEL   0x00000080
#define NLDLY   0x00000100
#define   NL0   0x00000000
#define   NL1   0x00000100
#define CRDLY   0x00000600
#define   CR0   0x00000000
#define   CR1   0x00000200
#define   CR2   0x00000400
#define   CR3   0x00000600
#define TABDLY  0x00001800
#define   TAB0  0x00000000
#define   TAB1  0x00000800
#define   TAB2  0x00001000
#define   TAB3  0x00001800
#define BSDLY   0x00002000
#define   BS0   0x00000000
#define   BS1   0x00002000
#define VTDLY   0x00004000
#define   VT0   0x00000000
#define   VT1   0x00004000
#define FFDLY   0x00008000
#define   FF0   0x00000000
#define   FF1   0x00008000

/* c_cflag; MDMBUF is <sys/ioctl.h>'s */
#define CSIZE   0x00000030
#define   CS5   0x00000000
#define   CS6   0x00000010
#define   CS7   0x00000020
#define   CS8   0x00000030
#define CSTOPB  0x00000040
#define CREAD   0x00000080
#define PARENB  0x00000100
#define PARODD  0x00000200
#define HUPCL   0x00000400
#define CLOCAL  0x00000800
#define CRTSCTS 0x80000000

/* c_lflag; ECHO, NOFLSH, TOSTOP, FLUSHO and PENDIN are <sys/ioctl.h>'s */
#define ISIG    0x00000001
#define ICANON  0x00000002
#define ECHOE   0x00000010
#define ECHOK   0x00000020
#define ECHONL  0x00000040
#define ECHOCTL 0x00000200
#define ECHOPRT 0x00000400
#define IEXTEN  0x00008000

/* c_cc indices */
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16
#define VDSUSP   17
#define VSTATUS  18

/* tcsetattr actions, tcflush queues, tcflow actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2
#define TCOOFF    0
#define TCOON     1
#define TCIOFF    2
#define TCION     3

speed_t cfgetispeed(const struct termios *);
speed_t cfgetospeed(const struct termios *);
int cfsetispeed(struct termios *, speed_t);
int cfsetospeed(struct termios *, speed_t);
int cfsetspeed(struct termios *, speed_t);
void cfmakeraw(struct termios *);
int tcgetattr(int, struct termios *);
int tcsetattr(int, int, const struct termios *);
int tcdrain(int);
int tcflush(int, int);
int tcflow(int, int);
int tcgetwinsize(int, struct winsize *);
int tcsetwinsize(int, const struct winsize *);

#endif /* _TERMIOS_H_ */
