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
 * records it here when a session is active and then translates it to xterm
 * bytes exactly as picokbd_translate() does, so the queue adds no path the
 * tty bytes wait on. The file includes no kernel header beyond
 * sys/lcdcons.h and sys/errno.h, and tests/picocalc runs it directly; the
 * kernel glue in lcdcons.c supplies the caller's identity as plain values.
 *
 * Keystrokes include passwords, so nothing is recorded by default. A
 * session starts when a process that holds the console readable
 * (LCDIOC_KEYEVS 1) and belongs to the tty's foreground process group asks
 * for it. It belongs to that process, identified by its proc entry and pid
 * (the pid alone could be reused), and to the open file the request came
 * through; only the owning process may drain or end it. The queue is emptied
 * when the session starts and when it ends, and events are recorded only
 * between the two. A session ends when its owner closes the file
 * (picokbd_evq_fileclose(), called from the last close of that open file),
 * when the owning process exits or execs (picokbd_evq_procexit(); an open
 * file shared with a parent shell outlives the process, so the file close
 * alone would leave the session recording), when the owner asks
 * (LCDIOC_KEYEVS 0), and when the tty's foreground group is no longer the
 * owner's group (picokbd_evq_foreground(), called with the tty's group
 * before every event and by every request). A drain requires the readable
 * descriptor and the foreground group, the checks ttread() applies, but a
 * background caller gets EIO at once instead of SIGTTIN, and a rejected
 * drain leaves the queue as it was.
 *
 * The queue holds LCD_KEYEV_QUEUE events. A full queue drops its oldest
 * event, so the newest state survives, and counts the drop for the next
 * drain. The caller serializes every call: the poller runs from the clock
 * callout and the requests from ioctl at spltty().
 */

#ifndef _PICOKBD_EVQ_H
#define _PICOKBD_EVQ_H

#include <sys/lcdcons.h>

#include "picokbd_map.h"

/*
 * Start a session owned by process `proc` (pid `pid`) through open file
 * `file`. readable is nonzero when the descriptor allows reading; pgrp is
 * the caller's process group and tty_pgrp the tty's foreground group.
 * Returns 0, EBADF (not readable), EIO (not in the foreground group, or the
 * tty has none) or EBUSY (another process or file owns the session). A second
 * start by the owner empties the queue and continues.
 */
int	picokbd_evq_start(const void *file, const void *proc, int pid, int pgrp,
	    int tty_pgrp, int readable);

/* End the session if `file` and the process own it: 0, or EPERM when not. */
int	picokbd_evq_stop(const void *file, const void *proc, int pid);

/*
 * Remove up to LCD_KEYEV_N events, oldest first, and the drop count into
 * out. Returns 0, EBADF (not readable), EIO (not foreground), EINVAL (no
 * session) or EPERM (the caller is not the owning process); only 0 touches
 * the queue.
 */
int	picokbd_evq_drain(struct lcd_keyevs *out, const void *proc, int pid,
	    int pgrp, int tty_pgrp, int readable);

/* The open file `file` is closed: end its session. */
void	picokbd_evq_fileclose(const void *file);

/* Process `proc` with pid `pid` exits or execs: end its session. */
void	picokbd_evq_procexit(const void *proc, int pid);

/* End the session when the tty's foreground group is not the owner's. */
void	picokbd_evq_foreground(int tty_pgrp);

/* Record one controller event if a session is active and the state is an event. */
void	picokbd_evq_put(int state, int key);

/* End any session and empty the queue. */
void	picokbd_evq_reset(void);

/* Record the event, then translate it as picokbd_translate() does. */
int	picokbd_event(int state, int key, int appcursor, int *ctrl,
	    char out[PICOKBD_MAXSEQ]);

#endif /* !_PICOKBD_EVQ_H */
