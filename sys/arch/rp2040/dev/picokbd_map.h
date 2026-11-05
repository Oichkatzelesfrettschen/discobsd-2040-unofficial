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
 * Key events of the PicoCalc keyboard controller, translated to the bytes
 * an xterm sends. picokbd_map.c includes no kernel header, so the host
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
 * of bytes stored in out, zero for an event that types nothing.
 */
int	picokbd_translate(int state, int key, int appcursor, int *ctrl,
	    char out[PICOKBD_MAXSEQ]);

#endif /* !_PICOKBD_MAP_H */
