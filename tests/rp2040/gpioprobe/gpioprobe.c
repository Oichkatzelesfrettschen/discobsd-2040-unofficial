/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * gpioprobe: drive one free pin of the bare Pico through /dev/porta and
 * read it back (sys/arch/rp2040/dev/gpio.c, doc/GPIO.md). GP2 lacks a
 * board function, and with its pad input enabled (datasheet 2.19.6.3,
 * PADS_BANK0 IE resets to 1) the SIO input register reads the level the
 * output drives. Each step prints one key=value line; the exit status
 * counts the failed steps. The pin must be unconnected: the probe drives
 * it high.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <machine/gpio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	PIN		2
#define	PIN_MASK	(1U << PIN)
#define	UART0_MASK	0x3U		/* GP0 and GP1, kept by the board */

static int failed;

static void
verdict(const char *step, int ok, int value, int error)
{
	printf("gpio.%s=%s value=0x%x errno=%d\n", step, ok ? "pass" : "fail",
	    (unsigned int)value, error);
	if (!ok)
		failed++;
}

static int
request(int fd, unsigned int cmd, unsigned int mask, int *error)
{
	int r;

	errno = 0;
	r = ioctl(fd, (u_int)(cmd | GPIO_PORTA), (char *)(unsigned long)mask);
	*error = errno;
	return r;
}

int
main(void)
{
	int fd, r, error;
	char byte;

	fd = open("/dev/porta", O_RDWR);
	verdict("open", fd >= 0, fd, fd < 0 ? errno : 0);
	if (fd < 0)
		return 1;

	r = request(fd, GPIO_CONFOUT, PIN_MASK, &error);
	verdict("confout", r == 0, r, error);

	r = request(fd, GPIO_SET, PIN_MASK, &error);
	verdict("set", r == 0, r, error);
	r = request(fd, GPIO_POLL, 0, &error);
	verdict("poll_high", r != -1 && (r & PIN_MASK) != 0, r, error);

	r = request(fd, GPIO_CLEAR, PIN_MASK, &error);
	verdict("clear", r == 0, r, error);
	r = request(fd, GPIO_POLL, 0, &error);
	verdict("poll_low", r != -1 && (r & PIN_MASK) == 0, r, error);

	/* The board keeps GP0 and GP1: a request for them leaves both unchanged. */
	r = request(fd, GPIO_CONFOUT | GPIO_SET, UART0_MASK, &error);
	verdict("uart0_ignored", r == 0, r, error);
	r = request(fd, GPIO_POLL, 0, &error);
	verdict("uart0_unreported", r != -1 && (r & UART0_MASK) == 0, r,
	    error);

	r = request(fd, GPIO_CONFOD, PIN_MASK, &error);
	verdict("confod_refused", r == -1 && error == EINVAL, r, error);

	errno = 0;
	r = ioctl(fd, (u_int)(GPIO_POLL | GPIO_PORT(GPIO_NPORTS)), (char *)0);
	error = errno;
	verdict("bad_port", r == -1 && error == ENXIO, r, error);

	errno = 0;
	r = (int)read(fd, &byte, 1);
	error = errno;
	verdict("read_refused", r == -1 && error == ENODEV, r, error);

	r = request(fd, GPIO_DECONF, PIN_MASK, &error);
	verdict("deconf", r == 0, r, error);

	close(fd);
	printf("gpio.failed=%d\n", failed);
	return failed;
}
