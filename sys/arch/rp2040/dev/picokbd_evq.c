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
 * The raw key event queue; picokbd_evq.h states the contract.
 *
 * head and tail count events taken in and removed since the reset and wrap
 * modulo 2^32, so head - tail is the fill and the slot of event k is
 * k % LCD_KEYEV_QUEUE.
 */

#include "picokbd_evq.h"

_Static_assert((LCD_KEYEV_QUEUE & (LCD_KEYEV_QUEUE - 1)) == 0,
    "the queue index is a mask");
_Static_assert(LCD_KEY_PRESSED == PICOKBD_PRESSED &&
    LCD_KEY_HOLD == PICOKBD_HOLD && LCD_KEY_RELEASED == PICOKBD_RELEASED,
    "the user states are the controller's states");

static struct lcd_keyev queue[LCD_KEYEV_QUEUE];
static unsigned head, tail;
static unsigned lost;			/* Drops since the last drain, at most 255. */

void
picokbd_evq_put(int state, int key)
{
	struct lcd_keyev *ev;

	if (state != PICOKBD_PRESSED && state != PICOKBD_HOLD &&
	    state != PICOKBD_RELEASED)
		return;
	if (head - tail >= LCD_KEYEV_QUEUE) {
		tail++;
		if (lost < 255)
			lost++;
	}
	ev = &queue[head++ & (LCD_KEYEV_QUEUE - 1)];
	ev->state = (unsigned char)state;
	ev->key = (unsigned char)key;
}

void
picokbd_evq_drain(struct lcd_keyevs *out)
{
	unsigned n = 0;

	while (n < LCD_KEYEV_N && tail != head)
		out->ev[n++] = queue[tail++ & (LCD_KEYEV_QUEUE - 1)];
	out->n = (unsigned char)n;
	out->lost = (unsigned char)lost;
	lost = 0;
}

void
picokbd_evq_reset(void)
{
	head = tail = 0;
	lost = 0;
}

int
picokbd_event(int state, int key, int appcursor, int *ctrl,
    char out[PICOKBD_MAXSEQ])
{
	picokbd_evq_put(state, key);
	return picokbd_translate(state, key, appcursor, ctrl, out);
}
