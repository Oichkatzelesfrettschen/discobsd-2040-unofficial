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
 * COM1 and a Microsoft serial mouse (serial.h).
 *
 * The UART has the 8250's registers: the receive and transmit buffer and
 * the divisor latch at offset 0, the interrupt enable register, the
 * interrupt identification, line control, modem control, line status,
 * modem status and scratch registers. It interrupts for received data
 * alone: the identification reads 04h while a byte waits and the receive
 * interrupt is enabled, otherwise 01h, and the interrupt output follows
 * that condition only while OUT2 (modem control bit 3) is set, as the PC's
 * serial adapter gates it. The transmitter is always empty, and a byte
 * written goes nowhere except in loopback mode, where it is received and
 * the modem status inputs read the modem control outputs.
 *
 * The mouse draws its power from RTS. When RTS rises it starts again and
 * sends 'M', the Microsoft mouse's identification, which drivers wait for
 * after lowering and raising the line. A report is three bytes of seven
 * bits: 40h with the buttons and the high bits of the movement, then the
 * low six bits of X and of Y. The bytes arrive one byte time apart, 7.5 ms
 * at the 1200 bit/s of the protocol, rounded up, and a byte waits in the
 * receive register until the guest reads it, so the stream never loses a
 * byte and stays aligned on reports. The divisor and the line format are
 * kept and not checked.
 */
#include <stdint.h>

#include "serial.h"

#define BYTE_MS		8		/* one byte at 1200 bit/s */
#define QN		48		/* bytes on the way: sixteen reports */

static uint8_t q[QN];
static unsigned qh, qn;
static uint8_t rbr, ier, lcr, mcr, scr, dll = 0x60, dlm;
static int ready;			/* the receive register holds a byte */
static unsigned long now_ms, due;	/* the next byte's earliest time */

static void
send(const uint8_t *b, unsigned n)
{
	unsigned i;

	if (QN - qn < n)
		return;
	/* A stream that was idle starts a byte time from now. */
	if (qn == 0 && (long)(now_ms + BYTE_MS - due) > 0)
		due = now_ms + BYTE_MS;
	for (i = 0; i < n; i++)
		q[(qh + qn++) % QN] = b[i];
}

void
ser_poll(unsigned long now)
{
	now_ms = now;
	if (ready || qn == 0 || (long)(now - due) < 0)
		return;
	rbr = q[qh];
	qh = (qh + 1) % QN;
	qn--;
	ready = 1;
	due = now + BYTE_MS;
}

int
ser_line(void)
{
	return (ready && (ier & 1) && (mcr & 8));
}

unsigned
ser_in(unsigned port)
{
	unsigned v = 0xff;

	switch (port - SER_BASE) {
	case 0:
		if (lcr & 0x80)
			return (dll);
		v = rbr;
		ready = 0;
		break;
	case 1:
		v = (lcr & 0x80) ? dlm : ier;
		break;
	case 2:
		v = ready && (ier & 1) ? 0x04 : 0x01;
		break;
	case 3:
		v = lcr;
		break;
	case 4:
		v = mcr;
		break;
	case 5:
		v = 0x60 | (unsigned)ready;
		break;
	case 6:
		/* Loopback: CTS, DSR, RI and DCD read RTS, DTR, OUT1 and
		 * OUT2. The mouse drives none of them. */
		v = (mcr & 0x10) ? ((mcr & 2u) << 3 | (mcr & 1u) << 5 |
		    (mcr & 0xcu) << 4) : 0;
		break;
	case 7:
		v = scr;
		break;
	}
	return (v);
}

void
ser_out(unsigned port, unsigned v)
{
	static const uint8_t ident = 'M';
	unsigned old;

	v &= 0xff;
	switch (port - SER_BASE) {
	case 0:
		if (lcr & 0x80)
			dll = (uint8_t)v;
		else if (mcr & 0x10) {
			rbr = (uint8_t)v;
			ready = 1;
		}
		break;
	case 1:
		if (lcr & 0x80)
			dlm = (uint8_t)v;
		else
			ier = (uint8_t)(v & 0x0f);
		break;
	case 3:
		lcr = (uint8_t)v;
		break;
	case 4:
		old = mcr;
		mcr = (uint8_t)(v & 0x1f);
		if ((old ^ mcr) & 2) {
			/* Power off, or on with the identification. */
			qh = qn = 0;
			if (mcr & 2)
				send(&ident, 1);
		}
		break;
	case 7:
		scr = (uint8_t)v;
		break;
	}
}

void
mouse_report(int dx, int dy, unsigned buttons)
{
	uint8_t b[3];
	unsigned x = (unsigned)dx & 0xff, y = (unsigned)dy & 0xff;

	if (!(mcr & 2) || (mcr & 0x10))
		return;
	b[0] = (uint8_t)(0x40 | ((buttons & MOUSE_LEFT) ? 0x20 : 0) |
	    ((buttons & MOUSE_RIGHT) ? 0x10 : 0) | (y >> 6) << 2 | x >> 6);
	b[1] = (uint8_t)(x & 0x3f);
	b[2] = (uint8_t)(y & 0x3f);
	send(b, 3);
}
