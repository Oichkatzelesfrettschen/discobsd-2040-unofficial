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
 * three cell primitives lcdterm.c draws through.
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
 * clear as scrolling requires. The initialization values are those the
 * vendor's PicoMite firmware sends for its ILI9488P (320x320) mode on
 * this panel; sys/arch/rp2040/doc/PICOCALC.md cites them.
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
static const u_char palette[16][3] = {
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

void
lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{
	const u_char *fg = palette[LT_ATTR_FG(attr)];
	const u_char *bg = palette[LT_ATTR_BG(attr)];
	const u_char *glyph;
	u_int x = (u_int)col * LT_CELL_W, y = (u_int)prow * LT_CELL_H;
	u_int bits;
	int row, px;

	if (ch < LCDFONT_FIRST || ch >= LCDFONT_FIRST + LCDFONT_NCHARS)
		ch = '?';
	glyph = lcdfont[ch - LCDFONT_FIRST];

	lcd_select();
	lcd_window(x, y, x + LT_CELL_W - 1, y + LT_CELL_H - 1);
	for (row = 0; row < LT_CELL_H; row++) {
		if (row < LCDFONT_HEIGHT)
			bits = glyph[row];
		else
			bits = (attr & LT_ATTR_UL) ? 0xfc : 0;
		for (px = 0; px < LT_CELL_W; px++, bits <<= 1)
			put_pixel((bits & 0x80) ? fg : bg);
	}
	lcd_deselect();
}

void
lcd_fill_cells(int prow, int col, int n, unsigned attr)
{
	const u_char *bg = palette[LT_ATTR_BG(attr)];
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
lcd_set_top(int prow)
{
	lcd_select();
	lcd_command(CMD_VSCSAD);
	put16((u_int)prow * LT_CELL_H);
	lcd_deselect();
}

/*
 * Reset the panel and program it. The reset pulse and the waits exceed
 * the ST7365P minimums (TRW 10 us; 120 ms after reset from Sleep Out and
 * after SLPOUT, pages 59-60 and 165).
 */
void
st7365p_init(void)
{
	const u_char *p = init_seq;
	u_int n;
	int x;

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

	mdelay(1);
	pc_gpio_put(PICOCALC_LCD_RST, 0);
	mdelay(1);
	pc_gpio_put(PICOCALC_LCD_RST, 1);
	mdelay(120);

	lcd_select();
	while (p < init_seq + sizeof(init_seq)) {
		lcd_command(*p++);
		for (n = *p++; n > 0; n--)
			spi_put(*p++);
	}
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

	/* Clear the two columns right of the last cell, then the cells. */
	lcd_select();
	lcd_window(LT_COLS * LT_CELL_W, 0, 319, LCD_LINES - 1);
	for (x = (320 - LT_COLS * LT_CELL_W) * LCD_LINES; x > 0; x--)
		put_pixel(palette[0]);
	lcd_command(CMD_DISPON);
	lcd_deselect();
}
