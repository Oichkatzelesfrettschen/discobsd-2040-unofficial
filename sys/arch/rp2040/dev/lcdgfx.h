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
 * Supplied by lcdcons.c, or by the host test: whether the opening process
 * is the superuser by effective uid. The controller's memory holds the
 * console's screen from before the open, so lcdgfx_open() blanks every line
 * of it for any other opener before the open returns, and only then may
 * that opener select and read a rectangle (LCDG_READ).
 */
int	lcdcons_gfx_root(void);
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
