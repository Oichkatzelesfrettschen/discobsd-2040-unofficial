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
 * /dev/audio on the PicoCalc: write-only, one opener, unsigned 8-bit mono
 * at PCM_RATE samples per second on both audio channels (GP26 and GP27,
 * PWM slice 5; picoaudio.c describes the wiring).
 *
 * With the DEV_GPIO_PWM option, a program that holds a PWM pin of slice 6
 * through /dev/porta or /dev/portb (GPIO_PWM; GP28 on a PicoCalc) keeps the
 * device closed: the
 * open answers EBUSY until that pin is stopped, and a PWM request on slice 6
 * answers EBUSY while the device is open.
 *
 * Slice 5 runs a 500 kHz carrier (clk_sys / 250) whose duty cycle is the
 * sample. The board's first-order RC filter (R501/R502 220 ohm series into
 * C502/C503 100 nF and R503/R504 100 ohm to ground, AC-coupled through
 * C504/C505 47 uF into R505/R506 1.8 kohm, SW501, and U502/U503 AW8010
 * amplifiers) has its low-pass corner near 24 kHz, attenuating the 500 kHz
 * carrier by 26.4 dB relative to DC. Open and close ramp the PWM duty
 * linearly between 0 and PCM_SILENCE over 128 samples (16.0 ms at 8 kHz) to
 * suppress the turn-on/turn-off click across the 1 kHz audio band by 34 dB. Slice 6 drives zero
 * pins and serves as the sample clock: it wraps every clk_sys / PCM_RATE cycles
 * and raises the PWM wrap interrupt (datasheet 4.5.2.6 and the INTE
 * register), whose handler moves one sample from the ring to slice 5's
 * compare register. CC is double buffered (4.5.2.3), so the new duty takes
 * effect at the next carrier wrap, within 2 us.
 *
 * The interrupt keeps the NVIC's reset priority, the highest, which is
 * also SysTick's (IPL_CLOCK maps to 0 in machine/intr.h), so neither
 * preempts the other and the handler adds zero nesting levels to the kernel
 * stack beyond the one SysTick already takes. PRIMASK holds it off like
 * every interrupt: a flash erase under splhigh() (dev/flash.c) pauses the
 * sound for its length and loses no sample, because the ring only empties
 * from the handler.
 *
 * The handler does not call wakeup(). A writer that finds the ring full
 * enters a timed sleep for PCM_NAP_TICKS; if the ring is still full after
 * PCM_STALL_NAPS naps the sample clock is not running and the write fails
 * with EIO instead of sleeping forever.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/uio.h>

#include <machine/board.h>
#ifdef DEV_GPIO_PWM
#ifndef DEV_GPIO
#error "DEV_GPIO_PWM is the PWM request of DEV_GPIO"
#endif
#include <machine/gpio.h>
#endif

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include "picopcm.h"

#define	PWM_BASE		0x40050000UL
#define	PWM_SLICE(n)		(PWM_BASE + (n) * 0x14)
#define	PWM_CSR			0x00
#define	PWM_DIV			0x04
#define	PWM_CC			0x0c
#define	PWM_TOP			0x10
#define	PWM_CSR_EN		(1UL << 0)
#define	PWM_DIV_1		(1UL << 4)
#define	PWM_INTR		(PWM_BASE + 0xa4)
#define	PWM_INTE		(PWM_BASE + 0xa8)

#define	PCM_SLICE		5	/* The audio output pins. */
#define	PCM_CLOCK_SLICE		6	/* The sample clock; no pin routed. */
#define	PCM_CLOCK_COUNT		(CPU_KHZ * 1000 / PCM_RATE)
#ifdef DEV_GPIO_PWM
/* The board table of <machine/gpio.h> names these slices as the kernel's. */
_Static_assert(PCM_SLICE == GPIO_PWM_SLICE_BELL &&
    PCM_CLOCK_SLICE == GPIO_PWM_SLICE_PCM,
    "gpio_board_claims() keeps programs off the slices this driver runs");
#endif

#define	PC_RESET_PWM		(1UL << 14)
#define	PC_FUNC_PWM		4

#define	PWM_IRQ_WRAP		4
#define	NVIC_ISER		0xe000e100UL
#define	NVIC_ICER		0xe000e180UL

#define	PCM_CHUNK		32	/* Bytes moved per uiomove(). */
#define	PCM_NAP_TICKS		((4 * HZ + 999) / 1000)		/* 4 ms. */
#define	PCM_STALL_NAPS		125				/* 500 ms. */

_Static_assert(CPU_KHZ * 1000 % PCM_RATE == 0,
    "the sample clock divides clk_sys exactly");
_Static_assert(PCM_CLOCK_COUNT >= 2 && PCM_CLOCK_COUNT <= 65536,
    "TOP is sixteen bits");
_Static_assert(CPU_KHZ * 1000 / PCM_LEVELS == 500000,
    "the carrier is 500 kHz");

static struct pcm_ring pcm;
static volatile u_char pcm_closing;

/* Samples waiting for the sample interrupt; machdep.pcm reports it. */
unsigned int
pcm_queued(void)
{
	return pcm_fill(&pcm);
}

/* The wrap interrupt of the sample clock: play one sample. */
void
PWM_IRQ_WRAP_Handler(void)
{
	u_int level;

	PC_REG32(PWM_INTR) = 1UL << PCM_CLOCK_SLICE;
	level = (pcm_closing && pcm_fill(&pcm) == 0) ? 0 :
	    pcm_level(pcm_get(&pcm));
	PC_REG32(PWM_SLICE(PCM_SLICE) + PWM_CC) = level | level << 16;
}

/* Sleep PCM_NAP_TICKS atomically, not interruptibly: the wait is bounded. */
static void
pcm_nap(void)
{
	(void)tsleep((caddr_t)&pcm, PRIBIO, PCM_NAP_TICKS);
}

/* Linearly sweep duty across [from..to] at PCM_RATE and wait for drain. */
static void
pcm_ramp(int from, int to)
{
	int v = from, step = from < to ? 1 : -1, naps = 0;

	for (;;) {
		pcm_put(&pcm, (u_char)v);
		if (v == to)
			break;
		v += step;
	}
	while (pcm_fill(&pcm) != 0 && naps++ < PCM_STALL_NAPS)
		pcm_nap();
}

int
pcmopen(dev_t dev, int flag __unused, int mode __unused)
{
	if (minor(dev) != 0 || board_id() != BOARD_ID_PICOCALC)
		return ENXIO;
	if (picoaudio_pcm)
		return EBUSY;
#ifdef DEV_GPIO_PWM
	/* A program's PWM pin on slice 6 would run against the sample clock. */
	if ((gpio_pwm_pins & GPIO_PWM_SLICE_PINS(PCM_CLOCK_SLICE)) != 0)
		return EBUSY;
#endif
	picoaudio_pcm = 1;
	pcm_closing = 0;

	pc_unreset(PC_RESET_IO_BANK0 | PC_RESET_PADS_BANK0 | PC_RESET_PWM);
	pcm.head = pcm.tail = 0;
	PC_REG32(PWM_SLICE(PCM_SLICE) + PWM_TOP) = PCM_LEVELS - 1;
	PC_REG32(PWM_SLICE(PCM_SLICE) + PWM_DIV) = PWM_DIV_1;
	PC_REG32(PWM_SLICE(PCM_SLICE) + PWM_CC) = 0;
	PC_REG32(PWM_SLICE(PCM_SLICE) + PWM_CSR) = PWM_CSR_EN;
	pc_pin(PICOCALC_AUDIO_L, PC_FUNC_PWM, 0);
	pc_pin(PICOCALC_AUDIO_R, PC_FUNC_PWM, 0);

	PC_REG32(PWM_SLICE(PCM_CLOCK_SLICE) + PWM_TOP) = PCM_CLOCK_COUNT - 1;
	PC_REG32(PWM_SLICE(PCM_CLOCK_SLICE) + PWM_DIV) = PWM_DIV_1;
	PC_REG32(PWM_INTR) = 1UL << PCM_CLOCK_SLICE;
	PC_REG32(PWM_INTE) = 1UL << PCM_CLOCK_SLICE;
	PC_REG32(NVIC_ISER) = 1UL << PWM_IRQ_WRAP;
	PC_REG32(PWM_SLICE(PCM_CLOCK_SLICE) + PWM_CSR) = PWM_CSR_EN;
	pcm_ramp(1, PCM_SILENCE);
	return 0;
}

/*
 * Let the queued samples play, for at most the stall limit, ramp DC bias back
 * down to 0 if the sample clock is running, then stop the clock and hold both
 * pins low as the bell does.
 */
int
pcmclose(dev_t dev __unused, int flag __unused, int mode __unused)
{
	int naps;

	for (naps = 0; pcm_fill(&pcm) != 0 && naps < PCM_STALL_NAPS; naps++)
		pcm_nap();
	if (pcm_fill(&pcm) == 0) {
		pcm_closing = 1;
		pcm_ramp(PCM_SILENCE - 1, 0);
	}
	PC_REG32(NVIC_ICER) = 1UL << PWM_IRQ_WRAP;
	PC_REG32(PWM_INTE) = 0;
	PC_REG32(PWM_SLICE(PCM_CLOCK_SLICE) + PWM_CSR) = 0;
	picoaudio_mute();
	pcm_closing = 0;
	picoaudio_pcm = 0;
	return 0;
}

int
pcmread(dev_t dev __unused, struct uio *uio __unused, int flag __unused)
{
	return ENODEV;
}

int
pcmwrite(dev_t dev __unused, struct uio *uio, int flag __unused)
{
	u_char buf[PCM_CHUNK];
	u_int n, i;
	int error, naps = 0;

	while (uio->uio_resid > 0) {
		n = pcm_space(&pcm);
		if (n == 0) {
			if (++naps > PCM_STALL_NAPS)
				return EIO;
			pcm_nap();
			continue;
		}
		naps = 0;
		if (n > sizeof(buf))
			n = sizeof(buf);
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		error = uiomove((caddr_t)buf, n, uio);
		if (error != 0)
			return error;
		for (i = 0; i < n; i++)
			pcm_put(&pcm, buf[i]);
	}
	return 0;
}
