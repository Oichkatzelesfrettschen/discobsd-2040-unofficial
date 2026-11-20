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
 * COM1 (serial.c): an 8250 UART at ports 3F8h-3FFh on interrupt line 4,
 * and on its lines a two-button Microsoft serial mouse. pc.c routes the
 * ports, turns the interrupt line's rising edges into requests and moves
 * the mouse from the keyboard.
 */
#ifndef I86_SERIAL_H
#define I86_SERIAL_H

#define SER_BASE	0x3f8u		/* COM1's first port */
#define SER_LINE	4		/* its interrupt controller line */
#define MOUSE_LEFT	1u
#define MOUSE_RIGHT	2u

unsigned ser_in(unsigned port);
void ser_out(unsigned port, unsigned v);
/* Time passes: the next byte from the mouse reaches the receive register
 * a byte time after the last one. now is in milliseconds. */
void ser_poll(unsigned long now);
/* The level of the UART's interrupt output. */
int ser_line(void);
/* One movement in mickeys (right and down positive, each within -128 to
 * 127) with the buttons held. The mouse sends nothing while it has no
 * power. A report that would not fit whole is dropped and its buttons
 * are sent again once a report fits. */
void mouse_report(int dx, int dy, unsigned buttons);

#endif
