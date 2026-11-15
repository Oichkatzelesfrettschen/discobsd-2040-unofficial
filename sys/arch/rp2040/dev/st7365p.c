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
 * The PicoCalc's 320x320 ST7365P panel on SPI1, driven write-only as the
 * three cell primitives lcdterm.c draws through and the pixel primitives
 * lcdgfx.c draws through.
 *
 * SPI1 is the RP2040's PL022 (datasheet 4.4) in Motorola mode 0 with
 * eight-bit frames. clk_peri runs at BUS_KHZ, 125 MHz, and a prescale of
 * 2 with SCR 4 gives 12.5 MHz, an 80 ns clock, inside the 66 ns minimum
 * write cycle (TSCYCW) of ST7365P_SPEC_V1.0 page 54. Chip select, D/C
 * and reset are SIO outputs so that select stays low across a command
 * and its parameters. D/C changes only once the PL022 reports idle,
 * because the panel samples it with the last bit of each byte.
 *
 * Pixels go out as RGB666 in three bytes (COLMOD 66h), the format every
 * vendor driver for this board uses; the ST7365P also accepts RGB565 over
 * four-wire SPI (page 96), which would cut a glyph from 180 to 120 bytes.
 * The panel shows 320 of the controller's 480 lines, so the vertical
 * scroll definition is a 320-line scroll area with a 160-line bottom
 * fixed area, which sums to 480 as page 184 requires, and with MADCTL MV
 * clear as scrolling requires. The register values come from the
 * vendor's PicoMite ILI9488P (320x320) initialization for this panel,
 * with three deliberate differences that PICOCALC.md lists: the command
 * E9h, absent from the ST7365P command table, is not sent; the scroll
 * definition is programmed before the display turns on; and the display
 * turns on only after lcdterm has cleared every cell, so power-on RAM
 * contents never show.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include <rp2040/dev/lcdterm.h>
#include <rp2040/dev/lcdfont.h>

#define	SPI1_BASE		0x40040000UL
#define	SSPCR0			0x00
#define	SSPCR1			0x04
#define	SSPDR			0x08
#define	SSPSR			0x0c
#define	SSPCPSR			0x10
#define	SSPICR			0x20

#define	SSPCR0_DSS_8		0x7		/* Eight-bit frames. */
#define	SSPCR0_SCR(n)		((u_int)(n) << 8)
#define	SSPCR1_SSE		(1UL << 1)
#define	SSPSR_TNF		(1UL << 1)
#define	SSPSR_RNE		(1UL << 2)
#define	SSPSR_BSY		(1UL << 4)
#define	SSPICR_RORIC		(1UL << 0)

#define	LCD_PRESCALE		2
#define	LCD_SCR			4		/* 125 / (2 * 5) == 12.5 MHz. */

#define	CMD_SLPOUT		0x11
#define	CMD_INVON		0x21
#define	CMD_DISPON		0x29
#define	CMD_CASET		0x2a
#define	CMD_RASET		0x2b
#define	CMD_RAMWR		0x2c
#define	CMD_VSCRDEF		0x33
#define	CMD_MADCTL		0x36
#define	CMD_VSCSAD		0x37
#define	CMD_COLMOD		0x3a

#define	MADCTL_MX_BGR		0x48		/* Column mirror, BGR panel. */
#define	COLMOD_RGB666		0x66

#define	LCD_LINES		320		/* Visible lines. */
#define	LCD_GRAM_LINES		480		/* Controller lines. */

#define	SPI_REG(off)		PC_REG32(SPI1_BASE + (off))

/*
 * Sixteen colors, R, G, B; the panel keeps the upper six bits of each.
 * 0-7 are the SGR 30-37 colors, 8-15 their bold variants.
 */
const u_char lcd_palette[16][3] = {
	{   0,   0,   0 }, { 205,   0,   0 }, {   0, 205,   0 },
	{ 205, 205,   0 }, {  60,  90, 238 }, { 205,   0, 205 },
	{   0, 205, 205 }, { 200, 200, 200 },
	{ 127, 127, 127 }, { 255,  60,  60 }, {  60, 255,  60 },
	{ 255, 255,  60 }, { 100, 120, 255 }, { 255,  60, 255 },
	{  60, 255, 255 }, { 255, 255, 255 },
};

/*
 * The vendor's ILI9488P initialization: gamma, power, VCOM, frame rate,
 * inversion, display function, entry mode and adjust control. Each entry
 * is a command byte, a parameter count, then the parameters.
 */
static const u_char init_seq[] = {
	0xe0, 15, 0x00, 0x03, 0x09, 0x08, 0x16, 0x0a, 0x3f, 0x78,
		  0x4c, 0x09, 0x0a, 0x08, 0x16, 0x1a, 0x0f,
	0xe1, 15, 0x00, 0x16, 0x19, 0x03, 0x0f, 0x05, 0x32, 0x45,
		  0x46, 0x04, 0x0e, 0x0d, 0x35, 0x37, 0x0f,
	0xc0, 2, 0x17, 0x15,
	0xc1, 1, 0x41,
	0xc5, 3, 0x00, 0x12, 0x80,
	CMD_MADCTL, 1, MADCTL_MX_BGR,
	CMD_COLMOD, 1, COLMOD_RGB666,
	0xb0, 1, 0x00,
	0xb1, 1, 0xa0,
	CMD_INVON, 0,
	0xb4, 1, 0x02,
	0xb6, 3, 0x02, 0x02, 0x3b,
	0xb7, 1, 0xc6,
	0xf7, 4, 0xa9, 0x51, 0x2c, 0x82,
};

static void
spi_wait_idle(void)
{
	while (SPI_REG(SSPSR) & SSPSR_BSY)
		continue;
	while (SPI_REG(SSPSR) & SSPSR_RNE)
		(void)SPI_REG(SSPDR);
	SPI_REG(SSPICR) = SSPICR_RORIC;
}

static void
spi_put(u_int b)
{
	while ((SPI_REG(SSPSR) & SSPSR_TNF) == 0)
		continue;
	SPI_REG(SSPDR) = b;
}

/* Send a command byte; the caller holds chip select low. */
static void
lcd_command(u_int cmd)
{
	spi_wait_idle();
	pc_gpio_put(PICOCALC_LCD_DC, 0);
	spi_put(cmd);
	spi_wait_idle();
	pc_gpio_put(PICOCALC_LCD_DC, 1);
}

static void
lcd_select(void)
{
	pc_gpio_put(PICOCALC_LCD_CS, 0);
}

static void
lcd_deselect(void)
{
	spi_wait_idle();
	pc_gpio_put(PICOCALC_LCD_CS, 1);
}

static void
put16(u_int v)
{
	spi_put(v >> 8);
	spi_put(v & 0xff);
}

/* Open a write window over pixel columns x0..x1 and lines y0..y1. */
static void
lcd_window(u_int x0, u_int y0, u_int x1, u_int y1)
{
	lcd_command(CMD_CASET);
	put16(x0);
	put16(x1);
	lcd_command(CMD_RASET);
	put16(y0);
	put16(y1);
	lcd_command(CMD_RAMWR);
}

static void
put_pixel(const u_char *rgb)
{
	spi_put(rgb[0]);
	spi_put(rgb[1]);
	spi_put(rgb[2]);
}

_Static_assert(LCDFONT_HEIGHT == LT_GLYPH_ROWS,
    "a bank glyph and an ASCII glyph share the nine-row draw loop");

void
lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{
	const u_char *fg = lcd_palette[LT_ATTR_FG(attr)];
	const u_char *bg = lcd_palette[LT_ATTR_BG(attr)];
	const u_char *glyph;
	u_int x = (u_int)col * LT_CELL_W, y = (u_int)prow * LT_CELL_H;
	u_int bits;
	int row, px;

	glyph = lcdterm_glyph(ch);
	if (glyph == NULL) {
		if (ch < LCDFONT_FIRST || ch >= LCDFONT_FIRST + LCDFONT_NCHARS)
			ch = '?';
		glyph = lcdfont[ch - LCDFONT_FIRST];
	}

	lcd_select();
	lcd_window(x, y, x + LT_CELL_W - 1, y + LT_CELL_H - 1);
	for (row = 0; row < LT_CELL_H; row++) {
		if (row < LCDFONT_HEIGHT)
			bits = glyph[row];
		else if (attr & LT_ATTR_UL)
			bits = 0xfc;
		else	/* A bank glyph repeats its last row; see lcdterm.h. */
			bits = ch >= LT_GLYPH_FIRST ? glyph[LCDFONT_HEIGHT - 1] : 0;
		for (px = 0; px < LT_CELL_W; px++, bits <<= 1)
			put_pixel((bits & 0x80) ? fg : bg);
	}
	lcd_deselect();
}

void
lcd_fill_cells(int prow, int col, int n, unsigned attr)
{
	const u_char *bg = lcd_palette[LT_ATTR_BG(attr)];
	u_int x = (u_int)col * LT_CELL_W, y = (u_int)prow * LT_CELL_H;
	u_int count = (u_int)n * LT_CELL_W * LT_CELL_H;

	if (n <= 0)
		return;
	lcd_select();
	lcd_window(x, y, x + (u_int)n * LT_CELL_W - 1, y + LT_CELL_H - 1);
	while (count-- > 0)
		put_pixel(bg);
	lcd_deselect();
}

void
lcd_gfx_scroll(int line)
{
	lcd_select();
	lcd_command(CMD_VSCSAD);
	put16((u_int)line);
	lcd_deselect();
}

void
lcd_set_top(int prow)
{
	lcd_gfx_scroll(prow * LT_CELL_H);
}

/*
 * The pixel primitives of lcdgfx.c. A transfer is lcd_gfx_window(), the
 * pixel bytes and lcd_gfx_end(); chip select stays low in between, so no
 * other drawer may touch the panel meanwhile (lcdterm.c draws nothing while
 * lcdgfx.c owns it, and st7365p_abort() ends a transfer a fault abandoned).
 */
void
lcd_gfx_window(int x0, int y0, int x1, int y1)
{
	lcd_select();
	lcd_window((u_int)x0, (u_int)y0, (u_int)x1, (u_int)y1);
}

void
lcd_gfx_byte(unsigned b)
{
	spi_put(b);
}

void
lcd_gfx_end(void)
{
	lcd_deselect();
}

/* VSCRDEF: a scroll area of the controller's lines that the fixed areas leave. */
void
lcd_gfx_scrdef(int top, int bottom)
{
	lcd_select();
	lcd_command(CMD_VSCRDEF);
	put16((u_int)top);
	put16((u_int)(LCD_GRAM_LINES - top - bottom));
	put16((u_int)bottom);
	lcd_deselect();
}

void
lcd_gfx_colmod(unsigned fmt)
{
	lcd_select();
	lcd_command(CMD_COLMOD);
	spi_put(fmt);
	lcd_deselect();
}

/* The two columns right of the last cell, which no cell draw reaches. */
static void
clear_margin(void)
{
	int n;

	lcd_select();
	lcd_window(LT_COLS * LT_CELL_W, 0, 319, LCD_LINES - 1);
	for (n = (320 - LT_COLS * LT_CELL_W) * LCD_LINES; n > 0; n--)
		put_pixel(lcd_palette[0]);
	lcd_deselect();
}

/*
 * Return the panel to the configuration lcdterm.c draws in: RGB666, the
 * 320-line scroll area with its 160-line bottom fixed area, and a margin a
 * pixel program may have drawn on cleared. The scroll start is the
 * caller's: lcdterm_repaint() sets it from the ring.
 */
void
lcd_gfx_reset(void)
{
	lcd_gfx_colmod(COLMOD_RGB666);
	lcd_gfx_scrdef(0, LCD_GRAM_LINES - LCD_LINES);
	clear_margin();
}

/* Send a table of command byte, parameter count, parameters; select held. */
static void
run_seq(const u_char *p, u_int len)
{
	const u_char *end = p + len;
	u_int n;

	while (p < end) {
		lcd_command(*p++);
		for (n = *p++; n > 0; n--)
			spi_put(*p++);
	}
}

/*
 * Reset the panel and program it with the nseq tables, leaving the display
 * off. The reset pulse exceeds TRW, 10 us (page 59). The 120 ms after
 * reset release covers note 7 of page 60: 5 ms before any command and
 * 120 ms before SLPOUT. The 120 ms after SLPOUT exceeds the 5 ms page 165
 * requires before the next command.
 */
static void
panel_program(const u_char *const *seq, const u_short *len, int nseq)
{
	int i;

	mdelay(1);
	pc_gpio_put(PICOCALC_LCD_RST, 0);
	mdelay(1);
	pc_gpio_put(PICOCALC_LCD_RST, 1);
	mdelay(120);

	lcd_select();
	for (i = 0; i < nseq; i++)
		run_seq(seq[i], len[i]);
	lcd_command(CMD_SLPOUT);
	lcd_deselect();
	mdelay(120);

	lcd_select();
	lcd_command(CMD_VSCRDEF);
	put16(0);				/* Top fixed area. */
	put16(LCD_LINES);			/* Scroll area. */
	put16(LCD_GRAM_LINES - LCD_LINES);	/* Bottom fixed area. */
	lcd_command(CMD_VSCSAD);
	put16(0);
	lcd_deselect();
}

void
st7365p_init(void)
{
	static const u_char *const seq[] = { init_seq };
	static const u_short len[] = { sizeof(init_seq) };

	pc_unreset(PC_RESET_IO_BANK0 | PC_RESET_PADS_BANK0 | PC_RESET_SPI1);

	SPI_REG(SSPCR1) = 0;
	SPI_REG(SSPCPSR) = LCD_PRESCALE;
	SPI_REG(SSPCR0) = SSPCR0_SCR(LCD_SCR) | SSPCR0_DSS_8;
	SPI_REG(SSPCR1) = SSPCR1_SSE;

	pc_pin(PICOCALC_LCD_SCK, PC_FUNC_SPI, 0);
	pc_pin(PICOCALC_LCD_TX, PC_FUNC_SPI, 0);
	pc_pin(PICOCALC_LCD_RX, PC_FUNC_SPI, 0);
	pc_gpio_output(PICOCALC_LCD_CS, 1);
	pc_gpio_output(PICOCALC_LCD_DC, 1);
	pc_gpio_output(PICOCALC_LCD_RST, 1);

	panel_program(seq, len, 1);

	/* The two columns right of the last cell; lcdterm clears the cells. */
	clear_margin();
}

void
st7365p_display_on(void)
{
	lcd_select();
	lcd_command(CMD_DISPON);
	lcd_deselect();
}

/*
 * End whatever transfer an abandoned drawer left open: let the byte in the
 * shifter finish, empty the receive FIFO and raise chip select, which ends
 * the panel's write window. The next window starts from a clean command.
 */
void
st7365p_abort(void)
{
	u_int t0 = pc_micros();

	while ((SPI_REG(SSPSR) & SSPSR_BSY) && pc_micros() - t0 < 1000)
		continue;
	while (SPI_REG(SSPSR) & SSPSR_RNE)
		(void)SPI_REG(SSPDR);
	SPI_REG(SSPICR) = SSPICR_RORIC;
	pc_gpio_put(PICOCALC_LCD_CS, 1);
	pc_gpio_put(PICOCALC_LCD_DC, 1);
}

#ifdef LCD_PANELTEST
/*
 * The panel register test (doc/PANELTEST.md): register sets compiled from
 * paneltest_sets.h and a readback of the controller's read commands. Only
 * lcdgfx.c calls these, with /dev/lcdgfx open, so lcdterm draws nothing and
 * no other transfer is in flight.
 */
#include <sys/errno.h>
#include <machine/picocalcvar.h>
#include <rp2040/dev/paneltest_sets.h>
#include <rp2040/dev/lcdgfx.h>

#define	CMD_CSCON		0xf0
#define	CMD_SPIRC		0xfb
#define	LCD_READ_SCR		19		/* 125 / (2 * 20) == 3.125 MHz. */

static const u_char NONE[] = { 0x00, 0 };	/* NOP (page 145). */
static const u_char UNLOCK[] = { CMD_CSCON, 1, 0xc3, CMD_CSCON, 1, 0x96 };
#define	DRIVER	init_seq
static const u_char VENDOR[] = {
	0xc5, 1, 0x1c,
	0xb0, 0,
	0xb4, 1, 0x01,
	CMD_INVON, 0,
	0xb6, 3, 0x80, 0x02, 0x3b,
	0xb7, 1, 0xc6,
	CMD_CSCON, 1, 0xc3, CMD_CSCON, 1, 0x96,
	CMD_MADCTL, 1, MADCTL_MX_BGR,
};
static const u_char GAMMA_DEFAULT[] = {
	0xe0, 14, 0xf0, 0x03, 0x05, 0x09, 0x0c, 0x0f, 0x3e, 0x77,
		  0x4f, 0x0f, 0x17, 0x17, 0x21, 0x23,
	0xe1, 14, 0xf0, 0x03, 0x05, 0x09, 0x0c, 0x0f, 0x3e, 0x77,
		  0x4f, 0x0f, 0x17, 0x17, 0x21, 0x23,
};
static const u_char GAMMA_V63_ZERO[] = {
	0xe0, 14, 0x00, 0x03, 0x05, 0x09, 0x0c, 0x0f, 0x3e, 0x77,
		  0x4f, 0x0f, 0x17, 0x17, 0x21, 0x23,
	0xe1, 14, 0x00, 0x03, 0x05, 0x09, 0x0c, 0x0f, 0x3e, 0x77,
		  0x4f, 0x0f, 0x17, 0x17, 0x21, 0x23,
};
/* init_seq's knots 2-14 behind the datasheet's first byte. */
static const u_char GAMMA_SHIFT[] = {
	0xe0, 14, 0xf0, 0x03, 0x09, 0x08, 0x16, 0x0a, 0x3f, 0x78,
		  0x4c, 0x09, 0x0a, 0x08, 0x16, 0x1a,
	0xe1, 14, 0xf0, 0x16, 0x19, 0x03, 0x0f, 0x05, 0x32, 0x45,
		  0x46, 0x04, 0x0e, 0x0d, 0x35, 0x37,
};
static const u_char GAMMA_ZERO[] = {
	0xe0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0xe1, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const u_char INV_COLUMN[] = { 0xb4, 1, 0x00 };
static const u_char INV_DOT1[] = { 0xb4, 1, 0x01 };
static const u_char PWR3_AA[] = { 0xc2, 1, 0xaa };
static const u_char PWR3_AF[] = { 0xc2, 1, 0xaf };
static const u_char VCOM_12[] = { 0xc5, 1, 0x12 };
static const u_char VCOM_1C[] = { 0xc5, 1, 0x1c };
static const u_char VCOM_30[] = { 0xc5, 1, 0x30 };
static const u_char VCOM_3F[] = { 0xc5, 1, 0x3f };
static const u_char FRAME_31HZ[] = { 0xb1, 1, 0x00 };
static const u_char MADCTL_40[] = { CMD_MADCTL, 1, 0x40 };
static const u_char MADCTL_08[] = { CMD_MADCTL, 1, 0x08 };

#define	SEQ_LEN(t)	((u_short)sizeof(t))

#define	PANELTEST_ROW(name, pre, base, post) \
	{ { pre, base, post }, { SEQ_LEN(pre), SEQ_LEN(base), SEQ_LEN(post) } },
static const struct {
	const u_char *seq[3];
	u_short len[3];
} panel_sets[] = { PANELTEST_SETS(PANELTEST_ROW) };

_Static_assert(sizeof(panel_sets) / sizeof(panel_sets[0]) == PANELTEST_NSETS,
    "one row per register set");

static int panel_set_now;

/*
 * Reset the panel and program register set n, then return it to the
 * format and scroll state lcdgfx.c expects: COLMOD 66h, the 320-line
 * scroll area at start 0, cleared margin, display on.
 */
int
st7365p_set(int n)
{
	if (n < 0 || n >= PANELTEST_NSETS)
		return EINVAL;
	panel_program(panel_sets[n].seq, panel_sets[n].len, 3);
	lcd_gfx_colmod(COLMOD_RGB666);
	clear_margin();
	st7365p_display_on();
	panel_set_now = n;
	return 0;
}

int
st7365p_set_current(void)
{
	return panel_set_now;
}

static u_int
spi_xfer(u_int b)
{
	spi_put(b);
	while ((SPI_REG(SSPSR) & SSPSR_RNE) == 0)
		continue;
	return SPI_REG(SSPDR);
}

static void
spi_clock(u_int scr)
{
	spi_wait_idle();
	SPI_REG(SSPCR1) = 0;
	SPI_REG(SSPCR0) = SSPCR0_SCR(scr) | SSPCR0_DSS_8;
	SPI_REG(SSPCR1) = SSPCR1_SSE;
}

/*
 * One read command (page 94): the command byte with D/CX low, then n bytes
 * clocked with D/CX high while the panel shifts SDO, sampled on the rising
 * edge as the PL022 does in mode 0. lcd_command() drains the byte received
 * during the command. The clock drops to 3.125 MHz, inside TSCYCR (150 ns,
 * page 77), and returns to the write clock after.
 */
static void
lcd_read(u_int cmd, u_int fb, u_char *out, u_int n)
{
	u_int i;

	if (fb != 0) {
		lcd_select();
		lcd_command(CMD_SPIRC);
		spi_put(fb);
		lcd_deselect();
	}
	spi_clock(LCD_READ_SCR);
	lcd_select();
	lcd_command(cmd);
	for (i = 0; i < n; i++)
		out[i] = (u_char)spi_xfer(0);
	lcd_deselect();
	spi_clock(LCD_SCR);
	if (fb != 0) {
		lcd_select();
		lcd_command(CMD_SPIRC);
		spi_put(0);
		lcd_deselect();
	}
}

/*
 * The readback list: the ID and status reads first (04h, 09h, 0Ah-0Fh,
 * DAh-DCh, D3h; Command Table 1 and RDID4), the known-good calibration,
 * two bytes for each 8-bit read: the command tables show a dummy read
 * cycle before the data (page 151) and the SPI figure on page 94 none,
 * then Command Table 2 registers through SPI Read Control. If SPIRC does
 * not take effect, those reads clock zero parameter bytes into B4h, B1h,
 * C5h, B7h or E0h, so paneltest programs its set again after a readback.
 */
static const u_char panel_reads[][3] = {
	{ 0x04, 0, 4 }, { 0x09, 0, 5 }, { 0x0a, 0, 2 }, { 0x0b, 0, 2 },
	{ 0x0c, 0, 2 }, { 0x0d, 0, 2 }, { 0x0e, 0, 2 }, { 0x0f, 0, 2 },
	{ 0xda, 0, 2 }, { 0xdb, 0, 2 }, { 0xdc, 0, 2 }, { 0xd3, 0, 4 },
	{ 0xb4, 0x10, 2 }, { 0xb4, 0x11, 2 }, { 0xb1, 0x10, 2 },
	{ 0xc5, 0x10, 2 }, { 0xb7, 0x10, 2 }, { 0xe0, 0x10, 2 },
	{ 0xe0, 0x11, 2 }, { 0xe0, 0x1d, 2 },
};
_Static_assert(sizeof(panel_reads) / sizeof(panel_reads[0]) <= PANEL_READ_N,
    "the readback fits struct picocalc_panel_read");

void
st7365p_readback(struct picocalc_panel_read *rb)
{
	u_int i;

	bzero(rb, sizeof(*rb));
	rb->variant = panel_set_now;
	rb->nent = sizeof(panel_reads) / sizeof(panel_reads[0]);
	for (i = 0; i < (u_int)rb->nent; i++) {
		rb->ent[i].cmd = panel_reads[i][0];
		rb->ent[i].fb = panel_reads[i][1];
		rb->ent[i].n = panel_reads[i][2];
		lcd_read(panel_reads[i][0], panel_reads[i][1], rb->ent[i].data,
		    panel_reads[i][2]);
	}
}
#endif /* LCD_PANELTEST */
