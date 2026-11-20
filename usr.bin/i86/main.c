/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * i86: run PC DOS programs on an emulated 8086, 80186 or 80286 in real
 * mode. See i86.1. This file holds the option parsing, the step loop and
 * the host terminal.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HOSTBUILD
#include <poll.h>
#include <termios.h>
#else
#include <sgtty.h>
#endif

#include "mem.h"
#include "pc.h"

#define STEPS	2048		/* instructions between polls of the host */

static struct pc_config cf;
static int verbose, dump, raw, in_eof, in_tty;
static volatile sig_atomic_t caught;
static unsigned long nsteps;
#ifdef HOSTBUILD
static struct termios saved;
#else
static struct sgttyb saved;
#endif

static void
tty_raw(void)
{
#ifdef HOSTBUILD
	struct termios t;

	if (tcgetattr(0, &saved) != 0)
		return;
	t = saved;
	t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
	t.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	raw = tcsetattr(0, TCSANOW, &t) == 0;
#else
	struct sgttyb t;

	if (ioctl(0, TIOCGETP, &saved) != 0)
		return;
	t = saved;
	t.sg_flags &= ~(ECHO | CRMOD);
	t.sg_flags |= RAW;
	raw = ioctl(0, TIOCSETP, &t) == 0;
#endif
}

/* The one way out: the terminal gets its mode and margins back and the
 * page file is closed, whatever ends the session. Status 3 is the page
 * store's failure, after which guest memory can no longer be read. */
void
host_exit(int code)
{
	static int leaving;

	if (leaving)
		_exit(code);
	leaving = 1;
	pc_restore(code != 3);
	if (raw) {
#ifdef HOSTBUILD
		(void)tcsetattr(0, TCSANOW, &saved);
#else
		(void)ioctl(0, TIOCSETP, &saved);
#endif
	}
	if (dump && code != 3)
		pc_dump();
	mem_fini();
	if (verbose)
		fprintf(stderr, "i86: %lu polls of %d steps; pages: %lu soft, "
		    "%lu zero, %lu in, %lu out, %lu elided, %lu clean\n",
		    nsteps, STEPS, mem_stats.soft, mem_stats.zero,
		    mem_stats.in, mem_stats.out, mem_stats.elided,
		    mem_stats.clean);
	exit(code);
}

static void
store_failed(void)
{
	static const char msg[] = "i86: page file I/O failed\n";

	(void)!write(2, msg, sizeof(msg) - 1);
	host_exit(3);
}

static void
on_signal(int sig)
{
	caught = sig;
}

int
host_ready(int ms)
{
	if (in_eof)
		return (-1);
#ifdef HOSTBUILD
	{
		struct pollfd p;

		p.fd = 0;
		p.events = POLLIN;
		return (poll(&p, 1, ms) > 0);
	}
#else
	/* Input that is not a terminal is asked through select(), which
	 * also reports the end of a file or pipe: the read that follows
	 * returns nothing and sets in_eof. The consoles answer FIONREAD. */
	if (!in_tty) {
		struct timeval tv;
		fd_set r;

		FD_ZERO(&r);
		FD_SET(0, &r);
		tv.tv_sec = 0;
		tv.tv_usec = ms > 0 ? ms * 1000L : 0;
		return (select(1, &r, NULL, NULL, &tv) > 0);
	}
	for (;;) {
		long n = 0;

		if (ioctl(0, FIONREAD, &n) == 0 && n > 0)
			return (1);
		if (ms <= 0)
			return (0);
		usleep(ms > 10 ? 10000L : ms * 1000L);
		ms -= 10;
	}
#endif
}

int
host_getc(void)
{
	unsigned char ch;

	if (read(0, &ch, 1) != 1) {
		in_eof = 1;
		return (-1);
	}
	return (ch);
}

void
host_write(const char *p, unsigned n)
{
	while (n) {
		ssize_t k = write(1, p, n);

		if (k <= 0)
			break;
		p += k;
		n -= (unsigned)k;
	}
}

unsigned long
host_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return ((unsigned long)tv.tv_sec * 1000UL +
	    (unsigned long)tv.tv_usec / 1000UL);
}

static void
usage(void)
{
	fprintf(stderr, "usage: i86 [-dqrTv] [-c 8086|186|286] [-m kbytes] "
	    "[-f frames] [-w 40|80]\n"
	    "           [-s pagefile] [-k keys] [-a floppy] [-b floppy] "
	    "[-h disk] ...\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	static struct i86 cpu;
	static char store_name[32];
	const char *store = store_name;
	unsigned frames = 128, nhard = 0;
	int ch, i;

	/* One page file for each running emulator. */
	sprintf(store_name, "/tmp/i86.%u", (unsigned)getpid() & 0xffffffu);
	cf.ram_kb = 640;
	cf.cols = 80;
	cf.model = I86_286;
	while ((ch = getopt(argc, argv, "a:b:c:df:h:k:m:qrs:Tvw:")) != -1)
		switch (ch) {
		case 'a':
			cf.disk[0] = optarg;
			break;
		case 'b':
			cf.disk[1] = optarg;
			break;
		case 'c':
			if (strcmp(optarg, "8086") == 0 ||
			    strcmp(optarg, "8088") == 0)
				cf.model = I86_8086;
			else if (strcmp(optarg, "186") == 0)
				cf.model = I86_186;
			else if (strcmp(optarg, "286") == 0)
				cf.model = I86_286;
			else
				usage();
			break;
		case 'd':
			dump = 1;
			/* FALLTHROUGH */
		case 'q':
			cf.quiet = 1;
			break;
		case 'f':
			frames = (unsigned)atoi(optarg);
			break;
		case 'h':
			if (nhard == 2)
				usage();
			cf.disk[2 + nhard++] = optarg;
			break;
		case 'k':
			cf.keys = optarg;
			break;
		case 'm':
			cf.ram_kb = (unsigned)atoi(optarg);
			break;
		case 'r':
			cf.readonly = 1;
			break;
		case 's':
			store = optarg;
			break;
		case 'T':
			cf.fixed_clock = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			cf.cols = (unsigned)atoi(optarg);
			break;
		default:
			usage();
		}
	if (optind != argc || (cf.cols != 40 && cf.cols != 80) ||
	    cf.ram_kb < 64 || cf.ram_kb > 640)
		usage();
	if (mem_init((uint32_t)cf.ram_kb << 10, frames, store) != 0) {
		fprintf(stderr, "i86: no memory for %u page frames\n", frames);
		return (1);
	}
	in_tty = isatty(0);
	mem_failed = store_failed;
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);
	signal(SIGTERM, on_signal);
	/* A reader that closes the output ends the session through
	 * host_exit() like any other signal, so the page file goes with it. */
	signal(SIGPIPE, on_signal);
	/* Keys are read one at a time whether or not the display is drawn. */
	if (cf.keys == NULL)
		tty_raw();
	pc_init(&cpu, &cf);
	for (;;) {
		/* A halted processor runs again only for an interrupt it
		 * can take. */
		for (i = 0; i < STEPS && !pc_idle && (!cpu.halted ||
		    (cpu.irq && (cpu.fl & F_IF))); i++)
			i86_step(&cpu);
		nsteps++;
		if (caught)
			host_exit(128 + (int)caught);
		pc_poll(&cpu);
	}
}
