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
 * The raw key event queue behind LCDIOC_GETKEY (sys/lcdcons.h). The poller
 * of picokbd.c passes every controller event through picokbd_event(), which
 * records it here and then translates it to xterm bytes exactly as
 * picokbd_translate() does, so the queue adds no path the tty bytes wait on.
 * The file includes no kernel header beyond sys/lcdcons.h, and tests/picocalc
 * runs it directly.
 *
 * The queue holds LCD_KEYEV_QUEUE events. A full queue drops its oldest
 * event, so the newest state survives, and counts the drop for the next
 * drain. The caller serializes put and drain: the poller runs from the
 * clock callout and the drain from the ioctl at spltty().
 */

#ifndef _PICOKBD_EVQ_H
#define _PICOKBD_EVQ_H

#include <sys/lcdcons.h>

#include "picokbd_map.h"

/* Record one controller event; a state other than PRESSED, HOLD and RELEASED is no event. */
void	picokbd_evq_put(int state, int key);

/* Remove up to LCD_KEYEV_N events, oldest first, and the drop count into out. */
void	picokbd_evq_drain(struct lcd_keyevs *out);

/* Empty the queue and the drop count. */
void	picokbd_evq_reset(void);

/* Record the event, then translate it as picokbd_translate() does. */
int	picokbd_event(int state, int key, int appcursor, int *ctrl,
	    char out[PICOKBD_MAXSEQ]);

#endif /* !_PICOKBD_EVQ_H */
