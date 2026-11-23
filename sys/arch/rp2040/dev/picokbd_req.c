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
kbdreq_lcd_level(int v)
{
	if (v <= KBD_LCD_MIN)
		return KBD_LCD_MIN;
	if (v >= KBD_LCD_MAX)
		return KBD_LCD_MAX;
	return (v / KBD_LCD_STEP) * KBD_LCD_STEP;
}

int
kbdreq_kbd_level(int v)
{
	if (v < KBD_KBD_STEP)
		return 0;
	if (v >= KBD_KBD_MAX)
		return KBD_KBD_MAX;
	return (v / KBD_KBD_STEP) * KBD_KBD_STEP;
}

int
kbdreq_level_for(int reg, int v)
{
	if (reg == KBD_REG_BKL)
		return kbdreq_lcd_level(v);
	if (reg == KBD_REG_BK2)
		return kbdreq_kbd_level(v);
	return kbdreq_level(v);
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
