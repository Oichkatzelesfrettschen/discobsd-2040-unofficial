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

#ifndef	_MACHINE_PICOCALCVAR_H_
#define	_MACHINE_PICOCALCVAR_H_

/*
 * machdep.picocalc: the PicoCalc keyboard controller's battery, backlight
 * and power-off registers (sys/arch/rp2040/dev/picokbd.c,
 * sys/arch/rp2040/doc/PICOCALC.md). Present in the kernels with the
 * picocalc device (PICOCALC, PICOCALC_LEAN and PICOCALC_LEAN_DIAG); the
 * other kernels answer EOPNOTSUPP.
 */
#ifdef KERNEL
int	picocalc_sysctl(int *, u_int, void *, size_t *, void *, size_t);
#endif

#define	CPU_PICOCALC_BATTERY		1	/* int: percent, read only */
#define	CPU_PICOCALC_CHARGING		2	/* int: 1 while charging, read only */
#define	CPU_PICOCALC_LCD_BACKLIGHT	3	/* int: level 16..240 */
#define	CPU_PICOCALC_KBD_BACKLIGHT	4	/* int: level 0..240 */
#define	CPU_PICOCALC_POWEROFF		5	/* int: write schedules power-off */
/*
 * The panel register test of kernels built with -DLCD_PANELTEST
 * (sys/arch/rp2040/doc/PANELTEST.md); other kernels answer EOPNOTSUPP.
 * Both need the superuser and an open /dev/lcdgfx. Writing panel_variant
 * resets the ST7365P and programs register set N (0 is the shipped
 * sequence); reading it returns the set in force. Reading panel_read
 * returns a struct picocalc_panel_read of the controller's read commands.
 */
#define	CPU_PICOCALC_PANEL_VARIANT	6	/* int: register set in force */
#define	CPU_PICOCALC_PANEL_READ		7	/* struct: register readback */
#define	CPU_PICOCALC_MAXID		8

#define	PANEL_READ_N		20	/* Entries of a readback. */
#define	PANEL_READ_BYTES	5	/* Bytes kept per entry. */

/*
 * One read command: cmd went out with D/CX low, then n bytes were clocked
 * in with D/CX high and the clock at 3.125 MHz (TSCYCR 150 ns, page 77).
 * A nonzero fb means SPI Read Control (FBh, page 246) was set to fb first
 * and cleared after. The bytes are raw: the 24- and 32-bit reads (04h, 09h)
 * carry one dummy clock first (page 94), so their value starts at bit 6 of
 * byte 0.
 */
struct picocalc_panel_ent {
	unsigned char	cmd, fb, n;
	unsigned char	data[PANEL_READ_BYTES];
};
struct picocalc_panel_read {
	int	variant;
	int	nent;
	struct picocalc_panel_ent ent[PANEL_READ_N];
};

#ifndef	KERNEL
#define	CTL_PICOCALC_NAMES { \
	{ 0, 0 }, \
	{ "battery", CTLTYPE_INT }, \
	{ "charging", CTLTYPE_INT }, \
	{ "lcd_backlight", CTLTYPE_INT }, \
	{ "kbd_backlight", CTLTYPE_INT }, \
	{ "poweroff", CTLTYPE_INT }, \
	{ "panel_variant", CTLTYPE_INT }, \
	{ "panel_read", CTLTYPE_STRUCT }, \
}
#endif	/* !KERNEL */

#endif	/* !_MACHINE_PICOCALCVAR_H_ */
