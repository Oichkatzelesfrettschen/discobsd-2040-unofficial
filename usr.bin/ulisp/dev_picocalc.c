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
 * The uLisp devices of a PicoCalc running DiscoBSD: the pixel display
 * /dev/lcdgfx, the pins /dev/porta and /dev/portb, and the sample audio
 * /dev/audio. Each device is opened at its first use. The analogue inputs
 * are the sysctl nodes machdep.adc.N and the analogue outputs the PWM
 * request of the pins' device (machine/gpio.h, machine/cpu.h).
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/lcdgfx.h>
#include <machine/cpu.h>
#include <machine/gpio.h>
#include <fcntl.h>
#include <unistd.h>

#include "platform.h"

#define AUDIO_RATE	8000	/* Samples a second (dev/picopcm.h). */
#define AUDIO_HIGH	0xb0	/* The square wave's two levels, about */
#define AUDIO_LOW	0x50	/* the 0x80 the output rests at. */

static int panel = -1, audio = -1, port[GPIO_NPORTS] = { -1, -1 };
/*
 * dev_bits() draws through palette entries PAL_BG and PAL_FG, which every
 * open resets; pal_set says they hold pal_fg and pal_bg.
 */
#define PAL_BG		14
#define PAL_FG		15
static int pal_set;
static uint16_t pal_fg, pal_bg;

/*
 * The open takes the panel from the text console, which draws nothing
 * until dev_release() closes the device again.
 */
static int
display(void)
{
	struct lcdg_colmod cm;

	if (panel < 0) {
		sys_flush();
		panel = open("/dev/lcdgfx", O_RDWR);
		if (panel < 0)
			ulisp_fail("no pixel display");
		/*
		 * An open starts in RGB666, three wire bytes a pixel; every
		 * color this program draws is an RGB565 value, which the
		 * panel takes in two.
		 */
		pal_set = 0;
		cm.fmt = LCDG_COLMOD_RGB565;
		if (ioctl(panel, LCDG_COLMOD, &cm) < 0) {
			(void)close(panel);
			panel = -1;
			ulisp_fail("no pixel display format");
		}
	}
	return panel;
}

void
dev_fill(int x0, int y0, int x1, int y1, uint16_t color)
{
	struct lcdg_rect r = {
		(u_short)x0, (u_short)y0, (u_short)x1, (u_short)y1, color
	};

	(void)ioctl(display(), LCDG_FILL, &r);
}

/* An RGB565 color into a palette entry, which keeps it exactly. */
static void
palette(int fd, int index, uint16_t color)
{
	struct lcdg_palette p = {
		(u_char)index, (u_char)((color >> 11) << 3),
		(u_char)(((color >> 5) & 63) << 2), (u_char)((color & 31) << 3)
	};

	(void)ioctl(fd, LCDG_PALETTE, &p);
}

/*
 * One window of one-bit pixels: 11 bytes of window command and the block's
 * pixels on the panel's wire, where a fill per run of each color costs the
 * command again for every run.
 */
void
dev_bits(int x, int y, int w, int h, const uint8_t *rows, uint16_t fg,
    uint16_t bg)
{
	struct lcdg_window win = {
		(u_short)x, (u_short)y, (u_short)w, (u_short)h, LCDG_WIN_BITS,
		PAL_FG, PAL_BG
	};
	int fd = display();
	size_t n = (size_t)((w + 7) / 8) * (size_t)h;

	if (!pal_set || pal_fg != fg || pal_bg != bg) {
		palette(fd, PAL_FG, fg);
		palette(fd, PAL_BG, bg);
		pal_fg = fg;
		pal_bg = bg;
		pal_set = 1;
	}
	if (ioctl(fd, LCDG_WINDOW, &win) < 0)
		return;
	while (n > 0) {
		ssize_t k = write(fd, rows, n);

		if (k <= 0)
			break;
		rows += k;
		n -= (size_t)k;
	}
}

/* The kernel lets the superuser alone read the panel's memory. */
int
dev_readpixel(int x, int y)
{
	struct lcdg_read r = { (u_short)x, (u_short)y, 1, 1, LCDG_RD_RGB565 };
	u_short pixel;
	int fd = display();

	if (ioctl(fd, LCDG_READ, &r) < 0 ||
	    read(fd, &pixel, sizeof pixel) != (ssize_t)sizeof pixel)
		ulisp_fail("cannot read the display");
	return pixel;
}

void
dev_release(void)
{
	if (panel >= 0)
		(void)close(panel);
	panel = -1;
}

/*
 * Sends one request for a pin; the result is the port's levels for a poll,
 * or -1 when the kernel refuses. extra fills the argument's high half, the
 * duty of a PWM request.
 */
static int
pin_request(int pin, unsigned int request, unsigned int extra)
{
	static const char *const name[GPIO_NPORTS] = {
		"/dev/porta", "/dev/portb"
	};
	int p = pin >> 4;

	if (pin < 0 || pin > 29)
		ulisp_fail("invalid pin");
	if (port[p] < 0 && (port[p] = open(name[p], O_RDWR)) < 0)
		ulisp_fail("the pins need the superuser");
	return ioctl(port[p], GPIO_PORT(p) | request,
	    1U << (pin & 15) | extra << 16);
}

/* The pads have no pull selection here: every input mode is plain input. */
void
dev_pinmode(int pin, int mode)
{
	(void)pin_request(pin, mode == SYS_OUTPUT ? GPIO_CONFOUT : GPIO_CONFIN,
	    0);
}

int
dev_digitalread(int pin)
{
	return (pin_request(pin, GPIO_POLL, 0) >> (pin & 15)) & 1;
}

void
dev_digitalwrite(int pin, int level)
{
	(void)pin_request(pin, level != 0 ? GPIO_SET : GPIO_CLEAR, 0);
}

/*
 * GP26-GP29 are ADC inputs 0-3. The kernel refuses the pins a board owns
 * and the node is absent from a kernel built without the ADC, which the
 * same message reports.
 */
int
dev_analogread(int pin)
{
	int mib[3] = { CTL_MACHDEP, CPU_ADC, pin - 26 };
	int raw;
	size_t len = sizeof raw;

	if (pin < 26 || pin > 29 ||
	    sysctl(mib, 3, &raw, &len, NULL, 0) < 0 || len != sizeof raw)
		ulisp_fail("no analogue input device");
	return raw;
}

/*
 * The kernel drives the pin from a PWM slice at its fixed carrier, and
 * refuses a pin the board owns and a slice the kernel uses. The pin keeps
 * the duty until the interpreter exits and closes the device.
 */
void
dev_analogwrite(int pin, int duty)
{
	if (pin < 0 || pin > 29 ||
	    pin_request(pin, GPIO_PWM, (unsigned int)duty) < 0)
		ulisp_fail("no analogue output device");
}

/*
 * The device plays samples at AUDIO_RATE and its write blocks while its
 * buffer is full, so writing ms of samples takes ms.
 */
bool
dev_tone(unsigned int hz, unsigned long ms)
{
	static unsigned int phase;
	unsigned char samples[160];
	unsigned int count = (unsigned int)ms * (AUDIO_RATE / 1000);

	if (audio < 0 && (audio = open("/dev/audio", O_WRONLY)) < 0)
		return false;
	if (count > sizeof samples)
		count = sizeof samples;
	if (hz > AUDIO_RATE / 2)
		hz = AUDIO_RATE / 2;
	for (unsigned int i = 0; i < count; i++) {
		phase += hz;
		if (phase >= AUDIO_RATE)
			phase -= AUDIO_RATE;
		samples[i] = phase < AUDIO_RATE / 2 ? AUDIO_HIGH : AUDIO_LOW;
	}
	return write(audio, samples, count) == (ssize_t)count;
}

/* entry is odd: defcode assembles Thumb code and the call enters it so. */
bool
dev_callcode(const uint8_t *entry, const int param[4], int *result)
{
	int (*code)(int, int, int, int) =
	    (int (*)(int, int, int, int))(uintptr_t)entry;

	*result = code(param[0], param[1], param[2], param[3]);
	return true;
}
