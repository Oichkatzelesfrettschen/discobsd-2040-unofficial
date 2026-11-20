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
 * The /dev/lcdgfx driver: minor LCDGFX_MINOR of the console's character
 * major. lcdcons.c dispatches the entry points of that minor here; the
 * interface is documented in sys/lcdgfx.h.
 */

#ifndef _LCDGFX_H
#define _LCDGFX_H

#define	LCDGFX_MINOR	1

struct uio;

#ifdef DEV_LCDGFX
int	lcdgfx_open(void);
int	lcdgfx_close(void);
int	lcdgfx_write(struct uio *uio);
int	lcdgfx_ioctl(unsigned cmd, char *addr);
int	lcdgfx_isopen(void);
#ifdef LCD_GFX_EXT
int	lcdgfx_read(struct uio *uio);
#else
/* Without the read command the device is write only: end of file. */
static inline int lcdgfx_read(struct uio *uio __attribute__((unused)))
{
	return 0;
}
#endif
#else
#include <sys/errno.h>

/*
 * A kernel configured without "device lcdgfx": minor LCDGFX_MINOR of the
 * console major answers ENXIO and the console never sees a drawer.
 */
static inline int lcdgfx_open(void) { return ENXIO; }
static inline int lcdgfx_close(void) { return 0; }
static inline int lcdgfx_write(struct uio *uio __unused) { return ENXIO; }
static inline int lcdgfx_read(struct uio *uio __unused) { return 0; }
static inline int lcdgfx_ioctl(unsigned cmd __unused, char *addr __unused)
{
	return ENXIO;
}
static inline int lcdgfx_isopen(void) { return 0; }
#endif

/*
 * Supplied by lcdcons.c, or by the host test: hand the panel to a pixel
 * program (on) or take it back and repaint the text (off), holding the
 * console's drawing flag. Entering returns EAGAIN while a drawer holds the
 * flag; leaving always completes.
 */
int	lcdcons_gfx(int on);

#ifdef LCD_GFX_EXT
/*
 * Supplied by lcdcons.c, or by the host test: whether the calling process
 * may read the controller's memory (LCDG_READ). The controller's memory
 * holds the console's screen from before the open, so the reader is the
 * superuser or the console's owner; lcdgfx_reader_ok() is the rule.
 */
int	lcdcons_gfx_reader(void);

/*
 * The console's owner is the user logged in on it: the effective uid of the
 * console session's leader, the login shell, latched at the first check that
 * finds the leader with a nonzero uid. The session is the tty's process
 * group at the first open that sets one (lcdgfx_console_open(), called after
 * ttyopen() with the tty's t_pgrp) and ends at the last close
 * (lcdgfx_console_close()). Job control moves the foreground group and a
 * set-uid program or su in the foreground changes the uid of processes that
 * are not the leader; neither moves the session or the latched uid.
 */
void	lcdgfx_console_open(int tpgrp);
void	lcdgfx_console_close(void);
int	lcdgfx_console_session(void);

/*
 * The rule of lcdcons_gfx_reader(), over the facts it gathers: the caller's
 * effective uid and whether pfind(lcdgfx_console_session()) found a process,
 * with that process's group and effective uid. The superuser reads. Any
 * other caller reads only as the latched owner, and only while the session
 * leader is alive and still leads its group: no session, no such process or
 * a process of another group (a reused pid) leave the memory to the
 * superuser, and so does a console with no login yet (leader uid 0).
 */
int	lcdgfx_reader_ok(unsigned uid, int found, int lpgrp, unsigned luid);
#endif

#ifdef LCD_PANELTEST
/*
 * The panel register test (doc/PANELTEST.md). lcdgfx_panel_set() and
 * lcdgfx_panel_read() return EBUSY unless /dev/lcdgfx is open, so the
 * console draws nothing while they reprogram or read the panel; the last
 * close programs set 0 again when another set is in force. st7365p.c
 * supplies the sets and the readback.
 */
struct picocalc_panel_read;
int	lcdgfx_panel_set(int n);
int	lcdgfx_panel_read(struct picocalc_panel_read *rb);
struct picocalc_panel_scan;
int	lcdgfx_panel_scan(struct picocalc_panel_scan *sc);
int	st7365p_set(int n);
int	st7365p_set_current(void);
void	st7365p_readback(struct picocalc_panel_read *rb);
void	st7365p_scan(struct picocalc_panel_scan *sc);
#endif

#endif /* !_LCDGFX_H */
