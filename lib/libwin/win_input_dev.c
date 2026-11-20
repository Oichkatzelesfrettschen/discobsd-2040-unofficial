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
 * libwin input, descriptor side: the raw key event session of the console
 * (LCDIOC_KEYEVS and LCDIOC_GETKEY, sys/sys/lcdcons.h), the tty fallback and
 * the wait that feeds both into the manager of win_input.c.
 *
 * select(2) on the console tty is ready when the tty's input queue holds a
 * byte (lcdselect() calls ttyselect(), sys/arch/rp2040/dev/lcdcons.c).
 * picokbd_translate() (picokbd_map.c) returns no byte for a HOLD or a
 * RELEASED event or for a modifier key, and kbd_poll() (picokbd.c) queues
 * the raw event without waking the tty, so select wakes on a press that
 * types a byte and on nothing else. A raw session therefore polls the event
 * queue every WIN_POLL_MS and also on every wake. Each PRESSED event of a
 * typing key also leaves its bytes in the tty queue, so a wake reads and
 * discards them: left in place they keep select ready and the loop spins.
 * Waking select on every raw event (a selwakeup() in picokbd_evq_put()) is
 * a kernel change, recorded as open and not made here.
 *
 * The timeout is charged on every iteration against gettimeofday(), which
 * the target answers from the 1 ms tick (kern_time.c: lbolt times usechz at
 * HZ 1000), so the call returns within timeout_ms plus one poll interval
 * whatever the descriptor reports; a source that stays readable without
 * completing an event (an endless incomplete escape sequence) returns the
 * bytes fed at the deadline, with the partial sequence still pending.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/lcdcons.h>
#include <errno.h>
#include <unistd.h>

#include "win_input.h"

#define READ_CHUNK	16

int
win_input_open(struct win_input_src *src, int fd)
{
	int on = 1;

	src->fd = fd;
	src->raw = 0;
	if (ioctl(fd, LCDIOC_KEYEVS, &on) < 0)
		return -1;
	src->raw = 1;
	return 0;
}

void
win_input_close(struct win_input_src *src)
{
	int off = 0;

	if (src->raw)
		(void)ioctl(src->fd, LCDIOC_KEYEVS, &off);
	src->raw = 0;
}

/* Everything the kernel holds now: the events, or the bytes. Returns the count or -1. */
static int
drain(struct win_input_src *src, struct win_input *in, struct win_scene *s)
{
	struct lcd_keyevs ev;
	int i, fed = 0;

	do {
		if (ioctl(src->fd, LCDIOC_GETKEY, &ev) < 0) {
			src->raw = 0;
			return -1;
		}
		if (ev.lost != 0)
			win_input_lost(in);
		for (i = 0; i < ev.n && i < LCD_KEYEV_N; i++) {
			if (win_input_raw(in, s, ev.ev[i].state, ev.ev[i].key) < 0) {
				win_input_lost(in);
				return -2;
			}
			fed++;
		}
	} while (ev.n == LCD_KEYEV_N);
	return fed;
}

int
win_input_wait(struct win_input_src *src, struct win_input *in,
    struct win_scene *s, int timeout_ms)
{
	unsigned char buf[READ_CHUNK];
	struct timeval tv, t0;
	fd_set rd;
	long elapsed = 0;
	int slice, n, i, r, fed = 0;

	(void)gettimeofday(&t0, NULL);
	for (;;) {
		slice = timeout_ms < 0 ? -1 : (int)(timeout_ms - elapsed);
		if ((src->raw || in->esc != 0) && (slice < 0 || slice > WIN_POLL_MS))
			slice = WIN_POLL_MS;
		FD_ZERO(&rd);
		FD_SET(src->fd, &rd);
		tv.tv_sec = slice < 0 ? 3600 : slice / 1000;
		tv.tv_usec = slice < 0 ? 0 : (slice % 1000) * 1000;
		r = select(src->fd + 1, &rd, NULL, NULL, &tv);
		if (r < 0 && errno != EINTR)
			return -1;
		if (r > 0) {
			n = (int)read(src->fd, buf, sizeof(buf));
			if (n < 0)
				return -1;
			if (!src->raw)
				for (i = 0; i < n; i++) {
					if (win_input_byte(in, s, buf[i]) < 0) {
						win_input_lost(in);
						return -1;
					}
					fed++;
				}
		} else if (r == 0 && !src->raw && in->esc != 0) {
			if (win_input_idle(in, s) < 0) {
				win_input_lost(in);
				return -1;
			}
			fed++;
		}
		if (src->raw) {
			n = drain(src, in, s);
			if (n == -2)
				return -1;	/* A dispatch failure; the session stays. */
			if (n < 0)
				return -1;
			fed += n;
		}
		if (fed > 0 && in->esc == 0)
			return fed;
		if (timeout_ms >= 0) {
			(void)gettimeofday(&tv, NULL);
			elapsed = (long)(tv.tv_sec - t0.tv_sec) * 1000 +
			    (long)(tv.tv_usec - t0.tv_usec) / 1000;
			if (elapsed >= timeout_ms)
				return fed;
		}
	}
}
