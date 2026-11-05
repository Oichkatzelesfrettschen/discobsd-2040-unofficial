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
 * ClockworkPi PicoCalc mainboard V2.0: the pins the Pico header carries
 * and the console device built on them.
 *
 * clockwork_Mainboard_V2.0_Schematic.pdf, connectors J301 and J302,
 * assigns the Pico's pins; sys/arch/rp2040/doc/PICOCALC.md tabulates
 * the whole map and names the vendor sources that agree with it.
 */

#ifndef _PICOCALC_H
#define _PICOCALC_H

/* ST7365P 320x320 panel on SPI1; chip select, D/C and reset are SIO. */
#define	PICOCALC_LCD_SCK	10
#define	PICOCALC_LCD_TX		11
#define	PICOCALC_LCD_RX		12
#define	PICOCALC_LCD_CS		13
#define	PICOCALC_LCD_DC		14
#define	PICOCALC_LCD_RST	15

/*
 * Keyboard controller (an STM32F103 running the vendor's picocalc_keyboard
 * firmware) as an I2C1 target. It also owns the AXP2101 PMU, the LCD and
 * keyboard backlights and power-off; the Pico reaches those only through
 * its registers. The board routes no interrupt line from it to the Pico.
 */
#define	PICOCALC_KBD_SDA	6
#define	PICOCALC_KBD_SCL	7
#define	PICOCALC_KBD_ADDR	0x1f

#ifdef KERNEL

#include <sys/tty.h>

#define	LCDCONS_NTTY	1

int		lcdopen(dev_t dev, int flag, int mode);
int		lcdclose(dev_t dev, int flag, int mode);
int		lcdread(dev_t dev, struct uio *uio, int flag);
int		lcdwrite(dev_t dev, struct uio *uio, int flag);
int		lcdselect(dev_t dev, int rw);
int		lcdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag);
void		lcdputc(dev_t dev, char c);
char		lcdgetc(dev_t dev);
void		lcdcons_init(void);
void		lcdabandon(void);

extern struct tty lcdttys[LCDCONS_NTTY];

/* st7365p.c */
void		st7365p_init(void);
void		st7365p_display_on(void);
void		st7365p_abort(void);

/* picokbd.c */
void		picokbd_init(void);
void		picokbd_poll_start(void);
int		picokbd_getc(void);
int		picokbd_version(void);
u_int		picokbd_errors(void);

/* Bytes a key event produced, delivered by picokbd.c to lcdcons.c. */
void		lcdcons_input(const char *s, int n);

#endif /* KERNEL */

#endif /* !_PICOCALC_H */
