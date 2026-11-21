/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The machine: an IBM PC as DOS sees it through the firmware interface.
 *
 * The disk, system and keyboard services (13h, 15h, 16h) return with
 * interrupts enabled whatever the caller's flag was, as the IBM firmware
 * does by enabling them and leaving through RETF 2: the SvarDOS kernel
 * times its start-up key wait by the tick count with interrupts off and
 * depends on the keyboard service to let the timer in.
 *
 * Control-] typed at a drawn display opens a one-line menu: leave, or
 * change the image in a floppy drive while the guest runs.
 *
 * No firmware code runs in the guest. Every interrupt vector points at a
 * four-byte entry in segment F000 that holds the trap sequence of i86.h,
 * and pc_trap() performs the service on the registers and guest memory.
 * The two hardware interrupts have guest code after their traps: the end
 * of interrupt, and for the timer the call of interrupt 1Ch before it.
 *
 * The display is the 4000 bytes of color text memory at B800:0. The
 * firmware services write it, programs may write it directly, and
 * refresh() sends the cells that differ from the last frame sent to the
 * terminal as ANSI sequences. A scroll of the whole screen goes to the
 * terminal as line feeds inside a scroll region of the guest's height, so
 * a terminal taller than 25 lines scrolls the guest's lines alone.
 *
 * Interrupts come through an interrupt controller at ports 20h and 21h
 * with request, in-service and mask registers, fixed priority and the
 * end-of-interrupt commands: the timer on line 0, the keyboard on line 1,
 * COM1 (serial.c) on line 4. Control-\ turns mouse keys on and off: while
 * on, the arrows and the bracket keys drive the mouse on COM1
 * (mouse_arrow(), mouse_button()) instead of typing.
 *
 * The keyboard is the terminal's byte stream turned back into key presses.
 * Each key becomes scan bytes at port 60h, one keyboard interrupt apiece:
 * the modifiers it needs, its make byte, its break byte, the modifiers
 * released. A terminal reports no releases, so the break follows the make
 * at once and no key is ever seen held. The handler behind vector 9 does
 * the firmware's part: it keeps the shift flags at 40:17 and puts the key
 * code into the 16-word buffer at 40:1E that interrupt 16h reads. A guest
 * that takes vector 9 sees the scan bytes first, as on the machine.
 * A guest that polls interrupt 16h without result gives up the processor.
 */
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HOSTBUILD
#include <dirent.h>
#define direct dirent
#else
#include <sys/dir.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gfx.h"
#include "mem.h"
#include "pc.h"
#include "serial.h"
#include "ems.h"

#define ROWS		25
#define VRAM_ADDR	GFX_VRAM
#define VRAM_SIZE	0x1000u		/* a text mode's page */
#define ROM_ADDR	0xff000u
#define ROM_OFF		0xf000u		/* offset of the ROM in F000 */
#define ROM_FONT	0xffa6eu	/* where programs expect the 8x8 font */
#define ROM_TIMER	0x400u		/* ROM offsets of the fixed parts */
#define ROM_DPT		0x410u		/* drive A's table; B's DPT_STEP on */
#define DPT_STEP	0x20u
#define DPT_SIZE	11
#define RESET_ADDR	0xffff0u
#define ROM_KEY		0x420u
#define TRAP_TICK	0x08
#define TRAP_KEY	0x09
#define KB_N		64		/* scan bytes waiting for the controller */
#define KB_KEY		8		/* the most one key puts there */
#define K_SHIFT		0x10000u	/* key_code(): typed with this held */
#define K_CTRL		0x20000u
#define K_ALT		0x40000u
#define BDA		0x400u
#define IDLE_POLLS	16		/* empty key polls in one tick: waiting */
#define MK_STEP		8		/* mickeys an arrow moves the mouse */
#define MK_MAX		64		/* the most, after repeats */
#define MK_TICKS	6		/* a press this soon after the last: a repeat */
#define TYPE_TICKS	64		/* timer ticks of polling before typing */

struct disk {
	int fd;
	uint32_t nsec;
	uint16_t cyl;
	uint8_t heads, spt, ro;
};

int pc_idle;

/* The cells at B800:0 and the copy of them last drawn; a graphics mode of
 * the panel display uses both halves for pixels (gfx.h). */
static uint8_t vmem[2 * VRAM_SIZE];
#define vram	vmem
#define shadow	(vmem + VRAM_SIZE)
static uint8_t rom[0x440], rom_reset[16];
static struct disk disks[4];
static const char *disk_path[4];	/* the image each drive holds */
static struct pc_config cf;

static uint8_t kb_scan[KB_N];		/* scan bytes on their way to port 60h */
static uint16_t kb_code[KB_N];		/* the key code a make byte stands for */
static unsigned kb_head, kb_n, key_polls, nticks, poll_tick;
static uint8_t kb_latch, kb_full;	/* port 60h and its status bit */
static uint16_t kb_latch_code;
static uint8_t pic_irr, pic_isr, pic_readisr;
static uint8_t pic_icw;			/* initialization words still to come */
static uint8_t pic_icw2, pic_icw4;	/* which of them the next one is */
static uint8_t pic_base = 8, pic_aeoi;	/* what the words selected */
static unsigned idle_polls, idle_tick;
static int key_eof;
static int ser_level;			/* COM1's interrupt output */
static int mouse_keys;			/* arrows and brackets move the mouse */
static unsigned mouse_held;		/* buttons held by { and } */
static unsigned mk_dir = 4, mk_tick, mk_step;	/* the last arrow */

/* The terminal is drawn on: the display is neither off nor the panel. */
#define TERMINAL	(!cf.quiet && !gfx_on)

static unsigned cols;
static int tx = -1, ty = -1, tattr = -1;	/* terminal state; -1 unknown */
static char obuf[256];
static unsigned olen;

static unsigned long next_tick, vtime;
static unsigned ticks_due;
static uint8_t imask, crtc_index, crt_flip, cmos_index;

/*
 * The interval timer, an 8254 at ports 40h to 43h whose three counters
 * count PIT_HZ clocks a second: counter 0 raises interrupt line 0 at each
 * period of its count, 65536 (54.925 ms) unless the guest loads another;
 * counter 1 refreshes memory; counter 2, gated by bit 0 of port 61h,
 * drives the speaker, whose sound goes nowhere, and shows its output in
 * bit 5 of that port. Modes 0, 2 and 3 are counted; 1, 4 and 5 count as
 * mode 0 does, with no gate trigger. A count loaded while counting takes
 * effect at once rather than at the end of the period.
 *
 * Time is a count of these clocks. At each poll it is the host's clock,
 * or under -T PIT_POLL a poll; between polls it moves on by the
 * instructions run (pc_step) times pit_step, the clocks an instruction
 * took over the last running stretch, so that two reads of a counter in
 * one poll differ as they would on the machine. It never runs backwards.
 * A timer faster than a poll shortens the next run of instructions to
 * the time left before its interrupt (pc_slice).
 */
#define PIT_HZ		1193182u
#define PIT_POLL	5966u		/* PC_POLL_MS in clocks, under -T */
#define PIT_TSTEP	745u		/* clocks a step in 256ths, under -T */
#define PIT_LEARN	59659u		/* clocks of running that set pit_step */
#define SLICE_MIN	64u		/* the fewest steps between polls */

struct pit {
	uint32_t t0;		/* the clock counting started at */
	uint32_t held;		/* clocks counted when gate 2 went low */
	uint32_t count;		/* the count loaded, 1 to 65536 */
	uint16_t latch;		/* the count latched for reading */
	uint8_t mode, rw, bcd;	/* the last control word's fields */
	uint8_t armed;		/* a count is loaded and counting */
	uint8_t low;		/* the low byte of a count being loaded */
	uint8_t wnext, rnext;	/* the next access is the high byte */
	uint8_t latched;	/* bytes of latch to read */
	uint8_t slatched, status;	/* a read-back status to read */
	uint32_t shown;		/* mode 2: the period whose low pulse was read */
};

static struct pit pit[3];
static uint32_t pit_t;		/* the clock at the last poll */
static uint32_t pit_last;	/* the latest clock a read has seen */
static uint32_t pit_step = PIT_TSTEP;
static uint32_t irq_at;		/* counter 0's next interrupt */
static uint8_t irq_armed, port61;
static uint32_t host_clk, host_rem, run_clk, run_steps;
static uint32_t pit_learn = PIT_POLL;	/* the first estimate comes soon */
static unsigned long host_last;
unsigned pc_step, pc_slice = PC_STEPS;
unsigned long pc_ran;

/*
 * -S: the instructions take as long as on the processor at cf.khz, at
 * cf.cpi10 tenths of a clock each (main.c sets it from -c), the figures
 * of the loop of docs/research/i86-dos-emulator/measure/speed.asm that
 * docs/research/i86-dos-emulator/cpu-timing.md derives from Intel's
 * manuals and checks against silicon captures. The host waits out the
 * time the guest is ahead; a guest behind, as on a board slower than the
 * clock asked for, keeps at most 20 ms of credit, and a step back of the
 * host's clock starts the pace again from there.
 */
static unsigned long thr_due;
static uint32_t thr_rem;

unsigned long
pc_target(void)
{
	return (cf.khz && !cf.fixed_clock ? cf.khz * 10000UL / cf.cpi10 : 0);
}

static void
throttle(uint32_t steps)
{
	unsigned long now = host_us();
	uint32_t n = steps * cf.cpi10 * 100u + thr_rem;

	thr_due += n / cf.khz;
	thr_rem = n % cf.khz;
	if ((long)(now - thr_due) > 20000L)
		thr_due = now - 20000UL;
	else if ((long)(thr_due - now) > 100000L)
		thr_due = now;	/* the host's clock stepped back */
	else if ((long)(thr_due - now) >= 1000L)
		host_sleep((unsigned)((thr_due - now) / 1000UL));
}

/* With I86_TRACE defined, a host build reports each firmware function and
 * port the machine does not implement, one line a use on standard error;
 * sys/arch/rp2040/doc/I86.md holds the table this produced for three DOS
 * systems. */
#ifdef I86_TRACE
#define TRACE(what, a, b)	fprintf(stderr, "trace %s %02x %04x\n", what, a, b)
#else
#define TRACE(what, a, b)	((void)0)
#endif

static unsigned
bda8(unsigned off)
{
	return (mem_rb(BDA + off));
}

static void
bda8w(unsigned off, unsigned v)
{
	mem_wb(BDA + off, v);
}

/* Terminal output. */

static void
oflush(void)
{
	if (olen)
		host_write(obuf, olen);
	olen = 0;
}

static void
oputc(int ch)
{
	if (olen == sizeof(obuf))
		oflush();
	obuf[olen++] = (char)ch;
}

static void
oputs(const char *s)
{
	while (*s)
		oputc(*s++);
}

static void
onum(unsigned v)
{
	if (v >= 10)
		onum(v / 10);
	oputc('0' + (int)(v % 10));
}

static void
ogoto(unsigned row, unsigned col)
{
	if ((int)row == ty && (int)col == tx)
		return;
	oputs("\033[");
	onum(row + 1);
	oputc(';');
	onum(col + 1);
	oputc('H');
	ty = (int)row;
	tx = (int)col;
}

/* IBM color order is blue, green, red; ANSI is red, green, blue. */
static unsigned
ansi(unsigned c)
{
	return ((c & 2) | (c & 1) << 2 | (c & 4) >> 2);
}

static void
oattr(unsigned a)
{
	if ((int)a == tattr)
		return;
	oputs("\033[0;");
	if (a & 8)
		oputs("1;");
	onum(30 + ansi(a & 7));
	oputc(';');
	onum(40 + ansi((a >> 4) & 7));
	oputc('m');
	tattr = (int)a;
}

/* Code page 437 on a terminal that draws ASCII. */
static int
glyph(unsigned ch)
{
	if (ch >= 0x20 && ch < 0x7f)
		return ((int)ch);
	if (ch == 0 || ch == 0xff)
		return (' ');
	if (ch == 0xb3 || ch == 0xba)
		return ('|');
	if (ch == 0xc4)
		return ('-');
	if (ch == 0xcd)
		return ('=');
	if ((ch >= 0xb0 && ch <= 0xb2) || (ch >= 0xdb && ch <= 0xdf))
		return ('#');
	if (ch >= 0xb4 && ch <= 0xda)
		return ('+');
	if (ch == 0x10 || ch == 0x1a)
		return ('>');
	if (ch == 0x11 || ch == 0x1b)
		return ('<');
	return ('?');
}

static void
refresh(void)
{
	unsigned row, col, i, n = cols * ROWS;

	if (cf.quiet)
		return;
	if (gfx_on) {
		gfx_refresh(cols, bda8(0x51) * cols + bda8(0x50));
		return;
	}
	for (i = 0; i < n; i++) {
		if (vram[2 * i] == shadow[2 * i] &&
		    vram[2 * i + 1] == shadow[2 * i + 1])
			continue;
		shadow[2 * i] = vram[2 * i];
		shadow[2 * i + 1] = vram[2 * i + 1];
		row = i / cols;
		col = i % cols;
		/* A gap of a few unchanged cells is cheaper to draw again
		 * than to address the cursor across. */
		if ((int)row == ty && tx >= 0 && col > (unsigned)tx &&
		    col - (unsigned)tx <= 4) {
			unsigned k;

			for (k = i - (col - (unsigned)tx); k < i; k++) {
				oattr(vram[2 * k + 1]);
				oputc(glyph(vram[2 * k]));
			}
			tx = (int)col;
		}
		ogoto(row, col);
		oattr(vram[2 * i + 1]);
		oputc(glyph(vram[2 * i]));
		/* The column after the last one is terminal-defined. */
		tx = col + 1 < cols ? (int)col + 1 : -1;
	}
	row = bda8(0x51);
	col = bda8(0x50);
	if (row < ROWS && col < cols)
		ogoto(row, col);
	oflush();
}

static void
clear_cells(unsigned from, unsigned n, unsigned attr)
{
	unsigned i;

	for (i = from; i < from + n; i++) {
		vram[2 * i] = ' ';
		vram[2 * i + 1] = (uint8_t)attr;
	}
}

static void
scroll(unsigned top, unsigned left, unsigned bot, unsigned right,
    unsigned n, unsigned attr, int down)
{
	unsigned w, h, r;

	if (bot >= ROWS)
		bot = ROWS - 1;
	if (right >= cols)
		right = cols - 1;
	if (top > bot || left > right)
		return;
	w = right - left + 1;
	h = bot - top + 1;
	if (n == 0 || n > h)
		n = h;
	if (gfx_graphic) {
		gfx_scroll(top, left, bot, right, n, attr, down);
		return;
	}
	if (!down && n < h && w == cols && top == 0 && bot == ROWS - 1 &&
	    !(attr & 0x70) && TERMINAL) {
		/* The terminal scrolls its copy and the frame follows it.
		 * A terminal fills the new lines with its own background,
		 * so only a black fill goes this way. */
		refresh();
		oattr(attr);
		ogoto(ROWS - 1, 0);
		for (r = 0; r < n; r++)
			oputc('\n');
		memmove(shadow, shadow + 2 * cols * n, 2 * cols * (h - n));
		for (r = 2 * cols * (h - n); r < 2 * cols * h; r += 2) {
			shadow[r] = ' ';
			shadow[r + 1] = (uint8_t)attr;
		}
	}
	for (r = 0; r < h - n; r++) {
		unsigned dst = down ? bot - r : top + r;
		unsigned src = down ? dst - n : dst + n;

		memmove(vram + 2 * (dst * cols + left),
		    vram + 2 * (src * cols + left), 2 * w);
	}
	for (r = 0; r < n; r++)
		clear_cells((down ? top + r : bot - r) * cols + left, w, attr);
}

static void
tty_out(unsigned ch, unsigned attr, int useattr)
{
	unsigned row = bda8(0x51), col = bda8(0x50), i;

	if (row >= ROWS)
		row = ROWS - 1;
	if (col >= cols)
		col = cols - 1;
	switch (ch) {
	case 7:
		if (TERMINAL)
			oputc(7);
		break;
	case 8:
		if (col)
			col--;
		break;
	case 10:
		row++;
		break;
	case 13:
		col = 0;
		break;
	default:
		i = row * cols + col;
		if (gfx_graphic) {
			gfx_putc(row, col, ch, attr);
		} else {
			vram[2 * i] = (uint8_t)ch;
			if (useattr)
				vram[2 * i + 1] = (uint8_t)attr;
		}
		if (++col == cols) {
			col = 0;
			row++;
		}
		break;
	}
	if (row == ROWS) {
		row--;
		scroll(0, 0, ROWS - 1, cols - 1, 1,
		    gfx_graphic ? 0 : vram[2 * (row * cols + col) + 1], 0);
	}
	bda8w(0x50, col);
	bda8w(0x51, row);
}

static void
set_mode(unsigned mode)
{
	/* The machine has a color adapter only: no memory answers at B000:0,
	 * so a request for the monochrome mode selects mode 3, as the IBM
	 * firmware does for an equipment word without a monochrome adapter. */
	int graphic;

	if ((mode & 0x7f) == 7)
		mode = (mode & 0x80) | 3;
	/* The graphics modes exist on the panel display alone, which
	 * clears their memory itself; elsewhere their numbers select 80
	 * columns of text. */
	graphic = gfx_on && gfx_mode(mode);
	cols = graphic ? ((mode & 0x7f) == 6 ? 80 : 40) :
	    (mode & 0x7e) == 0 ? 40 : 80;
	bda8w(0x49, mode & 0x7f);
	bda8w(0x4a, cols);
	bda8w(0x4b, 0);
	bda8w(0x4c, graphic ? 0 : (cols * ROWS * 2) & 0xff);
	bda8w(0x4d, graphic ? 0x40 : (cols * ROWS * 2) >> 8);
	bda8w(0x50, 0);
	bda8w(0x51, 0);
	bda8w(0x60, 7);
	bda8w(0x61, 6);
	bda8w(0x62, 0);
	bda8w(0x63, 0xd4);
	bda8w(0x64, 0x03);
	bda8w(0x84, ROWS - 1);
	if (!(mode & 0x80)) {
		if (!graphic)
			clear_cells(0, cols * ROWS, 7);
		if (TERMINAL) {
			memset(shadow, 0, VRAM_SIZE);
			oputs("\033[0m\033[2J");
			tx = ty = tattr = -1;
		}
	}
}

static void
video(struct i86 *c)
{
	unsigned ah = c->r[R_AX] >> 8, al = c->r[R_AX] & 0xff;
	unsigned bl = c->r[R_BX] & 0xff, i, n, row, col;

	if (gfx_graphic && gfx_video(c))
		return;
	/* These functions write a text mode's cells directly, where no
	 * write is noted. A graphics mode's services write through the
	 * watched path and the others change no display memory, so they
	 * mark nothing: every marked page is drawn again. */
	if (!gfx_graphic && (ah == 0x00 || ah == 0x06 || ah == 0x07 ||
	    ah == 0x09 || ah == 0x0a || ah == 0x0e || ah == 0x13))
		mem_watched |= (1u << (VRAM_SIZE >> MEM_PGSHIFT)) - 1;
	switch (ah) {
	case 0x00:
		set_mode(al);
		break;
	case 0x02:
		bda8w(0x50, c->r[R_DX] & 0xff);
		bda8w(0x51, c->r[R_DX] >> 8);
		break;
	case 0x03:
		c->r[R_DX] = (uint16_t)(bda8(0x51) << 8 | bda8(0x50));
		c->r[R_CX] = 0x0607;
		break;
	case 0x06:
	case 0x07:
		scroll(c->r[R_CX] >> 8, c->r[R_CX] & 0xff, c->r[R_DX] >> 8,
		    c->r[R_DX] & 0xff, al, c->r[R_BX] >> 8, ah == 7);
		break;
	case 0x08:
	case 0x09:
	case 0x0a:
		row = bda8(0x51);
		col = bda8(0x50);
		if (row >= ROWS || col >= cols)
			break;
		i = row * cols + col;
		if (gfx_graphic) {
			/* Pixels are not read back as a character. The
			 * count writes within the row: the firmware's
			 * listing gives it valid results on one row only. */
			if (ah == 8)
				c->r[R_AX] = 0;
			for (n = c->r[R_CX]; ah != 8 && n && col < cols; n--)
				gfx_putc(row, col++, al, bl);
			break;
		}
		if (ah == 8) {
			c->r[R_AX] = (uint16_t)(vram[2 * i + 1] << 8 | vram[2 * i]);
			break;
		}
		for (n = c->r[R_CX]; n && i < cols * ROWS; n--, i++) {
			vram[2 * i] = (uint8_t)al;
			if (ah == 9)
				vram[2 * i + 1] = (uint8_t)bl;
		}
		break;
	case 0x0e:
		tty_out(al, bl, 0);
		break;
	case 0x0f:
		c->r[R_AX] = (uint16_t)(cols << 8 | bda8(0x49));
		c->r[R_BX] &= 0xff;
		break;
	case 0x13:
		bda8w(0x50, c->r[R_DX] & 0xff);
		bda8w(0x51, c->r[R_DX] >> 8);
		i = c->r[R_BP];
		for (n = c->r[R_CX]; n; n--) {
			unsigned ch = mem_rb((((uint32_t)c->s[S_ES] << 4) +
			    (i++ & 0xffff)) & (MEM_SIZE - 1));

			if (al & 2)
				bl = mem_rb((((uint32_t)c->s[S_ES] << 4) +
				    (i++ & 0xffff)) & (MEM_SIZE - 1));
			tty_out(ch, bl, 1);
		}
		if (!(al & 1)) {
			bda8w(0x50, c->r[R_DX] & 0xff);
			bda8w(0x51, c->r[R_DX] >> 8);
		}
		break;
	default:	/* cursor shape, pages, palette, EGA and later */
		TRACE("int10", ah, c->r[R_BX]);
		break;
	}
}

/* Keyboard. */

/* The interrupt request line follows the highest request that is unmasked
 * and above everything in service. */
static void
pic_update(struct i86 *c)
{
	unsigned above = pic_isr ? (pic_isr & (0u - pic_isr)) - 1 : 0xff;

	c->irq = (pic_irr & ~(unsigned)imask & above) != 0;
}

/* The processor acknowledges: the vector of the request it takes. */
unsigned
pc_inta(struct i86 *c)
{
	unsigned above = pic_isr ? (pic_isr & (0u - pic_isr)) - 1 : 0xff;
	unsigned req = pic_irr & ~(unsigned)imask & above, n = 0;

	if (req == 0) {
		c->irq = 0;
		return (pic_base + 7u);	/* the controller's spurious level */
	}
	while (!(req >> n & 1))
		n++;
	pic_irr &= (uint8_t)~(1u << n);
	/* With automatic end of interrupt nothing stays in service. */
	if (!pic_aeoi)
		pic_isr |= (uint8_t)(1u << n);
	pic_update(c);
	return (pic_base + n);
}

/* The firmware's key buffer in the data area: head at 40:1A, tail at
 * 40:1C, sixteen words from 40:1E. */
static int
bq_empty(void)
{
	return (mem_rw(BDA + 0x1a) == mem_rw(BDA + 0x1c));
}

/* Returns 0 when the buffer is full and the key is dropped. */
static int
bq_put(unsigned code)
{
	unsigned tail = mem_rw(BDA + 0x1c), next = tail + 2;

	if (tail < 0x1e || tail > 0x3c)		/* the guest's own value */
		return (0);
	if (next == 0x3e)
		next = 0x1e;
	if (next == mem_rw(BDA + 0x1a))
		return (0);
	mem_ww(BDA + tail, code);
	mem_ww(BDA + 0x1c, next);
	return (1);
}

static void
kb_put(unsigned scan, unsigned code)
{
	unsigned i;

	if (kb_n == KB_N)
		return;
	i = (kb_head + kb_n++) % KB_N;
	kb_scan[i] = (uint8_t)scan;
	kb_code[i] = (uint16_t)code;
}

/* One key press, as key_code() describes it, on its way to port 60h. */
static void
key_type(unsigned key)
{
	unsigned scan = (key >> 8) & 0x7f;

	if (scan == 0) {		/* no key of the keyboard makes it */
		bq_put(key & 0xffff);
		return;
	}
	if (key & K_SHIFT)
		kb_put(0x2a, 0);
	if (key & K_CTRL)
		kb_put(0x1d, 0);
	if (key & K_ALT)
		kb_put(0x38, 0);
	kb_put(scan, key & 0xffff);
	kb_put(scan | 0x80, 0);
	if (key & K_ALT)
		kb_put(0xb8, 0);
	if (key & K_CTRL)
		kb_put(0x9d, 0);
	if (key & K_SHIFT)
		kb_put(0xaa, 0);
}

/* The next scan byte reaches port 60h once the last one has been taken
 * and its interrupt has ended. */
static void
kb_next(void)
{
	if (kb_n == 0 || kb_full || ((pic_irr | pic_isr) & 2))
		return;
	kb_latch = kb_scan[kb_head];
	kb_latch_code = kb_code[kb_head];
	kb_head = (kb_head + 1) % KB_N;
	kb_n--;
	kb_full = 1;
	pic_irr |= 2;
}

/* The firmware's keyboard interrupt, behind vector 9. */
static void
key_irq(void)
{
	static const uint8_t mod[] = { 0x2a, 0x02, 0x1d, 0x04, 0x38, 0x08 };
	unsigned i, flags = bda8(0x17);

	for (i = 0; i < sizeof(mod); i += 2)
		if ((kb_latch & 0x7f) == mod[i]) {
			if (kb_latch & 0x80)
				flags &= ~(unsigned)mod[i + 1];
			else
				flags |= mod[i + 1];
		}
	bda8w(0x17, flags);
	if (kb_latch_code)
		bq_put(kb_latch_code);
	kb_latch_code = 0;
	kb_full = 0;
}

/* Scan codes 02h-35h in order, unshifted and shifted. */
static const char keys_lo[] =
    "1234567890-=\b\tqwertyuiop[]\r\001asdfghjkl;'`\002\\zxcvbnm,./";
static const char keys_hi[] =
    "!@#$%^&*()_+\b\tQWERTYUIOP{}\r\001ASDFGHJKL:\"~\002|ZXCVBNM<>?";

static unsigned
key_code(unsigned ch)
{
	const char *p;

	if (ch == ' ')
		return (0x3920);
	if (ch == 0x1b)
		return (0x011b);
	if (ch == 0x7f)
		return (0x0e08);
	if (ch == '\n')
		ch = '\r';
	if (ch > 2 && ch < 0x80) {
		if ((p = strchr(keys_lo, (int)ch)) != NULL)
			return ((unsigned)(p - keys_lo + 2) << 8 | ch);
		if ((p = strchr(keys_hi, (int)ch)) != NULL)
			return (K_SHIFT | (unsigned)(p - keys_hi + 2) << 8 | ch);
	}
	if (ch >= 1 && ch <= 26 &&
	    (p = strchr(keys_lo, (int)ch + 0x60)) != NULL)
		return (K_CTRL | (unsigned)(p - keys_lo + 2) << 8 | ch);
	return (ch);
}

/* COM1's interrupt output: a rising edge is a request on its line. */
static void
ser_edge(void)
{
	int level = ser_line();

	if (level && !ser_level)
		pic_irr |= 1u << SER_LINE;
	ser_level = level;
}

/* Mouse keys. An arrow moves the mouse MK_STEP mickeys; a press of the
 * same arrow within MK_TICKS timer ticks of the last one, as the keyboard
 * sends while a key is held, moves twice as far as that one, up to MK_MAX.
 * dir is 0 to 3 for up, down, right and left. */
static void
mouse_arrow(unsigned dir)
{
	static const signed char mx[] = { 0, 0, 1, -1 }, my[] = { -1, 1, 0, 0 };

	if (dir == mk_dir && nticks - mk_tick <= MK_TICKS)
		mk_step = mk_step < MK_MAX ? 2 * mk_step : MK_MAX;
	else
		mk_step = MK_STEP;
	mk_dir = dir;
	mk_tick = nticks;
	mouse_report(mx[dir] * (int)mk_step, my[dir] * (int)mk_step,
	    mouse_held);
}

/* [ and ] click the left and right button, { and } press it and leave it
 * held, for dragging, until either key of that button comes again. */
static void
mouse_button(unsigned ch)
{
	unsigned b = (ch == '[' || ch == '{') ? MOUSE_LEFT : MOUSE_RIGHT;

	if (mouse_held & b)
		mouse_held &= ~b;
	else if (ch == '{' || ch == '}')
		mouse_held |= b;
	else
		mouse_report(0, 0, mouse_held | b);
	mouse_report(0, 0, mouse_held);
}

/* The bytes after ESC: a cursor or function key, or Alt with a key. */
static void
key_escape(void)
{
	static const uint8_t tilde[] = {
		0, 0x47, 0x52, 0x53, 0x4f, 0x49, 0x51, 0, 0, 0, 0,
		0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0, 0x40, 0x41, 0x42, 0x43, 0x44
	};
	static const char letter[] = "ABCDHF";
	static const uint8_t lcode[] = { 0x48, 0x50, 0x4d, 0x4b, 0x47, 0x4f };
	const char *p;
	unsigned n = 0;
	int ch;

	if (host_ready(20) <= 0) {
		key_type(0x011b);
		return;
	}
	ch = host_getc();
	if (ch != '[' && ch != 'O') {
		key_type(K_ALT | (key_code((unsigned)ch) & 0xff00));
		return;
	}
	for (;;) {
		if (host_ready(20) <= 0)
			return;
		ch = host_getc();
		if (ch >= '0' && ch <= '9')
			n = n * 10 + (unsigned)(ch - '0');
		else if (ch != ';')
			break;
	}
	if (ch == '~' && n < sizeof(tilde) && tilde[n])
		key_type((unsigned)tilde[n] << 8);
	else if (ch >= 'P' && ch <= 'S')
		key_type((unsigned)(0x3b + ch - 'P') << 8);
	else if (ch > 0 && (p = strchr(letter, ch)) != NULL) {
		if (mouse_keys && p - letter < 4)
			mouse_arrow((unsigned)(p - letter));
		else
			key_type((unsigned)lcode[p - letter] << 8);
	}
}

static void menu(void);
static void dpt_set(unsigned i);

static void
key_pump(void)
{
	int ch;

	while (!key_eof && kb_n <= KB_N - KB_KEY && host_ready(0) != 0) {
		ch = host_getc();
		if (ch < 0) {
			key_eof = 1;
			break;
		}
		if (ch == 0x1d) {	/* Ctrl-]: the menu, or out when quiet */
			if (cf.quiet)
				host_exit(0);
			menu();
			continue;
		}
		if (ch == 0x1c) {	/* Ctrl-\: mouse keys on or off */
			mouse_keys = !mouse_keys;
			gfx_pointer(mouse_keys);
			continue;
		}
		if (mouse_keys && ch != 0 && strchr("[]{}", ch) != NULL) {
			mouse_button((unsigned)ch);
			continue;
		}
		if (ch == 0x1b)
			key_escape();
		else
			key_type(key_code((unsigned)ch));
	}
}

/* Nothing in the queue: type the next scripted key if the guest has waited
 * long enough, leave when no key can ever arrive, otherwise wait. */
static void
key_wait(int block)
{
	if (key_polls++ == 0)
		poll_tick = nticks;
	if (cf.keys != NULL) {
		if (kb_n || kb_full) {	/* the last key is still arriving */
			pc_idle = 1;
			return;
		}
		if (!block && nticks - poll_tick < TYPE_TICKS)
			return;
		if (*cf.keys == '\0')
			host_exit(0);
		key_type(key_code((unsigned char)*cf.keys++));
		key_polls = 0;
		pc_idle = 1;
		return;
	}
	key_pump();
	if (!bq_empty())
		return;
	if (idle_tick != nticks) {
		idle_tick = nticks;
		idle_polls = 0;
	}
	/* A guest that blocks, or asks more than IDLE_POLLS times within
	 * one tick, is waiting for the keyboard: it sleeps to the next
	 * tick, and at the end of input the session is over. One that
	 * looks at the keyboard now and then while it works is left to
	 * work, whatever the input holds. */
	if (block || ++idle_polls > IDLE_POLLS) {
		if (key_eof && kb_n == 0 && !kb_full)
			host_exit(0);
		pc_idle = 1;
	}
}

static int
keyboard(struct i86 *c)
{
	unsigned head;

	switch (c->r[R_AX] >> 8) {
	case 0x00:
	case 0x10:
		if (bq_empty()) {
			key_wait(1);
			if (bq_empty()) {
				/* Run this trap again, with interrupts
				 * let in between the runs as the firmware's
				 * wait loop does: the keyboard interrupt
				 * brings the key and the clock keeps time. */
				c->ip -= 3;
				c->fl |= F_IF;
				return (1);
			}
		}
		key_polls = 0;
		head = mem_rw(BDA + 0x1a);
		c->r[R_AX] = (uint16_t)mem_rw(BDA + head);
		mem_ww(BDA + 0x1a, head + 2 >= 0x3e ? 0x1e : head + 2);
		break;
	case 0x01:
	case 0x11:
		if (bq_empty())
			key_wait(0);
		if (!bq_empty()) {
			c->r[R_AX] = (uint16_t)mem_rw(BDA + mem_rw(BDA + 0x1a));
			c->fl &= (uint16_t)~F_ZF;
		} else
			c->fl |= F_ZF;
		break;
	case 0x02:
	case 0x12:
		c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xff00) | bda8(0x17));
		break;
	case 0x05:
		/* AL reports a full buffer. */
		c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xff00) |
		    !bq_put(c->r[R_CX]));
		break;
	default:
		TRACE("int16", c->r[R_AX] >> 8, c->r[R_AX]);
		break;
	}
	return (2);
}

/* Disks. */

/* Open the image for drive i. Returns 0, or -1 with the drive empty when
 * the file cannot serve as a disk. */
static int
disk_open(unsigned i, const char *path)
{
	static const struct { uint16_t kb; uint8_t cyl, heads, spt; } fmt[] = {
		{ 160, 40, 1, 8 }, { 180, 40, 1, 9 }, { 320, 40, 2, 8 },
		{ 360, 40, 2, 9 }, { 720, 80, 2, 9 }, { 1200, 80, 2, 15 },
		{ 1440, 80, 2, 18 }, { 2880, 80, 2, 36 }
	};
	struct disk *d = &disks[i];
	struct stat st;
	uint8_t mbr[512];
	unsigned k;

	d->fd = -1;
	if (path == NULL)
		return (0);
	d->ro = (uint8_t)cf.readonly;
	if (d->ro || (d->fd = open(path, O_RDWR)) < 0) {
		d->ro = 1;
		d->fd = open(path, O_RDONLY);
	}
	if (d->fd < 0 || fstat(d->fd, &st) < 0 || st.st_size < 512) {
		if (d->fd >= 0)
			close(d->fd);
		d->fd = -1;
		return (-1);
	}
	disk_path[i] = path;
	d->nsec = (uint32_t)(st.st_size / 512);
	d->heads = 16;
	d->spt = 63;
	if (i < 2) {
		for (k = 0; k < sizeof(fmt) / sizeof(fmt[0]) - 1; k++)
			if (d->nsec <= fmt[k].kb * 2u)
				break;
		d->heads = fmt[k].heads;
		d->spt = fmt[k].spt;
		d->cyl = fmt[k].cyl;
		return (0);
	}
	/* The first partition's last sector names the geometry it was
	 * made with. */
	if (read(d->fd, mbr, 512) == 512 && mbr[510] == 0x55 &&
	    mbr[511] == 0xaa && (mbr[0x1be + 6] & 63) != 0 &&
	    mbr[0x1be + 5] != 0xff) {
		d->heads = (uint8_t)(mbr[0x1be + 5] + 1);
		d->spt = mbr[0x1be + 6] & 63;
	}
	k = d->nsec / ((unsigned)d->heads * d->spt);
	d->cyl = (uint16_t)(k > 1024 ? 1024 : k < 1 ? 1 : k);
	return (0);
}

static int
is_image(const char *name)
{
	size_t n = strlen(name);

	return (n > 4 && name[n - 4] == '.' &&
	    (name[n - 3] | 0x20) == 'i' && (name[n - 2] | 0x20) == 'm' &&
	    ((name[n - 1] | 0x20) == 'g' || (name[n - 1] | 0x20) == 'a'));
}

/* A file's name on the terminal: a byte that is not a printing ASCII
 * character could be a terminal's command, and shows as a question mark. */
static void
oname(const char *s)
{
	for (; *s; s++)
		oputc(*s >= 0x20 && *s < 0x7f ? *s : '?');
}

/* A line typed at the terminal, echoed; Escape gives it up. */
static unsigned
menu_line(char *buf, unsigned size)
{
	unsigned n = 0;
	int ch;

	for (;;) {
		oflush();
		ch = host_getc();
		if (ch < 0 || ch == 0x1b)
			return (0);
		if (ch == '\r' || ch == '\n')
			break;
		if (ch == 8 || ch == 0x7f) {
			if (n) {
				n--;
				oputs("\b \b");
			}
		} else if (ch >= ' ' && n < size - 1) {
			buf[n++] = (char)ch;
			oputc(ch);
		}
	}
	buf[n] = '\0';
	return (n);
}

/* Control-] on a drawn display: leave, or put another image in a floppy
 * drive. The images offered are the files named *.img and *.ima beside
 * the drive's current one, and a name without a slash is taken from
 * there. DOS notices the change itself: the drives report no change line,
 * so it reads the disk's parameters again before trusting what it holds. */
static void
menu(void)
{
	static char path[2][96];
	char name[48], dir[96], full[96];
	int panel = gfx_on, graphic = gfx_graphic;
	const char *slash;
	struct disk old;
	struct direct *e;
	DIR *dp;
	unsigned i;
	size_t n;
	int ch, known = 1;

	/* The console draws nothing while the panel is this program's. */
	gfx_close();
	/* One line that fits the 40 columns of the PicoCalc's DOS screen. */
	oputs("\033[0m\033[2J\033[H"
	    "i86: q quit, a b floppy, other: back\r\n");
	oflush();
	ch = host_getc();
	if (ch == 'a' || ch == 'b') {
		i = (unsigned)(ch - 'a');
		strcpy(dir, ".");
		if (disk_path[i] != NULL &&
		    (slash = strrchr(disk_path[i], '/')) != NULL) {
			n = (size_t)(slash - disk_path[i]);
			if (n == 0)
				n = 1;		/* the root directory */
			if (n < sizeof(dir)) {
				memcpy(dir, disk_path[i], n);
				dir[n] = '\0';
			} else
				known = 0;
		}
		if (!known) {
			/* Another directory's images are not offered in
			 * place of the ones beside the current image. */
			oputs("the image's directory name is too long; "
			    "press a key");
			oflush();
			(void)host_getc();
		} else if ((dp = opendir(dir)) != NULL) {
			while ((e = readdir(dp)) != NULL)
				if (is_image(e->d_name)) {
					oname(e->d_name);
					oputs("  ");
				}
			closedir(dp);
		}
		if (known)
			oputs("\r\nimage: ");
		if (known && menu_line(name, sizeof(name)) != 0) {
			if (strchr(name, '/') != NULL)
				n = (size_t)snprintf(full, sizeof(full), "%s", name);
			else
				n = (size_t)snprintf(full, sizeof(full), "%s/%s",
				    dir, name);
			old = disks[i];
			slash = disk_path[i];
			if (n >= sizeof(full) || disk_open(i, full) != 0) {
				disks[i] = old;
				disk_path[i] = slash;
				oputs("\r\ncannot use that image; press a key");
				oflush();
				(void)host_getc();
			} else {
				if (old.fd >= 0)
					close(old.fd);
				strcpy(path[i], full);
				disk_path[i] = path[i];
				dpt_set(i);
			}
		}
	}
	/* Draw the guest's screen again from nothing, also on the way out,
	 * so that the session ends on what the guest last showed. A panel
	 * that cannot be taken back leaves the terminal as the display,
	 * which has no graphics mode: the failed gfx_open() has put display
	 * memory back to a text mode's cells and returned the borrowed
	 * frames, and the mode set gives the guest a text mode's numbers in
	 * the BIOS data area and blank cells. */
	if (panel && gfx_open() != 0) {
		oputs("\033[1;25r\033[?7l");
		if (graphic)
			set_mode(cols == 40 ? 1 : 3);
	}
	if (!gfx_on) {
		memset(shadow, 0, VRAM_SIZE);
		oputs("\033[2J");
		tx = ty = tattr = -1;
	}
	if (ch == 'q' || ch < 0)
		host_exit(0);
}

/* The parameter table of floppy drive i for the format of its image:
 * sectors a track, and the read and format gaps that go with them. */
static void
dpt_set(unsigned i)
{
	static const uint8_t dpt[DPT_SIZE] = {
		0xdf, 0x02, 0x25, 0x02, 18, 0x1b, 0xff, 0x6c, 0xf6, 0x0f, 0x08
	};
	uint8_t *p = rom + ROM_DPT + DPT_STEP * i;
	unsigned spt = disks[i].fd >= 0 ? disks[i].spt : 18;

	memcpy(p, dpt, sizeof(dpt));
	p[4] = (uint8_t)spt;
	p[5] = spt >= 15 ? 0x1b : 0x2a;
	p[7] = spt == 18 ? 0x6c : spt == 15 ? 0x54 : spt == 36 ? 0x53 : 0x50;
}

static struct disk *
disk_of(unsigned dl)
{
	struct disk *d;

	if (dl < 2)
		d = &disks[dl];
	else if (dl >= 0x80 && dl < 0x82)
		d = &disks[dl - 0x80 + 2];
	else
		return (NULL);
	return (d->fd < 0 ? NULL : d);
}

/* Move n bytes between buf and guest memory at seg:off. */
static void
guest_copy(unsigned seg, unsigned off, uint8_t *buf, unsigned n, int toguest)
{
	uint32_t len;
	uint8_t *p;

	while (n) {
		off &= 0xffff;
		p = mem_span((((uint32_t)seg << 4) + off) & (MEM_SIZE - 1),
		    toguest, &len);
		if (len > n)
			len = n;
		if (len > 0x10000 - off)
			len = 0x10000 - off;
		if (p != NULL) {
			if (toguest)
				memcpy(p, buf, len);
			else
				memcpy(buf, p, len);
		} else if (!toguest)
			memset(buf, 0xff, len);
		buf += len;
		off += len;
		n -= len;
	}
}

static unsigned
disk_rw(struct i86 *c, struct disk *d, unsigned fn)
{
	static uint8_t buf[512];
	unsigned n = c->r[R_AX] & 0xff, cl = c->r[R_CX] & 0xff;
	unsigned cyl = (c->r[R_CX] >> 8) | (cl & 0xc0) << 2;
	unsigned head = c->r[R_DX] >> 8, sec = cl & 63, off = c->r[R_BX];
	uint32_t lba;
	unsigned done;

	if (sec == 0 || sec > d->spt || head >= d->heads)
		return (0x04);
	lba = ((uint32_t)cyl * d->heads + head) * d->spt + sec - 1;
	if (fn == 3 && d->ro)
		return (0x03);
	for (done = 0; done < n; done++, lba++, off += 512) {
		if (lba >= d->nsec ||
		    lseek(d->fd, (off_t)lba * 512, SEEK_SET) < 0)
			break;
		if (fn == 3) {
			guest_copy(c->s[S_ES], off, buf, 512, 0);
			if (write(d->fd, buf, 512) != 512)
				break;
		} else {
			if (read(d->fd, buf, 512) != 512)
				break;
			if (fn == 2)
				guest_copy(c->s[S_ES], off, buf, 512, 1);
		}
	}
	c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xff00) | done);
	return (done == n ? 0 : 0x04);
}

static void
disk_service(struct i86 *c)
{
	unsigned ah = c->r[R_AX] >> 8, dl = c->r[R_DX] & 0xff, st = 0, n;
	struct disk *d = disk_of(dl);
	int hard = dl >= 0x80;

	if (d == NULL && ah != 0 && ah != 8 && ah != 0x15)
		st = hard ? 0x01 : 0x80;
	else
		switch (ah) {
		case 0x00: case 0x0c: case 0x0d: case 0x10: case 0x11:
		case 0x14: case 0x16:
			break;
		case 0x01:
			st = bda8(hard ? 0x74 : 0x41);
			break;
		case 0x02: case 0x03: case 0x04:
			st = disk_rw(c, d, ah);
			break;
		case 0x08:
			n = hard ? (disks[2].fd >= 0) + (disks[3].fd >= 0) :
			    (disks[0].fd >= 0) + (disks[1].fd >= 0);
			if (d == NULL) {
				st = 0x07;
				c->r[R_CX] = 0;
				c->r[R_DX] = (uint16_t)n;
				break;
			}
			c->r[R_CX] = (uint16_t)(((d->cyl - 1) & 0xff) << 8 |
			    ((d->cyl - 1) >> 8) << 6 | d->spt);
			c->r[R_DX] = (uint16_t)((d->heads - 1) << 8 | n);
			if (!hard) {
				c->r[R_BX] = d->spt == 36 ? 6 : d->spt == 18 ?
				    4 : d->spt == 15 ? 2 : d->cyl == 80 ? 3 : 1;
				dpt_set(dl);
				c->s[S_ES] = 0xf000;
				c->r[R_DI] = (uint16_t)(ROM_OFF + ROM_DPT +
				    DPT_STEP * dl);
			}
			break;
		case 0x15:
			if (d == NULL) {
				c->r[R_AX] &= 0xff;
			} else if (hard) {
				c->r[R_AX] = (uint16_t)(0x0300 | (c->r[R_AX] & 0xff));
				c->r[R_CX] = (uint16_t)(d->nsec >> 16);
				c->r[R_DX] = (uint16_t)d->nsec;
			} else
				c->r[R_AX] = (uint16_t)(0x0100 | (c->r[R_AX] & 0xff));
			c->fl &= (uint16_t)~F_CF;
			return;
		default:
			TRACE("int13", ah, dl);
			st = 0x01;
			break;
		}
	bda8w(hard ? 0x74 : 0x41, st);
	if (ah != 0x15)
		c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xff) | st << 8);
	if (st)
		c->fl |= F_CF;
	else
		c->fl &= (uint16_t)~F_CF;
}

static int
boot(struct i86 *c)
{
	static const char msg[] = "i86: no bootable disk\r\n";
	static uint8_t buf[512];
	unsigned i;

	for (i = 0; i < 4; i += 2) {
		struct disk *d = &disks[i];

		if (d->fd < 0 || lseek(d->fd, 0, SEEK_SET) < 0 ||
		    read(d->fd, buf, 512) != 512)
			continue;
		if (buf[510] != 0x55 || buf[511] != 0xaa)
			continue;
		guest_copy(0, 0x7c00, buf, 512, 1);
		c->r[R_DX] = i ? 0x80 : 0;
		c->s[S_CS] = c->s[S_DS] = c->s[S_ES] = c->s[S_SS] = 0;
		c->ip = 0x7c00;
		c->r[R_SP] = 0x7c00;
		i86_setfl(c, F_IF);
		return (1);
	}
	pc_restore(1);
	host_write(msg, sizeof(msg) - 1);
	host_exit(1);
	return (1);
}

/* Clock. */

static unsigned
bcd(unsigned v)
{
	return ((v / 10) << 4 | v % 10);
}

/* The calendar: the host's, or 1990-01-01 12:00:00 when the clock is
 * fixed. */
static struct tm *
calendar(void)
{
	time_t now = cf.fixed_clock ? (time_t)631195200L : time(NULL);

	return (cf.fixed_clock ? gmtime(&now) : localtime(&now));
}

/* The battery-backed memory of the AT behind ports 70h and 71h: the clock
 * in BCD and 24-hour form, 640 KB of base memory and no extended memory.
 * Every other cell reads zero. */
static unsigned
cmos(unsigned i)
{
	struct tm *tm = calendar();

	if (tm == NULL)
		return (0);
	switch (i) {
	case 0x00: return (bcd((unsigned)tm->tm_sec));
	case 0x02: return (bcd((unsigned)tm->tm_min));
	case 0x04: return (bcd((unsigned)tm->tm_hour));
	case 0x06: return ((unsigned)tm->tm_wday + 1);
	case 0x07: return (bcd((unsigned)tm->tm_mday));
	case 0x08: return (bcd((unsigned)tm->tm_mon + 1));
	case 0x09: return (bcd((unsigned)tm->tm_year % 100));
	case 0x0a: return (0x26);
	case 0x0b: return (0x02);
	case 0x0d: return (0x80);
	case 0x15: return (cf.ram_kb & 0xff);
	case 0x16: return (cf.ram_kb >> 8);
	case 0x32: return (bcd(((unsigned)tm->tm_year + 1900) / 100));
	default: return (0);
	}
}

static void
clock_service(struct i86 *c)
{
	struct tm *tm;
	unsigned ah = c->r[R_AX] >> 8;

	c->fl &= (uint16_t)~F_CF;
	switch (ah) {
	case 0x00:
		c->r[R_DX] = (uint16_t)mem_rw(BDA + 0x6c);
		c->r[R_CX] = (uint16_t)mem_rw(BDA + 0x6e);
		c->r[R_AX] = (uint16_t)bda8(0x70);
		bda8w(0x70, 0);
		break;
	case 0x01:
		mem_ww(BDA + 0x6c, c->r[R_DX]);
		mem_ww(BDA + 0x6e, c->r[R_CX]);
		break;
	case 0x02:
	case 0x04:
		tm = calendar();
		if (tm == NULL) {
			c->fl |= F_CF;
			break;
		}
		if (ah == 2) {
			c->r[R_CX] = (uint16_t)(bcd((unsigned)tm->tm_hour) << 8 |
			    bcd((unsigned)tm->tm_min));
			c->r[R_DX] = (uint16_t)(bcd((unsigned)tm->tm_sec) << 8);
		} else {
			unsigned y = (unsigned)tm->tm_year + 1900;

			c->r[R_CX] = (uint16_t)(bcd(y / 100) << 8 | bcd(y % 100));
			c->r[R_DX] = (uint16_t)(bcd((unsigned)tm->tm_mon + 1) << 8 |
			    bcd((unsigned)tm->tm_mday));
		}
		break;
	default:
		TRACE("int1a", ah, c->r[R_CX]);
		c->fl |= F_CF;
		break;
	}
}

static void
tick(void)
{
	uint32_t t = mem_rw(BDA + 0x6c) | (uint32_t)mem_rw(BDA + 0x6e) << 16;

	if (++t >= 0x1800b0) {
		t = 0;
		bda8w(0x70, 1);
	}
	mem_ww(BDA + 0x6c, t & 0xffff);
	mem_ww(BDA + 0x6e, t >> 16);
}

int
pc_trap(struct i86 *c, unsigned n)
{
	switch (n) {
	case TRAP_TICK:
		tick();
		return (1);
	case TRAP_KEY:
		key_irq();
		return (1);
	case 0x67:
		if (ems_on)
			ems_int(c);
		else
			TRACE("int", n, c->r[R_AX]);
		break;
	case 0x10:
		video(c);
		break;
	case 0x11:
		c->r[R_AX] = (uint16_t)mem_rw(BDA + 0x10);
		break;
	case 0x12:
		c->r[R_AX] = (uint16_t)mem_rw(BDA + 0x13);
		break;
	case 0x13:
		disk_service(c);
		return (2);
	case 0x14:
		c->r[R_AX] = (uint16_t)(0x8000 | (c->r[R_AX] & 0xff));
		break;
	case 0x15:
		switch (c->r[R_AX] >> 8) {
		case 0x88:
			c->r[R_AX] = 0;
			c->fl &= (uint16_t)~F_CF;
			break;
		case 0x86:
			pc_idle = 1;
			/* FALLTHROUGH */
		case 0x90:
		case 0x91:
			c->r[R_AX] &= 0xff;
			c->fl &= (uint16_t)~F_CF;
			break;
		default:
			TRACE("int15", c->r[R_AX] >> 8, c->r[R_AX]);
			c->r[R_AX] = (uint16_t)(0x8600 | (c->r[R_AX] & 0xff));
			c->fl |= F_CF;
			break;
		}
		return (2);
	case 0x16:
		return (keyboard(c));
	case 0x17:
		c->r[R_AX] = (uint16_t)(0x0100 | (c->r[R_AX] & 0xff));
		break;
	case 0x18:
		return (boot(c));
	case 0x19:
		return (boot(c));
	case 0x1a:
		clock_service(c);
		break;
	case 0x06:
		/* An invalid opcode the guest has no handler for. Returning
		 * would run the same instruction again without end. A
		 * program's own INT 6 comes here too and returns. */
		if (cf.model != I86_8086 && c->fault) {
			char msg[64];
			uint32_t sp = ((uint32_t)c->s[S_SS] << 4) + c->r[R_SP];
			int len = snprintf(msg, sizeof(msg),
			    "i86: undefined instruction at %04X:%04X\n",
			    mem_rw((sp + 2) & (MEM_SIZE - 1)),
			    mem_rw(sp & (MEM_SIZE - 1)));

			pc_restore(1);
			(void)!write(2, msg, (size_t)len);
			host_exit(4);
		}
		break;
	default:
		if (n < 0x20 && n != 0x1c)
			TRACE("int", n, c->r[R_AX]);
		break;
	}
	return (0);
}

/* Ports. */

/* The host's clock in PIT clocks, exact through a carried remainder in
 * steps a 32-bit product holds; a gap over a second counts as one, and a
 * step back of the host's clock as none. */
/* The host's milliseconds for the 55 ms tick, the waits and the serial
 * port, moving on by every forward step of the host's clock and none of a
 * backward one, so that a clock set back keeps neither waiting out the
 * difference. */
static unsigned long
host_now(void)
{
	static unsigned long last, mono;
	unsigned long ms = host_ms(), d = ms - last;

	last = ms;
	if ((long)d > 0)
		mono += d;
	return (mono);
}

static uint32_t
host_clock(void)
{
	unsigned long us = host_us(), d = us - host_last;
	uint32_t k, n;

	host_last = us;
	if ((long)d < 0)		/* the host's clock stepped back */
		d = 0;
	else if (d > 1000000UL)
		d = 1000000UL;
	while (d) {
		k = d > 3598 ? 3598 : (uint32_t)d;
		d -= k;
		n = k * PIT_HZ + host_rem;
		host_clk += n / 1000000u;
		host_rem = n % 1000000u;
	}
	return (host_clk);
}

static uint32_t
pit_now(void)
{
	uint32_t t = pit_t + (pc_step * pit_step >> 8);

	if ((int32_t)(t - pit_last) > 0)
		pit_last = t;
	return (pit_last);
}

/* Clocks counter n has counted: none before its count is loaded, and
 * those up to the fall of its gate while gate 2 is low. */
static uint32_t
pit_elapsed(unsigned n, uint32_t now)
{
	if (!pit[n].armed)
		return (0);
	if (n == 2 && !(port61 & 1))
		return (pit[n].held);
	return (now - pit[n].t0);
}

/* Four decimal digits each way, for a counter in BCD. */
static unsigned
bcd_bin(unsigned v)
{
	return ((v >> 12 & 15) * 1000 + (v >> 8 & 15) * 100 +
	    (v >> 4 & 15) * 10 + (v & 15));
}

static unsigned
bin_bcd(unsigned v)
{
	return ((v / 1000 % 10) << 12 | (v / 100 % 10) << 8 |
	    (v / 10 % 10) << 4 | v % 10);
}

/*
 * The count a read shows. Mode 3 counts down by two twice a period, from
 * the count made even (an odd count loads count - 1): (count + 1) / 2
 * clocks in the high half and (count - 1) / 2 in the low half. A BCD
 * counter wraps at 10000 and reads as four decimal digits.
 */
static unsigned
pit_value(unsigned n, uint32_t now)
{
	struct pit *p = &pit[n];
	uint32_t e = pit_elapsed(n, now), c = p->count, hi, v;

	if (!p->armed)
		return (0);
	switch (p->mode) {
	case 2:
		v = c - e % c;
		break;
	case 3:
		e %= c;
		hi = (c + 1) / 2;
		v = c - (c & 1) - 2 * (e < hi ? e : e - hi);
		break;
	default:	/* on past zero */
		v = p->bcd ? (c + 10000 - e % 10000) % 10000 : c - e;
		break;
	}
	return (p->bcd ? bin_bcd(v % 10000) : v & 0xffff);
}

static unsigned
pit_out(unsigned n, uint32_t now)
{
	struct pit *p = &pit[n];
	uint32_t e = pit_elapsed(n, now);

	if (!p->armed)
		return (p->mode != 0);
	switch (p->mode) {
	case 2:
		/* Low for the one clock at the end of each period: shown
		 * once to the first read after the period ends, since
		 * time moves between reads in steps of whole instructions. */
		if ((n != 2 || (port61 & 1)) && e / p->count != p->shown) {
			p->shown = e / p->count;
			return (0);
		}
		return (1);
	case 3:
		return (n == 2 && !(port61 & 1)) ||
		    e % p->count < (p->count + 1) / 2;
	default:
		return (e >= p->count);
	}
}

/* Counter 0's interrupts due by now, at most four waiting, raised on
 * line 0 when it is free. */
static void
pit_irq(struct i86 *c, uint32_t now)
{
	while (irq_armed && (int32_t)(now - irq_at) >= 0) {
		if (ticks_due < 4)
			ticks_due++;
		if (pit[0].mode == 2 || pit[0].mode == 3) {
			irq_at += pit[0].count;
			if ((int32_t)(now - irq_at) > (int32_t)(PIT_HZ / 2))
				irq_at = now + pit[0].count;
		} else
			irq_armed = 0;
	}
	if (ticks_due && !(pic_irr & 1)) {
		ticks_due--;
		pic_irr |= 1;
		pic_update(c);
	}
}

/* Steps until counter 0's next interrupt when that comes sooner than a
 * poll's worth of clocks, which a periodic count shorter than a poll
 * makes happen; otherwise a whole run. */
static unsigned
slice_left(void)
{
	uint32_t d, poll = PC_STEPS * pit_step >> 8;

	if (!irq_armed ||
	    ((pit[0].mode == 2 || pit[0].mode == 3) && pit[0].count >= poll))
		return (PC_STEPS);
	d = irq_at - pit_now();
	if ((int32_t)d <= 0)
		return (SLICE_MIN);
	if (d >= poll)
		return (PC_STEPS);
	d = (d << 8) / pit_step;
	return (d < SLICE_MIN ? SLICE_MIN : d);
}

static void
pit_load(struct i86 *c, unsigned n, unsigned v)
{
	struct pit *p = &pit[n];
	uint32_t now = pit_now();

	if (p->bcd)
		v = bcd_bin(v);
	p->count = v ? v : p->bcd ? 10000 : 0x10000;
	p->t0 = now;
	p->held = 0;
	p->shown = 0;
	p->armed = 1;
	if (n == 0) {
		irq_at = now + p->count;
		irq_armed = 1;
		pit_irq(c, now);
		/* A faster rate takes effect inside the current run. */
		if (pc_step + slice_left() < pc_slice)
			pc_slice = pc_step + slice_left();
	}
}

static void
pit_latch(unsigned n)
{
	if (!pit[n].latched) {
		pit[n].latch = (uint16_t)pit_value(n, pit_now());
		pit[n].latched = pit[n].rw == 3 ? 2 : 1;
	}
}

static unsigned
pit_read(struct i86 *c, unsigned n)
{
	struct pit *p = &pit[n];
	unsigned v, hi;

	if (p->slatched) {
		p->slatched = 0;
		return (p->status);
	}
	if (n == 0)
		pit_irq(c, pit_now());
	v = p->latched ? p->latch : pit_value(n, pit_now());
	hi = p->rw == 2 || (p->rw == 3 && p->rnext);
	if (p->rw == 3)
		p->rnext ^= 1;
	if (p->latched)
		p->latched--;
	return (hi ? v >> 8 : v & 0xff);
}

static void
pit_write(struct i86 *c, unsigned n, unsigned v)
{
	struct pit *p = &pit[n];

	if (p->rw == 1)
		pit_load(c, n, v);
	else if (p->rw == 2)
		pit_load(c, n, v << 8);
	else if (!p->wnext) {
		/* Mode 0 stops at the first byte of a new count, OUT low. */
		p->low = (uint8_t)v;
		p->wnext = 1;
		if (p->mode == 0) {
			p->armed = 0;
			if (n == 0)
				irq_armed = 0;
		}
	} else {
		p->wnext = 0;
		pit_load(c, n, p->low | v << 8);
	}
}

/* Port 43h: a control word, a counter latch or a read-back. */
static void
pit_control(unsigned v)
{
	unsigned n = v >> 6, k, mode;
	uint32_t now = pit_now();

	if (n == 3) {
		for (k = 0; k < 3; k++) {
			if (!(v & 2u << k))
				continue;
			if (!(v & 0x10) && !pit[k].slatched) {
				pit[k].status = (uint8_t)(pit_out(k, now) << 7 |
				    !pit[k].armed << 6 | pit[k].rw << 4 |
				    pit[k].mode << 1 | pit[k].bcd);
				pit[k].slatched = 1;
			}
			if (!(v & 0x20))
				pit_latch(k);
		}
		return;
	}
	if ((v & 0x30) == 0) {
		pit_latch(n);
		return;
	}
	mode = v >> 1 & 7;
	pit[n].mode = (uint8_t)(mode > 5 ? mode - 4 : mode);
	pit[n].rw = (uint8_t)(v >> 4 & 3);
	pit[n].bcd = (uint8_t)(v & 1);
	pit[n].armed = pit[n].wnext = pit[n].rnext = pit[n].latched = 0;
	pit[n].slatched = 0;
	if (n == 0)
		irq_armed = 0;
}

/* Port 61h: gate 2 in bit 0. Its fall holds the count; its rise restarts
 * it in modes 2 and 3 and lets it go on in the others. */
static void
port61_out(unsigned v)
{
	uint32_t now = pit_now();

	if ((v ^ port61) & 1) {
		if (!(v & 1))
			pit[2].held = now - pit[2].t0;
		else if (pit[2].mode != 2 && pit[2].mode != 3)
			pit[2].t0 = now - pit[2].held;
		else {
			pit[2].t0 = now;
			pit[2].shown = 0;
		}
	}
	port61 = (uint8_t)v;
}

unsigned
pc_in(struct i86 *c, unsigned port, int w)
{
	unsigned v = 0xff;

	/* A word is two byte cycles, at port and port + 1, as the bus makes
	 * them for the machine's eight-bit devices. */
	if (w)
		return (pc_in(c, port, 0) |
		    pc_in(c, (port + 1) & 0xffff, 0) << 8);
	if (port - SER_BASE < 8) {
		v = ser_in(port);
		ser_edge();
		pic_update(c);
		return (v);
	}
	switch (port) {
	case 0x20:
		v = pic_readisr ? pic_isr : pic_irr;
		break;
	case 0x21:
		v = imask;
		break;
	case 0x40:
	case 0x41:
	case 0x42:
		v = pit_read(c, port - 0x40);
		break;
	case 0x60:
		v = kb_latch;
		kb_full = 0;
		break;
	case 0x64:
		v = kb_full;
		break;
	case 0x71:
		v = cmos(cmos_index & 0x7f);
		break;
	case 0x61:
		/* The refresh request toggles every 18 clocks (15 us). */
		v = (port61 & 0x0fu) | (pit_now() / 18 & 1) << 4 |
		    pit_out(2, pit_now()) << 5;
		break;
	case 0x3d5:
		if (crtc_index == 0x0e || crtc_index == 0x0f) {
			v = bda8(0x51) * cols + bda8(0x50);
			v = crtc_index == 0x0e ? v >> 8 : v & 0xff;
		}
		break;
	case 0x3da:
		crt_flip ^= 1;
		v = crt_flip ? 9 : 0;
		break;
	default:
		TRACE("in", 0, port);
		break;
	}
	return (v);
}

void
pc_out(struct i86 *c, unsigned port, unsigned v, int w)
{
	unsigned pos;

	if (w) {
		pc_out(c, port, v & 0xff, 0);
		pc_out(c, (port + 1) & 0xffff, v >> 8 & 0xff, 0);
		return;
	}
	v &= 0xff;
	if (port - SER_BASE < 8) {
		ser_out(port, v);
		ser_edge();
		pic_update(c);
		return;
	}
	switch (port) {
	case 0x20:
		if (v & 0x10) {
			/* Initialization: the words that follow at port
			 * 21h are not masks. */
			pic_icw = (uint8_t)(1 + !(v & 2) + (v & 1));
			pic_icw2 = 1;
			pic_icw4 = v & 1;
			pic_irr = pic_isr = imask = pic_aeoi = 0;
		} else if (v & 0x08) {
			if (v & 2)
				pic_readisr = v & 1;
		} else if (v & 0x20) {
			if (v & 0x40)
				pic_isr &= (uint8_t)~(1u << (v & 7));
			else
				pic_isr &= (uint8_t)(pic_isr - 1);
		}
		pic_update(c);
		break;
	case 0x21:
		if (pic_icw) {
			/* The vector base, then the cascade word of a
			 * controller that is not alone, then the mode. */
			if (pic_icw2)
				pic_base = v & 0xf8;
			else if (pic_icw == 1 && pic_icw4)
				pic_aeoi = v >> 1 & 1;
			pic_icw2 = 0;
			pic_icw--;
		} else
			imask = (uint8_t)v;
		pic_update(c);
		break;
	case 0x40:
	case 0x41:
	case 0x42:
		pit_write(c, port - 0x40, v);
		break;
	case 0x43:
		pit_control(v);
		break;
	case 0x61:
		port61_out(v);
		break;
	case 0x70:
		cmos_index = (uint8_t)v;
		break;
	case 0x3d4:
		crtc_index = (uint8_t)v;
		break;
	case 0x3d8:
	case 0x3d9:
		if (gfx_on)
			gfx_out(port, v);
		else
			TRACE("out", v, port);
		break;
	case 0x3d5:
		if (crtc_index != 0x0e && crtc_index != 0x0f)
			break;
		pos = bda8(0x51) * cols + bda8(0x50);
		pos = crtc_index == 0x0e ? (pos & 0xff) | v << 8 :
		    (pos & 0xff00) | v;
		bda8w(0x50, pos % cols);
		bda8w(0x51, pos / cols);
		break;
	default:
		TRACE("out", v, port);
		break;
	}
}

/*
 * Time: the timer's interrupts, and every 55 ms of the host's clock
 * (nticks) the keyboard and the display. Under -T a poll is PC_POLL_MS
 * and PIT_POLL clocks, or for a run the timer cut short the clocks of its
 * steps, so that a guest that keeps the timer's rate leaves the polls as
 * they were.
 */
void
pc_poll(struct i86 *c)
{
	static uint32_t vt_frac, host_prev;
	unsigned long now;
	uint32_t clk, d, steps = pc_step;
	int waited = 0;
	int part = pc_slice < PC_STEPS && steps >= pc_slice;
	int ran = steps == pc_slice && !pc_idle && !c->halted;

	pc_step = 0;
	pc_ran += steps;
	if (cf.khz && !cf.fixed_clock)
		throttle(steps);
	for (;;) {
		if (cf.fixed_clock) {
			/* A run the timer cut short moves time by its steps; a
			 * guest waiting under a timer faster than a poll moves
			 * it to the next interrupt; any other poll by PIT_POLL. */
			d = 0;
			if (part && !waited)
				d = steps * PIT_TSTEP >> 8;
			else if ((pc_idle || c->halted) && slice_left() < PC_STEPS) {
				d = irq_at - pit_t;
				d = (int32_t)d <= 0 ? 1 : d > PIT_POLL ? PIT_POLL : d;
			}
			if (d) {
				pit_t += d;
				vt_frac += d * 1000u;
				while (vt_frac >= PIT_HZ) {
					vt_frac -= PIT_HZ;
					vtime++;
				}
			} else {
				pit_t += PIT_POLL;
				vtime += PC_POLL_MS;
			}
			now = vtime;
		} else {
			/* The clocks a step takes, learned over running stretches
			 * of PIT_LEARN clocks or more. */
			clk = host_clock();
			if (ran && !waited) {
				run_clk += clk - host_prev;
				run_steps += steps;
				if (run_clk >= pit_learn && run_steps) {
					d = (run_clk << 8) / run_steps;
					pit_step = d < 1 ? 1 : d > 16384 ? 16384 : d;
					run_clk = run_steps = 0;
					pit_learn = PIT_LEARN;
				}
			}
			host_prev = clk;
			pit_t = clk;
			now = host_now();
		}
		if ((long)(now - next_tick) >= 0) {
			next_tick += 55;
			nticks++;
			if ((long)(now - next_tick) > 500)
				next_tick = now + 55;
			if (cf.keys == NULL)
				key_pump();
			refresh();
		}
		pit_irq(c, pit_now());
		kb_next();
		ser_poll(now);
		ser_edge();
		pic_update(c);
		if (!(pc_idle || c->halted) || waited ||
		    (c->irq && (c->fl & F_IF)))
			break;
		/* Halted with interrupts off, the guest never runs again.
		 * A session whose keys are scripted or at their end has
		 * nothing left to wait for. */
		if (c->halted && !(c->fl & F_IF) &&
		    (cf.keys != NULL || key_eof))
			host_exit(0);
		/* The guest waits for a key or an interrupt. */
		refresh();
		if (!cf.fixed_clock) {
			long ms = (long)(next_tick - now);

			if (irq_armed) {
				d = irq_at - pit_now();
				if ((long)(d / (PIT_HZ / 1000)) < ms)
					ms = (int32_t)d > 0 ?
					    (long)(d / (PIT_HZ / 1000)) : 0;
			}
			(void)host_ready(ms > 0 ? (int)ms : 1);
		}
		if (cf.keys == NULL)
			key_pump();
		waited = 1;
	}
	pc_slice = slice_left();
	pc_idle = 0;
}

void
pc_init(struct i86 *c, const struct pc_config *config)
{
	/* The trap, then for the timer the user hook, then the end of
	 * interrupt as guest code: push ax; mov al, 20h; out 20h, al;
	 * pop ax; iret. */
	static const uint8_t timer[] = {
		0x0f, 0xff, TRAP_TICK, 0xcd, 0x1c,
		0x50, 0xb0, 0x20, 0xe6, 0x20, 0x58, 0xcf
	};
	static const uint8_t key[] = {
		0x0f, 0xff, TRAP_KEY,
		0x50, 0xb0, 0x20, 0xe6, 0x20, 0x58, 0xcf
	};
	unsigned i, nfloppy;

	cf = *config;
	for (i = 0; i < 256; i++) {
		rom[4 * i] = 0x0f;
		rom[4 * i + 1] = 0xff;
		rom[4 * i + 2] = (uint8_t)i;
		rom[4 * i + 3] = 0xcf;
		mem_ww(4 * i, ROM_OFF + 4 * i);
		mem_ww(4 * i + 2, 0xf000);
	}
	memcpy(rom + ROM_TIMER, timer, sizeof(timer));
	memcpy(rom + ROM_KEY, key, sizeof(key));
	mem_ww(4 * 8, ROM_OFF + ROM_TIMER);
	mem_ww(4 * 9, ROM_OFF + ROM_KEY);
	mem_ww(4 * 0x1e, ROM_OFF + ROM_DPT);
	/* FFFF:0000 jumps to the bootstrap; the date and model follow. */
	rom_reset[0] = 0xea;
	rom_reset[1] = (uint8_t)(4 * 0x19);
	rom_reset[2] = ROM_OFF >> 8;
	rom_reset[3] = 0x00;
	rom_reset[4] = 0xf0;
	memcpy(rom_reset + 5, "01/01/90", 8);
	rom_reset[14] = cf.model == I86_286 ? 0xfc : 0xfe;
	mem_rom(ROM_ADDR, rom, sizeof(rom));
	mem_rom(ROM_FONT, gfx_font(), 128 * 8);
	mem_rom(RESET_ADDR, rom_reset, sizeof(rom_reset));
	mem_fixed(VRAM_ADDR, vram, VRAM_SIZE);
	gfx_init(vmem);
	if (cf.panel && !cf.quiet && gfx_open() != 0) {
		static const char msg[] =
		    "i86: no panel device; the terminal is the display\r\n";

		(void)!write(2, msg, sizeof(msg) - 1);
	}

	for (i = 0; i < 4; i++)
		if (disk_open(i, cf.disk[i]) != 0) {
			static const char msg[] = "i86: cannot use disk image ";

			host_write(msg, sizeof(msg) - 1);
			host_write(cf.disk[i], (unsigned)strlen(cf.disk[i]));
			host_write("\r\n", 2);
			host_exit(2);
		}
	dpt_set(0);
	dpt_set(1);
	nfloppy = (disks[0].fd >= 0) + (disks[1].fd >= 0);
	/* COM1 at 40:00, and one serial port in the equipment word. */
	mem_ww(BDA + 0x00, SER_BASE);
	mem_ww(BDA + 0x10, (cf.cols == 40 ? 0x10 : 0x20) | 0x200 |
	    (nfloppy ? 1 | (nfloppy - 1) << 6 : 0));
	mem_ww(BDA + 0x13, cf.ram_kb);
	mem_ww(BDA + 0x1a, 0x1e);
	mem_ww(BDA + 0x1c, 0x1e);
	mem_ww(BDA + 0x80, 0x1e);
	mem_ww(BDA + 0x82, 0x3e);
	bda8w(0x75, (disks[2].fd >= 0) + (disks[3].fd >= 0));
	if (TERMINAL)
		oputs("\033[1;25r\033[?7l");
	set_mode(cf.cols == 40 ? 1 : 3);
	next_tick = cf.fixed_clock ? 0 : host_now() + 55;
	/* The timer as the firmware leaves it: counter 0 at 18.2 Hz in
	 * mode 3, counter 1 refreshing, counter 2 loaded for a beep. */
	host_last = thr_due = host_us();
	/* Under -S the clocks a step takes are known while the pace holds. */
	pit_step = cf.fixed_clock ? PIT_TSTEP : cf.khz ?
	    (uint32_t)(PIT_HZ * 256UL / pc_target()) : 16;
	pit[0].mode = 3;
	pit[0].rw = 3;
	pit[1].mode = 2;
	pit[1].rw = 1;
	pit[2].mode = 3;
	pit[2].rw = 3;
	for (i = 0; i < 3; i++) {
		pit[i].count = i == 0 ? 0x10000 : i == 1 ? 18 : 0x533;
		pit[i].armed = 1;
	}
	irq_at = 0;		/* the first at the first poll, as the firmware's tick */
	irq_armed = 1;

	i86_reset(c, cf.model);
	c->s[S_CS] = 0xf000;
	c->ip = (uint16_t)(ROM_OFF + 4 * 0x19 + 3);
	c->r[R_SP] = 0x400;
	(void)boot(c);
}

/* Give the terminal back its margins and a line below the guest's. With
 * draw clear the pending cells stay undrawn and guest memory unread. */
void
pc_restore(int draw)
{
	static int restored;

#ifdef I86_TRACE
	static const uint8_t watched[] = { 8, 9, 0x10, 0x13, 0x15, 0x16, 0x1c };
	unsigned i;

	for (i = 0; draw && i < sizeof(watched); i++)
		if (mem_rw(watched[i] * 4 + 2) != 0xf000)
			TRACE("hooked", watched[i], mem_rw(watched[i] * 4 + 2));
#endif
	if (cf.quiet || restored)
		return;
	/* The frame reads guest memory; when the page store fails under it
	 * host_exit() comes back here, and that second call must still
	 * give the terminal back. */
	if (draw)
		refresh();
	restored = 1;
	if (gfx_on) {
		/* The console repaints its own screen at the close. */
		gfx_close();
		return;
	}
	oputs("\033[0m\033[r\033[?7h\033[26;1H");
	oflush();
}

void
pc_dump(void)
{
	unsigned row, col, end;

	for (row = 0; row < ROWS; row++) {
		for (end = cols; end > 0; end--)
			if (glyph(vram[2 * (row * cols + end - 1)]) != ' ')
				break;
		for (col = 0; col < end; col++)
			oputc(glyph(vram[2 * (row * cols + col)]));
		oputc('\n');
	}
	oflush();
}
