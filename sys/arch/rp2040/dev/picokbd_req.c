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

/* Register-request mailbox for the keyboard controller; see picokbd_req.h. */

#include "picokbd_req.h"

int
kbdreq_submit(struct kbdreq *q, int reg, int write, int val)
{
	if (q->state != KR_IDLE)
		return -1;
	q->reg = (unsigned char)(reg & ~KBD_WRITE_BIT);
	q->write = write != 0;
	q->val = (unsigned char)val;
	q->tries = 0;
	q->err = KRE_NONE;
	q->state = KR_QUEUED;
	return 0;
}

int
kbdreq_next(struct kbdreq *q, unsigned char out[2])
{
	if (q->state != KR_QUEUED)
		return 0;
	q->state = KR_SENT;
	if (!q->write) {
		out[0] = q->reg;
		return 1;
	}
	out[0] = (unsigned char)(q->reg | KBD_WRITE_BIT);
	out[1] = q->val;
	return 2;
}

void
kbdreq_abort(struct kbdreq *q)
{
	if (q->state != KR_SENT)
		return;
	if (++q->tries >= KBDREQ_TRIES) {
		q->err = KRE_IO;
		q->state = KR_FAILED;
	} else
		q->state = KR_QUEUED;
}

void
kbdreq_done(struct kbdreq *q, int b0, int b1)
{
	if (q->state != KR_SENT)
		return;
	/* The firmware echoes the register number; anything else is stale. */
	if (b0 != q->reg) {
		kbdreq_abort(q);
		return;
	}
	q->rx[0] = (unsigned char)b0;
	q->rx[1] = (unsigned char)b1;
	q->state = KR_DONE;
}

void
kbdreq_expire(struct kbdreq *q)
{
	if (q->state != KR_QUEUED && q->state != KR_SENT)
		return;
	q->err = KRE_TIMEOUT;
	q->state = KR_FAILED;
}

int
kbdreq_pending(const struct kbdreq *q)
{
	return q->state == KR_QUEUED || q->state == KR_SENT;
}

int
kbdreq_finish(struct kbdreq *q, int *val)
{
	int err = KRE_IO;

	if (q->state == KR_DONE) {
		*val = q->rx[1];
		err = KRE_NONE;
	} else if (q->state == KR_FAILED)
		err = q->err;
	q->state = KR_IDLE;
	return err;
}

int
kbdreq_level(int v)
{
	return v < 0 ? 0 : v > KBD_LEVEL_MAX ? KBD_LEVEL_MAX : v;
}

int
kbdreq_off_seconds(int v)
{
	return v < KBD_OFF_MIN ? KBD_OFF_MIN : v > 255 ? 255 : v;
}

int
kbdreq_battery_percent(int v)
{
	return v & ~KBD_BAT_CHARGING;
}

int
kbdreq_battery_charging(int v)
{
	return (v & KBD_BAT_CHARGING) != 0;
}
