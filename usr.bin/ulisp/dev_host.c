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
 * The uLisp devices of a development host: the display is a frame buffer
 * in memory, which save-bmp writes out, and there are no pins, no sound
 * and no machine code. Analogue pins form a loopback: analogwrite stores
 * the duty and analogread returns the 12-bit count of the voltage that duty
 * averages, so a transcript shows the pin and the value that crossed the
 * platform interface.
 */
#include "platform.h"

static uint16_t frame[GFX_HEIGHT][GFX_WIDTH];

void
dev_fill(int x0, int y0, int x1, int y1, uint16_t color)
{
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
			frame[y][x] = color;
}

int
dev_readpixel(int x, int y)
{
	return frame[y][x];
}

void
dev_release(void)
{
}

void
dev_pinmode(int pin, int mode)
{
	(void)pin;
	(void)mode;
}

int
dev_digitalread(int pin)
{
	(void)pin;
	return 0;
}

void
dev_digitalwrite(int pin, int level)
{
	(void)pin;
	(void)level;
}

static int duty_written[32];

int
dev_analogread(int pin)
{
	if (pin < 0 || pin > 31)
		return 0;
	return duty_written[pin] * 4095 / 255;
}

void
dev_analogwrite(int pin, int duty)
{
	if (pin >= 0 && pin <= 31)
		duty_written[pin] = duty;
}

bool
dev_tone(unsigned int hz, unsigned long ms)
{
	(void)hz;
	(void)ms;
	return false;
}

bool
dev_callcode(const uint8_t *entry, const int param[4], int *result)
{
	(void)entry;
	(void)param;
	(void)result;
	return false;
}
