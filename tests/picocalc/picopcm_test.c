/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Host test of the PicoCalc sample device (sys/arch/rp2040/dev/picopcm.c),
 * compiled unchanged with the bell (picoaudio.c) against the register
 * model in regmodel.h.
 *
 * The model of time is the test's sleep(): while the sample clock runs
 * (slice 6 enabled, its wrap interrupt enabled in the PWM block and in the
 * NVIC) each tick of a nap calls the wrap handler PCM_RATE / HZ times and
 * records the compare value the handler leaves, then the pending callout
 * runs. The recorded values must be the written samples, scaled, in
 * order, with none lost or repeated and the rest level after them. A
 * sleep with no callout pending, or more naps than a stalled write may
 * take, ends the test, so a missing bound fails instead of hanging.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/uio.h>

#include "regmodel.h"

#include <machine/board.h>
#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/lcdterm.h>
#include "picopcm.h"

#define	PWM_BASE		0x40050000UL
#define	SLICE(n)		(PWM_BASE + (n) * 0x14)
#define	CSR			0x00
#define	DIV			0x04
#define	CC			0x0c
#define	TOP			0x10
#define	PWM_INTR		(PWM_BASE + 0xa4)
#define	PWM_INTE		(PWM_BASE + 0xa8)
#define	NVIC_ISER		0xe000e100UL
#define	NVIC_ICER		0xe000e180UL
#define	GPIO_CTRL(n)		(0x40014000UL + 0x004 + 8 * (n))
#define	OUTOVER_LOW		(2U << 8)

void PWM_IRQ_WRAP_Handler(void);

const char *panicstr;
static int board = BOARD_ID_PICOCALC;
static void (*pending)(char *);
static char *pending_arg;
static int naps, clock_stuck, failures;
static unsigned char played[8192];
static unsigned nplayed;

int
board_id(void)
{
	return board;
}

void
timeout(void (*fn)(char *), char *arg, int ticks __unused)
{
	pending = fn;
	pending_arg = arg;
}

void
wakeup(caddr_t chan __unused)
{
}

static int
clock_runs(void)
{
	return !clock_stuck && (model_reg(SLICE(6) + CSR) & 1) != 0 &&
	    (model_reg(PWM_INTE) & (1U << 6)) != 0 &&
	    (model_reg(NVIC_ISER) & (1U << 4)) != 0;
}

/* One sample period: the interrupt is raised and the handler runs. */
static void
sample_tick(void)
{
	unsigned cc;

	*shim_reg(PWM_INTR) = 0;
	PWM_IRQ_WRAP_Handler();
	if (model_reg(PWM_INTR) != (1U << 6)) {
		fprintf(stderr, "FAIL the handler did not acknowledge slice 6\n");
		failures++;
	}
	cc = model_reg(SLICE(5) + CC);
	if ((cc & 0xffff) != cc >> 16 || (cc & 0xffff) >= PCM_LEVELS) {
		fprintf(stderr, "FAIL compare value %#x\n", cc);
		failures++;
	}
	if (nplayed < sizeof(played))
		played[nplayed++] = (unsigned char)cc;
}

void
sleep(caddr_t chan __unused, int pri)
{
	void (*fn)(char *) = pending;
	int i;

	if (pri >= 25) {
		fprintf(stderr, "FAIL an interruptible sleep, priority %d\n", pri);
		failures++;
	}
	if (fn == NULL || ++naps > 2000) {
		fprintf(stderr, "FAIL a sleep that nothing ends\n");
		exit(2);
	}
	if (clock_runs())
		for (i = 0; i < 4 * PCM_RATE / 1000; i++)
			sample_tick();
	pending = NULL;
	fn(pending_arg);
}

int
uiomove(caddr_t cp, u_int n, struct uio *uio)
{
	memcpy(cp, uio->uio_iov->iov_base, n);
	uio->uio_iov->iov_base += n;
	uio->uio_iov->iov_len -= n;
	uio->uio_resid -= n;
	return 0;
}

static void
check(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

static int
write_bytes(unsigned char *p, unsigned n)
{
	struct iovec iov = { (char *)p, n };
	struct uio uio = { &iov, 1, 0, n, UIO_WRITE };

	return pcmwrite(0, &uio, 0);
}

int
main(void)
{
	static unsigned char tune[1000];
	struct pcm_ring r;
	unsigned i, rest = PCM_SILENCE * PCM_LEVELS >> 8;

	/* The ring and the scaling, alone. */
	memset(&r, 0, sizeof r);
	r.head = r.tail = 250;		/* Wrap the byte indices. */
	check("an empty ring has no fill", pcm_fill(&r), 0);
	check("an empty ring has 255 free", pcm_space(&r), 255);
	check("an empty ring plays the rest sample", pcm_get(&r), PCM_SILENCE);
	for (i = 0; i < 255; i++)
		pcm_put(&r, (unsigned char)i);
	check("a full ring", pcm_fill(&r), 255);
	check("a full ring has no space", pcm_space(&r), 0);
	for (i = 0; i < 255; i++)
		if (pcm_get(&r) != (unsigned char)i) {
			check("ring order", i, -1);
			break;
		}
	check("the ring is empty again", pcm_fill(&r), 0);
	check("sample 0 is level 0", pcm_level(0), 0);
	check("sample 255 is the top level", pcm_level(255), PCM_LEVELS - 1);
	for (i = 1; i < 256; i++)
		if (pcm_level(i) < pcm_level(i - 1) || pcm_level(i) >= PCM_LEVELS)
			check("levels rise and stay under the period", i, -1);

	/* A bare Pico lacks an amplifier: no open, and no register touched. */
	board = BOARD_ID_PICO;
	check("open on a bare Pico", pcmopen(0, 0, 0), ENXIO);
	check("registers touched on a bare Pico", model_n, 0);
	board = BOARD_ID_PICOCALC;
	check("open of minor 1", pcmopen(1, 0, 0), ENXIO);

	/* A bell is sounding when the device opens. */
	lcd_bell();
	check("open", pcmopen(0, 0, 0), 0);
	check("second open", pcmopen(0, 0, 0), EBUSY);
	check("an opened device queues no sample", pcm_queued(), 0);
	check("carrier period", model_reg(SLICE(5) + TOP), PCM_LEVELS - 1);
	check("carrier divider 1", model_reg(SLICE(5) + DIV), 1 << 4);
	check("rest duty on both channels", model_reg(SLICE(5) + CC),
	    rest | rest << 16);
	check("carrier enabled", model_reg(SLICE(5) + CSR), 1);
	check("sample clock period", model_reg(SLICE(6) + TOP),
	    CPU_KHZ * 1000 / PCM_RATE - 1);
	check("sample clock enabled", model_reg(SLICE(6) + CSR), 1);
	check("wrap interrupt of slice 6 alone", model_reg(PWM_INTE), 1 << 6);
	check("NVIC enable of IRQ 4", model_reg(NVIC_ISER), 1 << 4);
	check("left pin on the slice", model_reg(GPIO_CTRL(PICOCALC_AUDIO_L)), 4);
	check("right pin on the slice", model_reg(GPIO_CTRL(PICOCALC_AUDIO_R)), 4);

	/* The bell's stop callout must leave the slice to the samples. */
	if (pending != NULL) {
		void (*fn)(char *) = pending;

		pending = NULL;
		fn(pending_arg);
	}
	check("bell stop keeps the carrier", model_reg(SLICE(5) + CSR), 1);
	check("bell stop keeps the left pin",
	    model_reg(GPIO_CTRL(PICOCALC_AUDIO_L)), 4);
	lcd_bell();
	check("a BEL while open arms no callout", pending == NULL, 1);
	check("a BEL while open keeps the carrier period",
	    model_reg(SLICE(5) + TOP), PCM_LEVELS - 1);

	/* 1,000 samples through a 255-sample ring, played in order. */
	for (i = 0; i < sizeof tune; i++)
		tune[i] = (unsigned char)(i * 37 + 11);
	nplayed = 0;
	check("write", write_bytes(tune, sizeof tune), 0);
	check("close", pcmclose(0, 0, 0), 0);
	check("a drained device queues no sample", pcm_queued(), 0);
	check("every sample played", nplayed >= sizeof tune, 1);
	for (i = 0; i < sizeof tune && i < nplayed; i++)
		if (played[i] != pcm_level(tune[i])) {
			check("played sample", played[i], pcm_level(tune[i]));
			check("at index", i, -1);
			break;
		}
	for (; i < nplayed; i++)
		if (played[i] != rest) {
			check("level after the last sample", played[i], rest);
			break;
		}

	/* The close stops the clock and holds both pins low. */
	check("wrap interrupt off", model_reg(PWM_INTE), 0);
	check("NVIC disable of IRQ 4", model_reg(NVIC_ICER), 1 << 4);
	check("sample clock stopped", model_reg(SLICE(6) + CSR), 0);
	check("carrier stopped", model_reg(SLICE(5) + CSR), 0);
	check("left pin held low", model_reg(GPIO_CTRL(PICOCALC_AUDIO_L)),
	    4 | OUTOVER_LOW);
	check("right pin held low", model_reg(GPIO_CTRL(PICOCALC_AUDIO_R)),
	    4 | OUTOVER_LOW);
	lcd_bell();
	check("the bell sounds again after the close", pending != NULL, 1);
	pending = NULL;

	/* Reads are refused. */
	check("read", pcmread(0, (struct uio *)0, 0), ENODEV);

	/* A sample clock that never ticks: the write gives up, bounded. */
	*shim_reg(NVIC_ISER) = 0;
	check("reopen", pcmopen(0, 0, 0), 0);
	clock_stuck = 1;
	naps = 0;
	check("write against a stopped clock", write_bytes(tune, 600), EIO);
	check("naps before giving up", naps, 125);
	naps = 0;
	check("close against a stopped clock", pcmclose(0, 0, 0), 0);
	check("naps of the bounded drain", naps, 125);

	if (failures != 0)
		return 1;
	printf("picopcm_test: ok\n");
	return 0;
}
