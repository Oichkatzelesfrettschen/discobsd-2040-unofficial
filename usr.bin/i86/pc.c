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
 * end-of-interrupt commands: the timer on line 0, the keyboard on line 1.
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

#include "mem.h"
#include "pc.h"

#define ROWS		25
#define VRAM_ADDR	0xb8000u
#define ROM_ADDR	0xff800u
#define ROM_OFF		0xf800u		/* offset of the ROM in F000 */
#define ROM_TIMER	0x400u		/* ROM offsets of the fixed parts */
#define ROM_DPT		0x410u		/* drive A's table; B's DPT_STEP on */
#define DPT_STEP	0x20u
#define DPT_SIZE	11
#define ROM_RESET	0x7f0u
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
#define TYPE_TICKS	64		/* timer ticks of polling before typing */

struct disk {
	int fd;
	uint32_t nsec;
	uint16_t cyl;
	uint8_t heads, spt, ro;
};

int pc_idle;

static uint8_t vram[0x1000];
static uint8_t shadow[2 * 80 * ROWS];
static uint8_t rom[0x800];
static struct disk disks[4];
static const char *disk_path[4];	/* the image each drive holds */
static struct pc_config cf;

static uint8_t kb_scan[KB_N];		/* scan bytes on their way to port 60h */
static uint16_t kb_code[KB_N];		/* the key code a make byte stands for */
static unsigned kb_head, kb_n, key_polls, nticks, poll_tick;
static uint8_t kb_latch, kb_full;	/* port 60h and its status bit */
static uint16_t kb_latch_code;
static uint8_t pic_irr, pic_isr, pic_readisr, pic_icw;
static unsigned idle_polls, idle_tick;
static int key_eof;

static unsigned cols;
static int tx = -1, ty = -1, tattr = -1;	/* terminal state; -1 unknown */
static char obuf[256];
static unsigned olen;

static unsigned long next_tick, vtime;
static unsigned ticks_due;
static uint8_t imask, crtc_index, pit_flip, cmos_index;
static uint16_t pit_count;

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
	if (!down && n < h && w == cols && top == 0 && bot == ROWS - 1 &&
	    !(attr & 0x70) && !cf.quiet) {
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
		if (!cf.quiet)
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
		vram[2 * i] = (uint8_t)ch;
		if (useattr)
			vram[2 * i + 1] = (uint8_t)attr;
		if (++col == cols) {
			col = 0;
			row++;
		}
		break;
	}
	if (row == ROWS) {
		row--;
		scroll(0, 0, ROWS - 1, cols - 1, 1,
		    vram[2 * (row * cols + col) + 1], 0);
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
	if ((mode & 0x7f) == 7)
		mode = (mode & 0x80) | 3;
	cols = (mode & 0x7e) == 0 ? 40 : 80;
	bda8w(0x49, mode & 0x7f);
	bda8w(0x4a, cols);
	bda8w(0x4b, 0);
	bda8w(0x4c, (cols * ROWS * 2) & 0xff);
	bda8w(0x4d, (cols * ROWS * 2) >> 8);
	bda8w(0x50, 0);
	bda8w(0x51, 0);
	bda8w(0x60, 7);
	bda8w(0x61, 6);
	bda8w(0x62, 0);
	bda8w(0x63, 0xd4);
	bda8w(0x64, 0x03);
	bda8w(0x84, ROWS - 1);
	if (!(mode & 0x80)) {
		clear_cells(0, cols * ROWS, 7);
		if (!cf.quiet) {
			memset(shadow, 0, sizeof(shadow));
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
		tty_out(al, 0, 0);
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
		return (0x0f);		/* the controller's spurious level */
	}
	while (!(req >> n & 1))
		n++;
	pic_irr &= (uint8_t)~(1u << n);
	pic_isr |= (uint8_t)(1u << n);
	pic_update(c);
	return (8 + n);
}

/* The firmware's key buffer in the data area: head at 40:1A, tail at
 * 40:1C, sixteen words from 40:1E. */
static int
bq_empty(void)
{
	return (mem_rw(BDA + 0x1a) == mem_rw(BDA + 0x1c));
}

static void
bq_put(unsigned code)
{
	unsigned tail = mem_rw(BDA + 0x1c), next = tail + 2;

	if (tail < 0x1e || tail > 0x3c)		/* the guest's own value */
		return;
	if (next == 0x3e)
		next = 0x1e;
	if (next == mem_rw(BDA + 0x1a))
		return;
	mem_ww(BDA + tail, code);
	mem_ww(BDA + 0x1c, next);
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
	else if (ch > 0 && (p = strchr(letter, ch)) != NULL)
		key_type((unsigned)lcode[p - letter] << 8);
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
				/* Run this trap again, with the keyboard
				 * interrupt let in between the runs. */
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
		bq_put(c->r[R_CX]);
		c->r[R_AX] &= 0xff00;
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
	char name[48], dir[80], full[96];
	const char *slash;
	struct disk old;
	struct direct *e;
	DIR *dp;
	unsigned i;
	size_t n;
	int ch;

	oputs("\033[0m\033[2J\033[H"
	    "i86: q quit, a or b change that floppy, any other key returns\r\n");
	oflush();
	ch = host_getc();
	if (ch == 'q' || ch < 0)
		host_exit(0);
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
			}
		}
		if ((dp = opendir(dir)) != NULL) {
			while ((e = readdir(dp)) != NULL)
				if (is_image(e->d_name)) {
					oputs(e->d_name);
					oputs("  ");
				}
			closedir(dp);
		}
		oputs("\r\nimage: ");
		if (menu_line(name, sizeof(name)) != 0) {
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
	/* Draw the guest's screen again from nothing. */
	memset(shadow, 0, sizeof(shadow));
	oputs("\033[2J");
	tx = ty = tattr = -1;
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
		 * would run the same instruction again without end. */
		if (cf.model != I86_8086) {
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

unsigned
pc_in(struct i86 *c, unsigned port, int w)
{
	unsigned v = 0xff;

	(void)c;
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
		/* A counter that moves between any two reads. */
		if (!pit_flip)
			pit_count -= 0x123;
		v = pit_flip ? pit_count >> 8 : pit_count & 0xff;
		pit_flip ^= 1;
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
		pit_flip ^= 2;
		v = (pit_flip & 2) ? 0x10 : 0;
		break;
	case 0x3d5:
		if (crtc_index == 0x0e || crtc_index == 0x0f) {
			v = bda8(0x51) * cols + bda8(0x50);
			v = crtc_index == 0x0e ? v >> 8 : v & 0xff;
		}
		break;
	case 0x3da:
		pit_flip ^= 4;
		v = (pit_flip & 4) ? 9 : 0;
		break;
	default:
		TRACE("in", 0, port);
		break;
	}
	return (w ? v | 0xff00 : v);
}

void
pc_out(struct i86 *c, unsigned port, unsigned v, int w)
{
	unsigned pos;

	(void)w;
	v &= 0xff;
	switch (port) {
	case 0x20:
		if (v & 0x10) {
			/* Initialization: the words that follow at port
			 * 21h are not masks. */
			pic_icw = (uint8_t)(1 + !(v & 2) + (v & 1));
			pic_irr = pic_isr = imask = 0;
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
		if (pic_icw)
			pic_icw--;
		else
			imask = (uint8_t)v;
		pic_update(c);
		break;
	case 0x70:
		cmos_index = (uint8_t)v;
		break;
	case 0x3d4:
		crtc_index = (uint8_t)v;
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

/* Time: the 18.2 Hz interrupt, the keyboard and the display. */
void
pc_poll(struct i86 *c)
{
	unsigned long now;
	int waited = 0;

	for (;;) {
		now = cf.fixed_clock ? (vtime += PC_POLL_MS) : host_ms();
		if ((long)(now - next_tick) >= 0) {
			next_tick += 55;
			nticks++;
			if ((long)(now - next_tick) > 500)
				next_tick = now + 55;
			if (ticks_due < 4)
				ticks_due++;
			if (cf.keys == NULL)
				key_pump();
			refresh();
		}
		if (ticks_due && !(pic_irr & 1)) {
			ticks_due--;
			pic_irr |= 1;
		}
		kb_next();
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
		if (!cf.fixed_clock)
			(void)host_ready((int)(next_tick - now) > 0 ?
			    (int)(next_tick - now) : 1);
		if (cf.keys == NULL)
			key_pump();
		waited = 1;
	}
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
	rom[ROM_RESET] = 0xea;
	rom[ROM_RESET + 1] = (uint8_t)(4 * 0x19);
	rom[ROM_RESET + 2] = ROM_OFF >> 8;
	rom[ROM_RESET + 3] = 0x00;
	rom[ROM_RESET + 4] = 0xf0;
	memcpy(rom + ROM_RESET + 5, "01/01/90", 8);
	rom[ROM_RESET + 14] = cf.model == I86_286 ? 0xfc : 0xfe;
	mem_rom(ROM_ADDR, rom, sizeof(rom));
	mem_fixed(VRAM_ADDR, vram, sizeof(vram));

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
	mem_ww(BDA + 0x10, (cf.cols == 40 ? 0x10 : 0x20) |
	    (nfloppy ? 1 | (nfloppy - 1) << 6 : 0));
	mem_ww(BDA + 0x13, cf.ram_kb);
	mem_ww(BDA + 0x1a, 0x1e);
	mem_ww(BDA + 0x1c, 0x1e);
	mem_ww(BDA + 0x80, 0x1e);
	mem_ww(BDA + 0x82, 0x3e);
	bda8w(0x75, (disks[2].fd >= 0) + (disks[3].fd >= 0));
	if (!cf.quiet) {
		oputs("\033[1;25r\033[?7l");
	}
	set_mode(cf.cols == 40 ? 1 : 3);
	next_tick = cf.fixed_clock ? 0 : host_ms() + 55;

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
	restored = 1;
	if (draw)
		refresh();
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
