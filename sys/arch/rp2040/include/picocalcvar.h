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
 * Reading panel_scan samples the controller's scanline (GSCAN 45h) and
 * returns a struct picocalc_panel_scan from which paneltest computes the
 * frame rate.
 */
#define	CPU_PICOCALC_PANEL_VARIANT	6	/* int: register set in force */
#define	CPU_PICOCALC_PANEL_READ		7	/* struct: register readback */
#define	CPU_PICOCALC_PANEL_SCAN		8	/* struct: scanline sample */
#define	CPU_PICOCALC_MAXID		9

#define	PANEL_READ_N		33	/* Entries of a readback. */
#define	PANEL_READ_BYTES	5	/* Bytes kept per entry. */

/*
 * One read command: cmd went out with D/CX low, then n bytes were clocked
 * in with D/CX high and the clock at 3.125 MHz (TSCYCR 150 ns, page 77).
 * A nonzero fb means SPI Read Control (FBh, page 246) was set to fb first
 * and cleared after. The bytes are raw: the 24- and 32-bit reads (04h, 09h,
 * D3h) carry one dummy clock first (page 94), so their value starts at
 * bit 6 of byte 0.
 *
 * how names the Command Table 2 read variant. PANEL_RD_MARK unlocks
 * command 2 (CSCON C3h, 96h) and writes the mark values below just before
 * the read, so every marked read starts from the same known register
 * contents whatever an earlier read clocked into them. PANEL_RD_FB_FIRST
 * sends FBh before that unlock instead of after it; PANEL_RD_LOCK locks
 * command 2 (3Ch, 69h) between the mark and FBh; PANEL_RD_ONE_CS sends FBh
 * in the read's own chip select.
 */
#define	PANEL_RD_MARK		0x01
#define	PANEL_RD_FB_FIRST	0x02
#define	PANEL_RD_LOCK		0x04
#define	PANEL_RD_ONE_CS		0x08
#define	PANEL_MARK_C5		0x2a	/* VCMPCTL: VCOM inside 00h-3Fh */
#define	PANEL_MARK_B1_0		0x50	/* FRMCTR1: FRS 5, DIVA 0 */
#define	PANEL_MARK_B1_1		0x1c	/* FRMCTR1: RTNA 28 (40 Hz) */

struct picocalc_panel_ent {
	unsigned char	cmd, fb, n, how;
	unsigned char	data[PANEL_READ_BYTES];
};
struct picocalc_panel_read {
	int	variant;
	int	nent;
	struct picocalc_panel_ent ent[PANEL_READ_N];
};

/*
 * A scanline sample: GSCAN read back to back at 3.125 MHz, each read
 * timed with the 1 us system timer, until nine wraps or 400 ms. A wrap is
 * a sample whose line, decoded as bits 22-7 of the three reply bytes (one
 * dummy clock first), lies more than 64 below the one before and whose
 * successor continues from it; a single sample off the scan's position is
 * a misread of the moving counter and counts as rejected (st7365p_scan()).
 * A decoded line of 2048 or more counts as bad and is skipped. Times are
 * microseconds from the first read; a wrap's time is that of the sample
 * that saw it, so it lags the wrap by at most max_gap_us. raw_max is the
 * largest raw 24-bit reply among the samples taken as the scan's position, so a reader can decode
 * it either way.
 */
struct picocalc_panel_scan {
	unsigned int	samples;	/* GSCAN reads taken */
	unsigned int	wraps;		/* scanline wraps seen */
	unsigned int	bad;		/* reads decoded past any line */
	unsigned int	first_us;	/* time of the first wrap */
	unsigned int	last_us;	/* time of the last wrap */
	unsigned int	elapsed_us;	/* time of the last read */
	unsigned int	max_gap_us;	/* longest time between two reads */
	unsigned int	raw_max;	/* largest good raw reply */
	unsigned int	rejected;	/* samples off the scan's position */
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
	{ "panel_scan", CTLTYPE_STRUCT }, \
}
#endif	/* !KERNEL */

#endif	/* !_MACHINE_PICOCALCVAR_H_ */
