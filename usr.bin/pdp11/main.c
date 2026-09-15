/*
 * pdp11 [image]
 *
 * Boots the RK05 pack in `image` (DEFAULT_IMAGE when built with one) on
 * an 11/40 with MEMSIZE bytes of core and the process's terminal as the
 * console. The pack's block 0 must hold a bootstrap (V6's
 * /usr/mdec/rkuboot prompts "@" for a kernel name).
 *
 * The line clock runs at 60 Hz of wall time, read from gettimeofday
 * every POLL instructions or every millisecond while the CPU waits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "pdp11.h"

#define POLL 1024
#define TICK_US 16667L

jmp_buf trapbuf;
static int intrap;
/* Instructions since the last device poll, and polls so far; static so
 * _longjmp cannot clobber them. */
static unsigned polln;
static unsigned long polls;
static struct timeval started;

void
fatal(const char *msg, uint32_t a)
{
	cons_close();
	rk_close();
	if (strcmp(msg, "exit") == 0) {
		struct timeval now;
		long secs;

		gettimeofday(&now, NULL);
		secs = now.tv_sec - started.tv_sec;
		if (secs < 1)
			secs = 1;
		printf("\r\n[pdp11: exit; %lu K instructions in %ld s, %lu K/s]\r\n",
		    polls, secs, polls / secs);
		exit(0);
	}
	printf("\r\n[pdp11: %s at %06lo]\r\n", msg, (unsigned long)a);
	cpu_printstate();
	exit(1);
}

void
cpu_printstate(void)
{
	int i;

	for (i = 0; i < 8; i++)
		printf("R%d=%06o%s", i, R[i], i == 7 ? "" : " ");
	printf("\r\nPS=%06o KSP=%06o USP=%06o SR0=%06o SR2=%06o\r\n",
	    PS, KSP, USP, SR0, SR2);
}

static struct timeval last;

void
clock_poll(void)
{
	struct timeval now;
	long us;

	if (gettimeofday(&now, NULL) < 0)
		return;
	us = (now.tv_sec - last.tv_sec) * 1000000L + (now.tv_usec - last.tv_usec);
	if (us < TICK_US)
		return;
	if (us > 20 * TICK_US)
		last = now;		/* fell behind: drop ticks */
	else {
		last.tv_usec += TICK_US;
		if (last.tv_usec >= 1000000L) {
			last.tv_usec -= 1000000L;
			last.tv_sec++;
		}
	}
	LKS |= 1 << 7;
	if (LKS & (1 << 6))
		cpu_interrupt(INTCLOCK, 6);
}


static void
run(void)
{
	uint16_t vec;

	for (;;) {
		vec = _setjmp(trapbuf);
		if (vec) {
			if (intrap)
				fatal("double fault", vec);
			intrap = 1;
			cpu_trapat(vec);
			intrap = 0;
		}
		for (;;) {
			if (cpu_intpending()) {
				intrap = 1;
				cpu_handleinterrupt();
				intrap = 0;
			}
			if (waiting) {
				usleep(1000);
				clock_poll();
				cons_poll();
				continue;
			}
			cpu_step();
			if (++polln >= POLL) {
				polln = 0;
				polls++;
				clock_poll();
				cons_poll();
			}
		}
	}
}

int
main(int argc, char **argv)
{
	const char *image = NULL;
	int i;

	for (i = 1; i < argc; i++)
		image = argv[i];
#ifdef DEFAULT_IMAGE
	if (image == NULL)
		image = DEFAULT_IMAGE;
#endif
	if (image == NULL) {
		fprintf(stderr, "usage: pdp11 image\n");
		return 2;
	}
	if (rk_open(image) < 0) {
		perror(image);
		return 1;
	}
	if (cons_open() < 0) {
		perror("stdin");
		rk_close();
		return 1;
	}
	gettimeofday(&last, NULL);
	started = last;
	printf("pdp11: %d KB, RK05 %s, Ctrl-_ exits\r\n", MEMSIZE / 1024, image);
	cpu_reset();
	run();
	return 0;
}
