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
 * One register request to the PicoCalc keyboard controller, carried by the
 * split-phase poller of picokbd.c in place of a FIFO read.
 *
 * The controller's register protocol (picocalc_keyboard receiveEvent): a
 * read is the register number as one byte; a write is the register number
 * with bit 7 set followed by one value byte. Either exchange is answered by
 * a two-byte read that returns (register number, value), so a write reads
 * back the value the firmware kept.
 *
 * The mailbox holds one request. picokbd.c's write phase asks
 * kbdreq_next() whether a request waits and sends it instead of register
 * 0x09; the collect phase hands the two reply bytes to kbdreq_done(), and
 * a fault in the exchange goes to kbdreq_abort(). This file includes no
 * kernel header, so tests/picocalc links it against a model controller.
 */

#ifndef _PICOKBD_REQ_H
#define _PICOKBD_REQ_H

#define	KBD_REG_BKL		0x05	/* LCD backlight level. */
#define	KBD_REG_BK2		0x0a	/* Keyboard backlight level. */
#define	KBD_REG_BAT		0x0b	/* Battery percent, bit 7 charging. */
#define	KBD_REG_OFF		0x0e	/* Power-off countdown in seconds. */

#define	KBD_WRITE_BIT		0x80
#define	KBD_BAT_CHARGING	0x80
#define	KBD_LEVEL_MAX		240	/* Firmware ceiling for both lights. */
#define	KBD_OFF_MIN		6	/* Firmware floor for the countdown. */

/* Mailbox states. */
#define	KR_IDLE			0	/* Free. */
#define	KR_QUEUED		1	/* Waiting for a write phase. */
#define	KR_SENT			2	/* Exchange in flight. */
#define	KR_DONE			3	/* Reply stored; kbdreq_value() valid. */
#define	KR_FAILED		4	/* Faults or timeout; kbdreq_error(). */

/* Failure causes kbdreq_error() reports. */
#define	KRE_NONE		0
#define	KRE_IO			1	/* Retries exhausted or reply mismatch. */
#define	KRE_TIMEOUT		2	/* The caller's deadline expired. */

#define	KBDREQ_TRIES		3	/* Exchanges per request. */

struct kbdreq {
	unsigned char	state;
	unsigned char	reg;		/* Register number, bit 7 clear. */
	unsigned char	write;
	unsigned char	val;		/* Value to write. */
	unsigned char	tries;
	unsigned char	err;
	unsigned char	rx[2];		/* Reply (register, value). */
};

/* Queue a request; -1 when the mailbox is not idle. */
int	kbdreq_submit(struct kbdreq *q, int reg, int write, int val);

/*
 * The bytes the next write phase sends: 0 when no request waits, else 1
 * (a read) or 2 (a write), with the request now in flight.
 */
int	kbdreq_next(struct kbdreq *q, unsigned char out[2]);

/* The reply of the exchange in flight; ignored unless a request is in flight. */
void	kbdreq_done(struct kbdreq *q, int b0, int b1);

/* A fault ended the exchange in flight: retry, or fail after KBDREQ_TRIES. */
void	kbdreq_abort(struct kbdreq *q);

/* The caller's deadline passed with the request queued or in flight. */
void	kbdreq_expire(struct kbdreq *q);

/* True while the request is queued or in flight. */
int	kbdreq_pending(const struct kbdreq *q);

/*
 * Finish a request: 0 and the reply value, or a KRE_ cause, and the mailbox
 * returns to idle. Valid in the done and failed states.
 */
int	kbdreq_finish(struct kbdreq *q, int *val);

/* Value encodings. */
int	kbdreq_level(int v);		/* Clamp a light level to 0..240. */
int	kbdreq_off_seconds(int v);	/* Clamp a countdown to 6..255. */
int	kbdreq_battery_percent(int v);
int	kbdreq_battery_charging(int v);

#endif /* !_PICOKBD_REQ_H */
