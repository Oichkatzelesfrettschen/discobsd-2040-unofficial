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
 * SPI1 is the RP2040's PL022 (datasheet 4.4; ARM DDI 0194H) in Motorola
 * format with SPO and SPH set (mode 3) and eight-bit frames;
 * LCD_SPI_FORMAT gives the reason. clk_peri runs at BUS_KHZ, 125 MHz, and
 * the bit clock is SSPCLK / (CPSDVSR * (1 + SCR)) (DDI 0194H pages 2-8 and
 * 3-4), so a prescale of 2 with SCR 4 gives 12.5 MHz, an 80 ns clock,
 * inside the 66 ns minimum write cycle (TSCYCW) of ST7365P_SPEC_V1.0
 * page 54. Chip select, D/C and reset are SIO outputs so that select
 * stays low across a command and its parameters. D/C changes only once
 * the PL022 reports idle (SSPSR BSY, DDI 0194H 3.3.4, page 3-8), because
 * the panel samples it with the last bit of each byte.
 *
 * The console's pixels go out as RGB565 in two bytes (COLMOD 55h, page
 * 96), 120 bytes a glyph; lcdgfx.c selects RGB666 in three bytes (66h),
 * the format the vendor drivers for this board use, or RGB565.
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
#ifdef LCD_GFX_EXT
#include <sys/lcdgfx.h>
#endif

#define	SPI1_BASE		0x40040000UL
#define	SSPCR0			0x00
#define	SSPCR1			0x04
#define	SSPDR			0x08
#define	SSPSR			0x0c
#define	SSPCPSR			0x10
#define	SSPICR			0x20

#define	SSPCR0_DSS_8		0x7		/* Eight-bit frames. */
#define	SSPCR0_SPO		(1UL << 6)	/* Clock idles high. */
#define	SSPCR0_SPH		(1UL << 7)	/* Data moves on the first edge. */
#define	SSPCR0_SCR(n)		((u_int)(n) << 8)
#define	SSPCR1_SSE		(1UL << 1)
#define	SSPSR_TNF		(1UL << 1)
#define	SSPSR_RNE		(1UL << 2)
#define	SSPSR_BSY		(1UL << 4)
#define	SSPICR_RORIC		(1UL << 0)

#define	LCD_PRESCALE		2
#define	LCD_SCR			4		/* 125 / (2 * 5) == 12.5 MHz. */

/*
 * The frame format. With SPH clear the PL022 raises its frame signal
 * between the words of a continuous transfer (ARM DDI 0194H, 2.3.10, page
 * 2-11), and the clock pauses with it: lcdgfxtest measured a 320 by 320
 * fill at 1.19 times its bit time on the board, which is 9.5 clock periods
 * a byte. With SPO and SPH set the words follow without a gap (2.3.13,
 * page 2-15), and the panel still samples SDA on the rising edge of SCL
 * (page 94), since the second edge of a clock that idles high is the
 * rising one. Chip select is a GPIO, so the frame signal stays off the pins.
 * LCD_SPI_MODE0 keeps the gapped format for a comparison build.
 */
#ifdef LCD_SPI_MODE0
#define	LCD_SPI_FORMAT		SSPCR0_DSS_8
#else
#define	LCD_SPI_FORMAT		(SSPCR0_SPO | SSPCR0_SPH | SSPCR0_DSS_8)
#endif

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
#define	COLMOD_RGB565		0x55

#define	LCD_LINES		320		/* Visible lines. */
#define	LCD_GRAM_LINES		480		/* Controller lines. */

#define	SPI_REG(off)		PC_REG32(SPI1_BASE + (off))

/*
 * Sixteen colors, R, G, B. The console sends them as RGB565 (COLMOD 55h),
 * two bytes a pixel: five bits of red and blue and six of green, which the
 * controller widens to its eighteen-bit memory. 0-7 are the SGR 30-37
 * colors, 8-15 their bold variants, so bold default text is entry 15 in
 * place of entry 7. Each entry is its RGB565 word widened, so the panel
 * shows the listed value. Colors 1-8 and 9-15 stay apart under normal vision
 * and protanopia, deuteranopia and tritanopia simulation, and entry 8 is
 * neutral gray because SGR 90 is dim text. The numeric bounds and their
 * check are in tools/check_console_palette.py.
 */
const u_char lcd_palette[16][3] = {
	{   0,   0,   0 }, { 206,  93,   0 }, {  57, 227, 140 },
	{ 214, 190,  24 }, {  90, 113, 255 }, { 189, 109, 198 },
	{ 140, 182, 206 }, { 222, 219, 214 }, { 132, 134, 132 },
	{ 222, 113,  41 }, {  16, 255, 173 }, { 239, 207,  24 },
	{ 123, 134, 255 }, { 206, 142, 198 }, { 173, 207, 231 },
	{ 255, 255, 255 },
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
	CMD_COLMOD, 1, COLMOD_RGB565,
	0xb0, 1, 0x00,
	0xb1, 1, 0xa0,
	CMD_INVON, 0,
	0xb4, 1, 0x02,
	0xb6, 3, 0x02, 0x02, 0x3b,
	0xb7, 1, 0xc6,
	0xf7, 4, 0xa9, 0x51, 0x2c, 0x82,
};

/*
 * The frame rate. FRMCTR1 (B1h, page 219) takes FRS[3:0] and DIVA[1:0] in
 * its first parameter and RTNA[4:0] in its second, and gives the normal
 * mode frame rate as 10^7 / ((168 + RTNA + 32 (15 - FRS)) (480 + VFP + VBP)).
 * BPC (B5h, page 222) lacks a documented reset value, so a frame table
 * writes VFP = VBP = 2 (and HBP = 4), the values its table lists, which
 * makes the second factor 484; B5h precedes SLPOUT as page 223 requires.
 * FRAME_SET builds a table from its fields and asserts at compile time
 * that the formula, shifted right by DIVA, lands within 0.25 Hz of the
 * nominal rate.
 *
 * On the measured board B1h acts only after the Command 2 unlock (CSCON
 * F0h C3h, 96h, page 245), and the rate is 0.962 of the formula at FRS 12
 * (doc/PANELTEST.md). st7365p_init() therefore sends init_unlock, init_seq
 * and init_frame: FRS 12, RTNA 1, 77.96 Hz by the formula and 74.94 Hz
 * measured. The B1h A0h of init_seq, sent first, is overridden.
 */
#define	FRAME_LINES	(480 + 2 + 2)
#define	FRAME_MHZ(frs, diva, rtna) \
	((10000000000ULL / ((168ULL + (rtna) + 32 * (15 - (frs))) * \
	FRAME_LINES)) >> (diva))
#define	FRAME_SET(name, frs, diva, rtna, hz) \
	static const u_char name[] = { 0xb5, 4, 0x02, 0x02, 0x00, 0x04, \
	    0xb1, 2, (frs) << 4 | (diva), (rtna) }; \
	_Static_assert((frs) <= 15 && (diva) <= 3 && (rtna) <= 31 && \
	    FRAME_MHZ(frs, diva, rtna) + 250 >= (hz) * 1000ULL && \
	    FRAME_MHZ(frs, diva, rtna) <= (hz) * 1000ULL + 250, \
	    #name " gives its nominal rate")
#define	CMD_CSCON		0xf0
static const u_char init_unlock[] = { CMD_CSCON, 1, 0xc3, CMD_CSCON, 1, 0x96 };
FRAME_SET(init_frame, 12, 0, 1, 78);

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

#if defined(LCD_PANELTEST) || defined(LCD_GFX_EXT)
/*
 * The read side: a byte exchanged for the one the panel shifts out on SDO,
 * and the clock change a read needs, since TSCYCR is 150 ns (page 54)
 * against the 66 ns of a write.
 */
#define	LCD_READ_SCR		19		/* 125 / (2 * 20) == 3.125 MHz. */
#define	CMD_GSCAN		0x45

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
	SPI_REG(SSPCR0) = SSPCR0_SCR(scr) | LCD_SPI_FORMAT;
	SPI_REG(SSPCR1) = SSPCR1_SSE;
}
#endif

#ifdef LCD_GFX_EXT
#define	CMD_SLPIN		0x10
#define	CMD_DISPOFF		0x28
#define	CMD_RAMRD		0x2e
#define	CMD_IDMOFF		0x38
#define	CMD_IDMON		0x39

static u_int lcd_power_mode;		/* LCDG_PWR_* in force. */
static u_int lcd_power_pending;		/* A mode change may have begun. */
#endif

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

/* A palette entry as the RGB565 word the console sends. */
static u_int
pal565(u_int i)
{
	const u_char *rgb = lcd_palette[i];

	return (rgb[0] >> 3) << 11 | (rgb[1] >> 2) << 5 | rgb[2] >> 3;
}

_Static_assert(LCDFONT_HEIGHT == LT_GLYPH_ROWS,
    "a bank glyph and an ASCII glyph share the nine-row draw loop");

void
lcd_draw_cell(int prow, int col, int ch, unsigned attr)
{
	u_int fg = pal565(LT_ATTR_FG(attr));
	u_int bg = pal565(LT_ATTR_BG(attr));
	const u_char *glyph;
	u_char bank[LT_GLYPH_ROWS];
	u_int x = (u_int)col * LT_CELL_W, y = (u_int)prow * LT_CELL_H;
	u_int bits;
	int row, px;

	if (lcdterm_glyph(ch, bank))
		glyph = bank;
	else {
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
			put16((bits & 0x80) ? fg : bg);
	}
	lcd_deselect();
}

void
lcd_fill_cells(int prow, int col, int n, unsigned attr)
{
	u_int bg = pal565(LT_ATTR_BG(attr));
	u_int x = (u_int)col * LT_CELL_W, y = (u_int)prow * LT_CELL_H;
	u_int count = (u_int)n * LT_CELL_W * LT_CELL_H;

	if (n <= 0)
		return;
	lcd_select();
	lcd_window(x, y, x + (u_int)n * LT_CELL_W - 1, y + LT_CELL_H - 1);
	while (count-- > 0)
		put16(bg);
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
 * other drawer may touch the panel meanwhile (lcdterm.c stops drawing while
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
		put16(pal565(0));
	lcd_deselect();
}

/*
 * Return the panel to the configuration lcdterm.c draws in: RGB565, the
 * 320-line scroll area with its 160-line bottom fixed area, and a margin a
 * pixel program may have drawn on cleared. The scroll start is the
 * caller's: lcdterm_repaint() sets it from the ring.
 */
void
lcd_gfx_reset(void)
{
	lcd_gfx_colmod(COLMOD_RGB565);
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
 * The reset pulse exceeds TRW, 10 us (page 59). The 120 ms after
 * reset release covers note 7 of page 60 before the first command.
 * Chip select stays low across the register sequence through SLPOUT.
 */
static void
panel_begin(void)
{
	mdelay(1);
	pc_gpio_put(PICOCALC_LCD_RST, 0);
	mdelay(1);
	pc_gpio_put(PICOCALC_LCD_RST, 1);
	mdelay(120);
#ifdef LCD_GFX_EXT
	lcd_power_mode = LCDG_PWR_ON;	/* panel_wake() follows every reset. */
	lcd_power_pending = 0;
#endif

	lcd_select();
}

/* The 120 ms after SLPOUT exceeds the 5 ms page 165 requires. */
static void
panel_wake(void)
{
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
	pc_unreset(PC_RESET_IO_BANK0 | PC_RESET_PADS_BANK0 | PC_RESET_SPI1);

	SPI_REG(SSPCR1) = 0;
	SPI_REG(SSPCPSR) = LCD_PRESCALE;
	SPI_REG(SSPCR0) = SSPCR0_SCR(LCD_SCR) | LCD_SPI_FORMAT;
	SPI_REG(SSPCR1) = SSPCR1_SSE;

	pc_pin(PICOCALC_LCD_SCK, PC_FUNC_SPI, 0);
	pc_pin(PICOCALC_LCD_TX, PC_FUNC_SPI, 0);
	pc_pin(PICOCALC_LCD_RX, PC_FUNC_SPI, 0);
	pc_gpio_output(PICOCALC_LCD_CS, 1);
	pc_gpio_output(PICOCALC_LCD_DC, 1);
	pc_gpio_output(PICOCALC_LCD_RST, 1);

	panel_begin();
	run_seq(init_unlock, sizeof(init_unlock));
	run_seq(init_seq, sizeof(init_seq));
	run_seq(init_frame, sizeof(init_frame));
	panel_wake();

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
#ifdef LCD_GFX_EXT
	/*
	 * The report that follows must be visible. A fault can arrive between
	 * the commands of a mode change, where lcd_power_mode names neither
	 * the old mode nor the new, so every leaving command goes out whenever
	 * a change may have begun; each is harmless in a panel already there.
	 */
	if (lcd_power_pending) {
		/* SLPIN may be the last command sent: page 163's 120 ms. */
		mdelay(120);
		lcd_select();
		lcd_command(CMD_SLPOUT);
		lcd_deselect();
		mdelay(120);
		lcd_select();
		lcd_command(CMD_IDMOFF);
		lcd_command(CMD_DISPON);
		lcd_deselect();
		lcd_power_mode = LCDG_PWR_ON;
		lcd_power_pending = 0;
	}
#endif
}

#ifdef LCD_GFX_EXT
/*
 * The LCD_GFX_EXT primitives of lcdgfx.c (lcdterm.h). Only lcdgfx.c calls
 * them, with /dev/lcdgfx open and no window in flight.
 */

void
lcd_gfx_read_begin(void)
{
	spi_clock(LCD_READ_SCR);
}

void
lcd_gfx_read_end(void)
{
	spi_clock(LCD_SCR);
}

/*
 * RAMRD (2Eh, page 178) over a window of n pixels of one line: len reply
 * bytes, stored as they arrive. lcdgfx.c holds the framing, which follows
 * the COLMOD in force; bytes past the window's last pixel repeat it from
 * its first.
 */
void
lcd_gfx_read(int x, int y, int n, unsigned char *raw, unsigned len)
{

	lcd_select();
	lcd_command(CMD_CASET);
	put16((u_int)x);
	put16((u_int)(x + n - 1));
	lcd_command(CMD_RASET);
	put16((u_int)y);
	put16((u_int)y);
	lcd_command(CMD_RAMRD);
	while (len-- > 0)
		*raw++ = spi_xfer(0);
	lcd_deselect();
}

/* GSCAN (45h, page 201), decoded as st7365p_scan() decodes it. */
int
lcd_gfx_scanline(void)
{
	u_int raw;

	lcd_select();
	lcd_command(CMD_GSCAN);
	raw = spi_xfer(0) << 16;
	raw |= spi_xfer(0) << 8;
	raw |= spi_xfer(0);
	lcd_deselect();
	return (raw >> 7) & 0xffff;
}

unsigned
lcd_gfx_micros(void)
{
	return pc_micros();
}

/*
 * Leave the mode in force, then enter the one asked for. SLPIN and SLPOUT
 * each need 5 ms before the next command and 120 ms before the other of
 * the pair (pages 163 and 165); 120 ms covers both. Sleep keeps the
 * display-on state and the memory.
 */
void
lcd_gfx_power(unsigned mode)
{
	static const u_char leave[] = { 0, CMD_IDMOFF, CMD_DISPON, CMD_SLPOUT };
	static const u_char enter[] = { 0, CMD_IDMON, CMD_DISPOFF, CMD_SLPIN };
	u_int was = lcd_power_mode;

	if (mode == was)
		return;
	lcd_power_pending = 1;
	lcd_select();
	if (was != LCDG_PWR_ON) {
		lcd_command(leave[was]);
		if (was == LCDG_PWR_SLEEP) {
			lcd_deselect();
			mdelay(120);
			lcd_select();
		}
	}
	if (mode != LCDG_PWR_ON)
		lcd_command(enter[mode]);
	lcd_deselect();
	if (mode == LCDG_PWR_SLEEP)
		mdelay(120);
	lcd_power_mode = mode;
	/* st7365p_abort() wakes the panel while any mode but on may hold. */
	lcd_power_pending = mode != LCDG_PWR_ON;
}

/*
 * The LCDG_DITHER probe (sys/lcdgfx.h). COLMOD 77h is the register's 24-bit
 * value (page 196); the 4-line serial interface lists 16- and 18-bit input
 * only (page 96), so what the memory keeps of these pixels is the
 * measurement. MODESEL (B9h, page 229) has the one parameter 02h, dithering
 * on, and is a Command Table 2 register, so the unlock precedes it.
 */
void
lcd_gfx_dither(int on)
{
	static const u_char modesel[] = {
		0xf0, 1, 0xc3, 0xf0, 1, 0x96,
		0xb9, 1, 0x02,
		0xf0, 1, 0x3c, 0xf0, 1, 0x69,
	};
	int n;

	lcd_select();
	lcd_command(CMD_COLMOD);
	spi_put(0x77);
	if (on)
		run_seq(modesel, sizeof(modesel));
	lcd_window(LCDG_DITHER_X, LCD_GRAM_LINES - 1,
	    LCDG_DITHER_X + LCDG_DITHER_N - 1, LCD_GRAM_LINES - 1);
	for (n = 3 * LCDG_DITHER_N; n > 0; n--)
		spi_put(LCDG_DITHER_BYTE);
	lcd_deselect();
}
#endif /* LCD_GFX_EXT */

#ifdef LCD_PANELTEST
/*
 * The panel register test (doc/PANELTEST.md): register sets compiled from
 * paneltest_sets.h and a readback of the controller's read commands. Only
 * lcdgfx.c calls these, with /dev/lcdgfx open, so lcdterm stops drawing and
 * no other transfer is in flight.
 */
#include <sys/errno.h>
#include <machine/picocalcvar.h>
#include <rp2040/dev/paneltest_sets.h>
#include <rp2040/dev/lcdgfx.h>
#include <rp2040/dev/st7365p_scan.h>

#define	CMD_SPIRC		0xfb

static const u_char NONE[] = { 0x00, 0 };	/* NOP (page 145). */
#define	UNLOCK	init_unlock
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

/*
 * Frame-rate sets, built by FRAME_SET (above init_unlock). The printed
 * formula omits DIVA; the 24 Hz set takes DIVA = 01b (Fosc / 2) to halve a
 * 48 Hz line timing, an inference that the GSCAN measurement checks.
 *
 *   set    FRS  DIVA  RTNA  line units           formula
 *   24 Hz  7    01b   6     168 + 6 + 256 = 430  48.05 Hz, halved: 24.02 Hz
 *   40 Hz  5    00b   28    168 + 28 + 320 = 516 40.04 Hz
 *   60 Hz  10   00b   16    168 + 16 + 160 = 344 60.06 Hz (A0h 10h, the defaults)
 *   75 Hz  12   00b   11    168 + 11 + 96 = 275  75.13 Hz
 *   90 Hz  14   00b   30    168 + 30 + 32 = 230  89.83 Hz
 *   120 Hz 15   00b   4     168 + 4 + 0 = 172    120.13 Hz
 *
 * The measured board runs the formula 2.6% to 3.8% slow (doc/PANELTEST.md),
 * so three further sets aim the formula above a target rate by the ratio
 * measured at the same FRS: 62.23 Hz for 60 (ratio 0.965), 77.96 Hz for 75
 * (0.962) and 122.98 Hz, the formula's maximum at these porches, for 120
 * (0.974).
 *
 *   62 Hz  10   00b   4     168 + 4 + 160 = 332  62.23 Hz
 *   78 Hz  12   00b   1     168 + 1 + 96 = 265   77.96 Hz
 *   123 Hz 15   00b   0     168 + 0 + 0 = 168    122.98 Hz
 *
 * The 78 Hz table is init_frame, the one st7365p_init() sends. On the
 * board the 123 Hz table scans at 119.66 Hz and the glass stays black.
 */
FRAME_SET(FRAME_24HZ, 7, 1, 6, 24);
FRAME_SET(FRAME_40HZ, 5, 0, 28, 40);
FRAME_SET(FRAME_60HZ, 10, 0, 16, 60);
FRAME_SET(FRAME_75HZ, 12, 0, 11, 75);
FRAME_SET(FRAME_90HZ, 14, 0, 30, 90);
FRAME_SET(FRAME_120HZ, 15, 0, 4, 120);
FRAME_SET(FRAME_62HZ, 10, 0, 4, 62);
#define	FRAME_78HZ	init_frame
FRAME_SET(FRAME_123HZ, 15, 0, 0, 123);
static const u_char MADCTL_40[] = { CMD_MADCTL, 1, 0x40 };
static const u_char MADCTL_08[] = { CMD_MADCTL, 1, 0x08 };
/* Distinctive in-range values that the marked readback variants expect. */
static const u_char CT2_MARK[] = {
	0xc5, 1, PANEL_MARK_C5,
	0xb1, 2, PANEL_MARK_B1_0, PANEL_MARK_B1_1,
};
static const u_char LOCK[] = { CMD_CSCON, 1, 0x3c, CMD_CSCON, 1, 0x69 };

/* Each row shares its preamble and base with the other register sets. */
enum { PRE_NONE, PRE_UNLOCK };
enum { BASE_DRIVER, BASE_VENDOR };

#define	PANELTEST_ROW(name, pre, base, post) \
	{ post, sizeof(post), PRE_##pre, BASE_##base },
static const struct panel_set {
	const u_char *post;
	u_short post_len;
	u_char pre, base;
} panel_sets[] = { PANELTEST_SETS(PANELTEST_ROW) };

_Static_assert(sizeof(panel_sets) / sizeof(panel_sets[0]) == PANELTEST_NSETS,
    "one row per register set");

static int panel_set_now;

/*
 * Reset the panel and program register set n, then return it to the
 * format and scroll state lcdgfx.c opens with: COLMOD 66h, the 320-line
 * scroll area at start 0, cleared margin, display on.
 */
int
st7365p_set(int n)
{
	const struct panel_set *s;

	if (n < 0 || n >= PANELTEST_NSETS)
		return EINVAL;
	s = &panel_sets[n];
	panel_begin();
	if (s->pre == PRE_UNLOCK)
		run_seq(UNLOCK, sizeof(UNLOCK));
	else
		run_seq(NONE, sizeof(NONE));
	if (s->base == BASE_VENDOR)
		run_seq(VENDOR, sizeof(VENDOR));
	else
		run_seq(DRIVER, sizeof(DRIVER));
	run_seq(s->post, s->post_len);
	panel_wake();
	/* The margin clears in the console's format; lcdgfx.c then takes 66h. */
	lcd_gfx_colmod(COLMOD_RGB565);
	clear_margin();
	lcd_gfx_colmod(COLMOD_RGB666);
	st7365p_display_on();
	panel_set_now = n;
	return 0;
}

int
st7365p_set_current(void)
{
	return panel_set_now;
}

static void
send_seq(const u_char *p, u_int len)
{
	lcd_select();
	run_seq(p, len);
	lcd_deselect();
}

static void
send_spirc(u_int fb)
{
	lcd_select();
	lcd_command(CMD_SPIRC);
	spi_put(fb);
	lcd_deselect();
}

/*
 * One read command (page 94): the command byte with D/CX low, then n bytes
 * clocked with D/CX high while the panel shifts SDO, sampled on the rising
 * edge as the PL022 does in mode 3. lcd_command() drains the byte received
 * during the command. The clock drops to 3.125 MHz, inside TSCYCR (150 ns,
 * page 54), and returns to the write clock after. how selects the Command
 * Table 2 variant (PANEL_RD_*, machine/picocalcvar.h).
 */
static void
lcd_read(const u_char *r, u_char *out)
{
	u_int cmd = r[0], fb = r[1], n = r[2], how = r[3], i;

	if (fb != 0 && (how & PANEL_RD_FB_FIRST))
		send_spirc(fb);
	if (how & PANEL_RD_MARK) {
		send_seq(UNLOCK, sizeof(UNLOCK));
		send_seq(CT2_MARK, sizeof(CT2_MARK));
	}
	if (how & PANEL_RD_LOCK)
		send_seq(LOCK, sizeof(LOCK));
	if (how & PANEL_RD_PIXEL) {
		lcd_gfx_colmod(COLMOD_RGB565);
		lcd_select();
		lcd_window(0, LCD_GRAM_LINES - 1, 0, LCD_GRAM_LINES - 1);
		put16(0xffff);
		lcd_deselect();
		/* The reply takes the format in force: read it as 18 bits. */
		lcd_gfx_colmod(COLMOD_RGB666);
	}
	if (fb != 0 && (how & (PANEL_RD_FB_FIRST | PANEL_RD_ONE_CS)) == 0)
		send_spirc(fb);
	spi_clock(LCD_READ_SCR);
	lcd_select();
	if (how & PANEL_RD_ONE_CS) {
		lcd_command(CMD_SPIRC);
		spi_put(fb);
	}
	lcd_command(cmd);
	for (i = 0; i < n; i++)
		out[i] = (u_char)spi_xfer(0);
	lcd_deselect();
	spi_clock(LCD_SCR);
	if (fb != 0)
		send_spirc(0);
}

/*
 * The readback list: the ID and status reads first (04h, 09h, 0Ah-0Fh,
 * DAh-DCh, D3h; Command Table 1 and RDID4), the known-good calibration,
 * two bytes for each 8-bit read: the command tables show a dummy read
 * cycle before the data (page 151) and the SPI figure on page 94 none,
 * then Command Table 2 registers through SPI Read Control. If SPIRC does
 * not take effect, those reads clock zero parameter bytes into B4h, B1h,
 * C5h, B7h or E0h, so paneltest programs its set again after a readback.
 * D3h is itself a Command Table 2 entry (section 9.3.18) and answers
 * without FBh, which makes it the known-good read of that table.
 *
 * The last eight are the Command Table 2 read control: each rewrites the
 * mark values (C5h 2Ah, B1h 50h 1Ch) and then reads with one condition
 * changed from the marked C5h read through FBh 10h: no FBh, parameter
 * number 1, FBh before the unlock, command 2 locked again before FBh, FBh
 * in the read's chip select; then B1h through parameter numbers 0 and 1.
 * Five bytes each show where in the reply a mark value lands.
 *
 * The last five are the two Command Table 2 registers that section 9.3
 * prints with read rows: NVM Status Read (D2h, page 235: the ID and VCOM
 * program counts, BUSY and the stored VMF[5:0]), plain and through FBh
 * 10h, and RDID4 (D3h) through FBh 10h, 11h and 12h. D3h answers without
 * FBh, so a reply that changes with the parameter number shows SPIRC
 * acting on a register known to be readable, and one that does not shows
 * SPIRC inert on this link.
 *
 * The last entry writes one FFFFh pixel under COLMOD 55h to the last line
 * of the controller's memory, which the glass does not show, selects 66h
 * and reads it back with RAMRD (2Eh, page 178). The memory read answers in
 * the format in force (the board returned two bytes a pixel under 55h and
 * three under 66h), so only a read under 66h shows how EM (B7h) EPF widened
 * the sixteen bits to eighteen (page 228): FCh FCh FCh is full white,
 * F8h FCh F8h white with the low bit of red and blue clear, the widening
 * the datasheet gives for EPF 00. The entry leaves the panel in 66h;
 * lcdgfx_panel_read() programs the format lcdgfx.c is in again.
 */
#define	M	PANEL_RD_MARK
static const u_char panel_reads[][4] = {
	{ 0x04, 0, 4, 0 }, { 0x09, 0, 5, 0 }, { 0x0a, 0, 2, 0 },
	{ 0x0b, 0, 2, 0 }, { 0x0c, 0, 2, 0 }, { 0x0d, 0, 2, 0 },
	{ 0x0e, 0, 2, 0 }, { 0x0f, 0, 2, 0 }, { 0xda, 0, 2, 0 },
	{ 0xdb, 0, 2, 0 }, { 0xdc, 0, 2, 0 }, { 0xd3, 0, 4, 0 },
	{ 0xb4, 0x10, 2, 0 }, { 0xb4, 0x11, 2, 0 }, { 0xb1, 0x10, 2, 0 },
	{ 0xc5, 0x10, 2, 0 }, { 0xb7, 0x10, 2, 0 }, { 0xe0, 0x10, 2, 0 },
	{ 0xe0, 0x11, 2, 0 }, { 0xe0, 0x1d, 2, 0 },
	{ 0xc5, 0x10, 5, M }, { 0xc5, 0, 5, M }, { 0xc5, 0x11, 5, M },
	{ 0xc5, 0x10, 5, M | PANEL_RD_FB_FIRST },
	{ 0xc5, 0x10, 5, M | PANEL_RD_LOCK },
	{ 0xc5, 0x10, 5, M | PANEL_RD_ONE_CS },
	{ 0xb1, 0x10, 5, M }, { 0xb1, 0x11, 5, M },
	{ 0xd2, 0, 5, 0 }, { 0xd2, 0x10, 5, 0 },
	{ 0xd3, 0x10, 4, 0 }, { 0xd3, 0x11, 4, 0 }, { 0xd3, 0x12, 4, 0 },
	{ 0x2e, 0, 5, PANEL_RD_PIXEL },
};
#undef	M
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
		rb->ent[i].how = panel_reads[i][3];
		lcd_read(panel_reads[i], rb->ent[i].data);
	}
}

/*
 * Sample GSCAN (45h, page 201), the line the controller is scanning, back
 * to back until SCAN_WRAPS wraps or SCAN_US. The reply is a dummy period
 * and the 16-bit line N; the board's 24- and 32-bit reads carry a one-bit
 * dummy clock (doc/PANELTEST.md), so N is taken as bits 22-7 of the three
 * reply bytes. That decode is unmeasured for 45h, and raw_max keeps a raw
 * reply for the other. Each read takes the 8-bit command and 24 clocks at
 * 3.125 MHz, about 10 us; interrupts stay enabled, and max_gap_us records
 * how far any of them delayed a sample. The clock drops once, around the
 * whole loop.
 * st7365p_scan.h holds the rule that tells a wrap from a misread of the
 * moving counter; rejected counts the samples it set aside.
 */
#define	SCAN_US			400000	/* Eight frames at 24 Hz with a lead. */
#define	SCAN_WRAPS		9
#define	SCAN_LINE_LIMIT		2048	/* Decoded lines at or above are bad. */

void
st7365p_scan(struct picocalc_panel_scan *sc)
{
	struct scan_track tr;
	u_int t0, now, prev_t, raw, line;

	bzero(sc, sizeof(*sc));
	bzero(&tr, sizeof(tr));
	spi_clock(LCD_READ_SCR);
	t0 = prev_t = pc_micros();
	do {
		lcd_select();
		lcd_command(CMD_GSCAN);
		raw = spi_xfer(0) << 16;
		raw |= spi_xfer(0) << 8;
		raw |= spi_xfer(0);
		lcd_deselect();
		now = pc_micros();
		sc->samples++;
		if (now - prev_t > sc->max_gap_us)
			sc->max_gap_us = now - prev_t;
		prev_t = now;
		line = (raw >> 7) & 0xffff;
		if (line >= SCAN_LINE_LIMIT) {
			sc->bad++;
			continue;
		}
		if (scan_step(&tr, line, now - t0)) {
			if (sc->wraps == 0)
				sc->first_us = tr.wrap_us;
			sc->last_us = tr.wrap_us;
			sc->wraps++;
		}
		if (tr.at && raw > sc->raw_max)
			sc->raw_max = raw;
	} while (sc->wraps < SCAN_WRAPS && now - t0 < SCAN_US);
	sc->elapsed_us = now - t0;
	sc->rejected = tr.rejected;
	spi_clock(LCD_SCR);
}
#endif /* LCD_PANELTEST */
