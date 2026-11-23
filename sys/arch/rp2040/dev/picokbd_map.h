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
 * Key events of the PicoCalc keyboard controller, translated to the bytes
 * an xterm sends. picokbd_map.c compiles without kernel headers, so the host
 * test in tests/picocalc runs it directly.
 *
 * The controller firmware (picocalc_keyboard, reg.h and keyboard.h) reports
 * each event from FIFO register 0x09 as a state byte and a key byte. It
 * applies Shift, Caps Lock and the Alt symbol layer itself and repeats a
 * held printable or arrow key as further PRESSED events, but it reports
 * Ctrl only as key 0xA5 changing state, so the host applies Ctrl.
 */

#ifndef _PICOKBD_MAP_H
#define _PICOKBD_MAP_H

#define	PICOKBD_IDLE		0
#define	PICOKBD_PRESSED		1
#define	PICOKBD_HOLD		2
#define	PICOKBD_RELEASED	3

#define	PICOKBD_MAXSEQ		6	/* Longest sequence: ESC [ 1 5 ~. */

/*
 * Translate one event. *ctrl carries the Ctrl state between calls;
 * appcursor is the terminal's cursor-key mode (DECCKM). Returns the number
 * of bytes stored in out, zero for an event that types zero bytes.
 */
int	picokbd_translate(int state, int key, int appcursor, int *ctrl,
	    char out[PICOKBD_MAXSEQ]);

#ifdef LCD_VT
/*
 * The console a key event selects (LCD_VT): Ctrl+F1 is 0 and Ctrl+F2 is 1,
 * taken from the first press only (a repeat selects the same console again,
 * which leaves the state unchanged). Returns -1 for every other event. The caller
 * consumes a selecting event instead of translating it.
 */
int	picokbd_vt_key(int state, int key, int ctrl);
#endif

#endif /* !_PICOKBD_MAP_H */
