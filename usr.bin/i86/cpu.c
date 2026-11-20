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
 * The instruction interpreter. One call of i86_step() takes a pending
 * interrupt or executes one instruction with its prefixes; a repeated
 * string instruction runs all its iterations inside that call.
 *
 * The three models differ where the processors do: the 8086 pushes the
 * decremented SP, takes shift counts unmasked, decodes 60-6F as 70-7F,
 * C0/C1/C8/C9 as the returns two above them and 0F as POP CS, and returns
 * from a divide error to the following instruction; the 80186 adds the
 * instructions at 60-62, 68-6F, C0, C1, C8 and C9, masks shift counts to
 * five bits and raises interrupt 6 for an undefined opcode; the 80286
 * pushes the original SP, holds flag bits 12-15 at zero and returns from a
 * divide error to the dividing instruction. Protected mode is absent:
 * LMSW stores nothing, SMSW reports the mode off, and the descriptor
 * table instructions that real mode permits load or store nothing.
 */
#include "i86.h"
#include "mem.h"

#define LIN(seg, off)	((((uint32_t)(seg) << 4) + (off)) & (MEM_SIZE - 1))
#define ARITH		(F_CF | F_PF | F_AF | F_ZF | F_SF | F_OF)

/* Eight and six consecutive case labels. */
#define CASE6(x)	case (x): case (x) + 1: case (x) + 2: case (x) + 3: \
			case (x) + 4: case (x) + 5
#define CASE8(x)	CASE6(x): case (x) + 6: case (x) + 7
/* The 8086 decodes this opcode as the one at the label. */
#define ALIAS_8086(label)	do { if (c->model == I86_8086) goto label; } \
				while (0)

/* An encoding the 80286 refuses with interrupt 6 and the earlier
 * processors execute; the list is the one its SingleStepTests vectors
 * exercise. Used inside the switch of i86_step(). */
#define UNDEFINED_286(cond)	if (c->model == I86_286 && (cond)) { \
					fault(c, 6, ip0, 1); \
					break; \
				} else (void)0

struct dec {
	uint8_t mod, reg, rm;
	uint8_t seg;		/* segment register of the operand */
	uint16_t off;
	int8_t ovr;		/* segment override, -1 when none */
	uint8_t rep;		/* 0, 0xf2 or 0xf3 */
};

void
i86_setfl(struct i86 *c, unsigned fl)
{
	fl = (fl & 0x0fd5) | 2;
	if (c->model != I86_286)
		fl |= 0xf000;
	c->fl = (uint16_t)fl;
}

void
i86_reset(struct i86 *c, int model)
{
	unsigned i;

	for (i = 0; i < 8; i++)
		c->r[i] = 0;
	c->s[S_ES] = c->s[S_SS] = c->s[S_DS] = 0;
	c->s[S_CS] = 0xffff;
	c->ip = 0;
	c->model = (uint8_t)model;
	c->events = 0;
	i86_setfl(c, 0);
}

static unsigned
rd8(struct i86 *c, unsigned seg, unsigned off)
{
	return (mem_rb(LIN(c->s[seg], off & 0xffff)));
}

static unsigned
rd16(struct i86 *c, unsigned seg, unsigned off)
{
	off &= 0xffff;
	if (off == 0xffff)
		return (mem_rb(LIN(c->s[seg], off)) |
		    mem_rb(LIN(c->s[seg], 0)) << 8);
	return (mem_rw(LIN(c->s[seg], off)));
}

static void
wr8(struct i86 *c, unsigned seg, unsigned off, unsigned v)
{
	mem_wb(LIN(c->s[seg], off & 0xffff), v);
}

static void
wr16(struct i86 *c, unsigned seg, unsigned off, unsigned v)
{
	off &= 0xffff;
	if (off == 0xffff) {
		mem_wb(LIN(c->s[seg], off), v);
		mem_wb(LIN(c->s[seg], 0), v >> 8);
	} else
		mem_ww(LIN(c->s[seg], off), v);
}

/* The fetch is inlined at every site: as a call it was a fifth of the
 * instructions the interpreter executed. */
#ifdef __GNUC__
__attribute__((always_inline))
#endif
static inline unsigned
fetch8(struct i86 *c)
{
	uint32_t a = LIN(c->s[S_CS], c->ip++);
	uintptr_t b = mem_rmap[a >> MEM_PGSHIFT];

	return (b ? *(const uint8_t *)(b + a) : mem_rslow(a));
}

static unsigned
fetch16(struct i86 *c)
{
	unsigned lo = fetch8(c);

	return (lo | fetch8(c) << 8);
}

static void
push(struct i86 *c, unsigned v)
{
	c->r[R_SP] -= 2;
	wr16(c, S_SS, c->r[R_SP], v);
}

static unsigned
pop(struct i86 *c)
{
	unsigned v = rd16(c, S_SS, c->r[R_SP]);

	c->r[R_SP] += 2;
	return (v);
}

void
i86_int(struct i86 *c, unsigned n)
{
	push(c, c->fl);
	push(c, c->s[S_CS]);
	push(c, c->ip);
	c->fl &= (uint16_t)~(F_IF | F_TF);
	c->ip = (uint16_t)mem_rw(n * 4);
	c->s[S_CS] = (uint16_t)mem_rw(n * 4 + 2);
}

static unsigned
getr8(struct i86 *c, unsigned i)
{
	return (i < 4 ? c->r[i] & 0xff : c->r[i - 4] >> 8);
}

static void
setr8(struct i86 *c, unsigned i, unsigned v)
{
	if (i < 4)
		c->r[i] = (uint16_t)((c->r[i] & 0xff00) | (v & 0xff));
	else
		c->r[i - 4] = (uint16_t)((c->r[i - 4] & 0xff) | (v & 0xff) << 8);
}

static unsigned
getreg(struct i86 *c, unsigned i, int w)
{
	return (w ? c->r[i] : getr8(c, i));
}

static void
setreg(struct i86 *c, unsigned i, int w, unsigned v)
{
	if (w)
		c->r[i] = (uint16_t)v;
	else
		setr8(c, i, v);
}

static void
modrm(struct i86 *c, struct dec *d)
{
	unsigned b = fetch8(c), off, seg = S_DS;

	d->mod = (uint8_t)(b >> 6);
	d->reg = (b >> 3) & 7;
	d->rm = b & 7;
	if (d->mod == 3)
		return;
	switch (d->rm) {
	case 0:	off = c->r[R_BX] + c->r[R_SI]; break;
	case 1:	off = c->r[R_BX] + c->r[R_DI]; break;
	case 2:	off = c->r[R_BP] + c->r[R_SI]; seg = S_SS; break;
	case 3:	off = c->r[R_BP] + c->r[R_DI]; seg = S_SS; break;
	case 4:	off = c->r[R_SI]; break;
	case 5:	off = c->r[R_DI]; break;
	case 6:
		if (d->mod == 0)
			off = fetch16(c);
		else {
			off = c->r[R_BP];
			seg = S_SS;
		}
		break;
	default: off = c->r[R_BX]; break;
	}
	if (d->mod == 1)
		off += (unsigned)(int8_t)fetch8(c);
	else if (d->mod == 2)
		off += fetch16(c);
	d->off = (uint16_t)off;
	d->seg = (uint8_t)(d->ovr >= 0 ? (unsigned)d->ovr : seg);
}

static unsigned
getrm(struct i86 *c, const struct dec *d, int w)
{
	if (d->mod == 3)
		return (getreg(c, d->rm, w));
	return (w ? rd16(c, d->seg, d->off) : rd8(c, d->seg, d->off));
}

static void
setrm(struct i86 *c, const struct dec *d, int w, unsigned v)
{
	if (d->mod == 3)
		setreg(c, d->rm, w, v);
	else if (w)
		wr16(c, d->seg, d->off, v);
	else
		wr8(c, d->seg, d->off, v);
}

static unsigned
szp(unsigned fl, unsigned res, unsigned sign)
{
	unsigned p = res & 0xff;

	if (res == 0)
		fl |= F_ZF;
	if (res & sign)
		fl |= F_SF;
	p ^= p >> 4;
	if (!((0x6996 >> (p & 15)) & 1))
		fl |= F_PF;
	return (fl);
}

/* The eight operations of opcodes 00-3F and group 80-83, in their order:
 * ADD OR ADC SBB AND SUB XOR CMP. */
static unsigned
alu(struct i86 *c, unsigned op, unsigned a, unsigned b, int w)
{
	unsigned mask = w ? 0xffff : 0xff, sign = w ? 0x8000 : 0x80;
	unsigned fl = c->fl & ~(unsigned)ARITH, cf = c->fl & F_CF, res;

	switch (op) {
	case 0:
		cf = 0;
		/* FALLTHROUGH */
	case 2:
		res = a + b + cf;
		if ((a ^ res) & (b ^ res) & sign)
			fl |= F_OF;
		break;
	case 5:
	case 7:
		cf = 0;
		/* FALLTHROUGH */
	case 3:
		res = a - b - cf;
		if ((a ^ b) & (a ^ res) & sign)
			fl |= F_OF;
		break;
	case 1:
		res = a | b;
		goto logic;
	case 4:
		res = a & b;
		goto logic;
	default:
		res = a ^ b;
	logic:
		c->fl = (uint16_t)szp(fl, res, sign);
		return (res);
	}
	if (res & (mask + 1))
		fl |= F_CF;
	if ((a ^ b ^ res) & 0x10)
		fl |= F_AF;
	res &= mask;
	c->fl = (uint16_t)szp(fl, res, sign);
	return (res);
}

static unsigned
incdec(struct i86 *c, unsigned v, int dec, int w)
{
	unsigned cf = c->fl & F_CF;

	v = alu(c, dec ? 5 : 0, v, 1, w);
	c->fl = (uint16_t)((c->fl & ~(unsigned)F_CF) | cf);
	return (v);
}

/* ROL ROR RCL RCR SHL SHR SHL SAR, by reg field. The overflow flag follows
 * the last single-bit step, which is the documented result for a count of
 * one. */
static unsigned
shift(struct i86 *c, unsigned op, unsigned v, unsigned cnt, int w)
{
	unsigned mask = w ? 0xffff : 0xff, sign = w ? 0x8000 : 0x80;
	unsigned cf = c->fl & F_CF, fl, t;

	if (c->model != I86_8086)
		cnt &= 31;
	if (cnt == 0)
		return (v);
	do {
		switch (op) {
		case 0:
			cf = (v & sign) != 0;
			v = ((v << 1) | cf) & mask;
			break;
		case 1:
			cf = v & 1;
			v = (v >> 1) | (cf ? sign : 0);
			break;
		case 2:
			t = (v & sign) != 0;
			v = ((v << 1) | cf) & mask;
			cf = t;
			break;
		case 3:
			t = v & 1;
			v = (v >> 1) | (cf ? sign : 0);
			cf = t;
			break;
		case 5:
			cf = v & 1;
			v >>= 1;
			break;
		case 7:
			cf = v & 1;
			v = (v >> 1) | (v & sign);
			break;
		default:
			cf = (v & sign) != 0;
			v = (v << 1) & mask;
			break;
		}
	} while (--cnt);
	fl = c->fl & ~(unsigned)(F_CF | F_OF);
	if (cf)
		fl |= F_CF;
	if (op == 0 || op == 2 || op == 4 || op == 6) {
		if (((v & sign) != 0) != (cf != 0))
			fl |= F_OF;
	} else if (op != 7) {
		if ((v ^ (v << 1)) & sign)
			fl |= F_OF;
	}
	if (op >= 4)
		fl = szp(fl & ~(unsigned)(F_ZF | F_SF | F_PF), v, sign);
	c->fl = (uint16_t)fl;
	return (v);
}

static int
cond(const struct i86 *c, unsigned cc)
{
	unsigned f = c->fl;
	int r;

	switch (cc >> 1) {
	case 0:	r = (f & F_OF) != 0; break;
	case 1:	r = (f & F_CF) != 0; break;
	case 2:	r = (f & F_ZF) != 0; break;
	case 3:	r = (f & (F_CF | F_ZF)) != 0; break;
	case 4:	r = (f & F_SF) != 0; break;
	case 5:	r = (f & F_PF) != 0; break;
	case 6:	r = ((f & F_SF) != 0) != ((f & F_OF) != 0); break;
	default:
		r = (f & F_ZF) || ((f & F_SF) != 0) != ((f & F_OF) != 0);
		break;
	}
	return (r ^ (int)(cc & 1));
}

/* Interrupt n raised by the instruction that began at ip0. */
static void
fault(struct i86 *c, unsigned n, unsigned ip0, int restart)
{
	if (restart)
		c->ip = (uint16_t)ip0;
	i86_int(c, n);
}

static void
divide(struct i86 *c, unsigned op, unsigned v, int w, unsigned ip0, int rep)
{
	uint32_t n, q, r;
	int neg_n, neg_q;

	if (w)
		n = (uint32_t)c->r[R_DX] << 16 | c->r[R_AX];
	else
		n = c->r[R_AX];
	if (v == 0)
		goto error;
	if (op == 6) {
		q = n / v;
		r = n % v;
		if (q > (w ? 0xffffu : 0xffu))
			goto error;
	} else {
		neg_n = w ? (n >> 31) != 0 : (n & 0x8000) != 0;
		if (neg_n)
			n = w ? 0u - n : (0u - n) & 0xffff;
		/* A repeat prefix on the 8086 inverts the quotient's sign. */
		neg_q = neg_n ^ (rep && c->model == I86_8086);
		if (v & (w ? 0x8000u : 0x80u)) {
			v = (0u - v) & (w ? 0xffffu : 0xffu);
			neg_q = !neg_q;
		}
		q = n / v;
		r = n % v;
		/* The 8086 refuses the most negative quotient. */
		if (q > (w ? 0x7fffu : 0x7fu) + (neg_q && c->model != I86_8086))
			goto error;
		if (neg_q)
			q = 0u - q;
		if (neg_n)
			r = 0u - r;
	}
	if (w) {
		c->r[R_AX] = (uint16_t)q;
		c->r[R_DX] = (uint16_t)r;
	} else
		c->r[R_AX] = (uint16_t)((q & 0xff) | (r & 0xff) << 8);
	return;
error:
	fault(c, 0, ip0, c->model == I86_286);
}

static void
group3(struct i86 *c, struct dec *d, int w, unsigned ip0)
{
	unsigned mask = w ? 0xffff : 0xff;
	unsigned v, fl;
	uint32_t p;

	modrm(c, d);
	v = getrm(c, d, w);
	switch (d->reg) {
	case 0:
	case 1:
		(void)alu(c, 4, v, w ? fetch16(c) : fetch8(c), w);
		break;
	case 2:
		setrm(c, d, w, ~v & mask);
		break;
	case 3:
		setrm(c, d, w, alu(c, 5, 0, v, w));
		break;
	case 4:
	case 5:
		fl = c->fl & ~(unsigned)(F_CF | F_OF);
		if (d->reg == 4) {
			p = (uint32_t)getreg(c, R_AX, w) * v;
			if (p >> (w ? 16 : 8))
				fl |= F_CF | F_OF;
		} else {
			int32_t a = w ? (int16_t)c->r[R_AX] : (int8_t)c->r[R_AX];
			int32_t b = w ? (int16_t)v : (int8_t)v;
			int32_t s = a * b;

			p = (uint32_t)s;
			if (s != (w ? (int16_t)s : (int8_t)s))
				fl |= F_CF | F_OF;
		}
		if (w) {
			c->r[R_AX] = (uint16_t)p;
			c->r[R_DX] = (uint16_t)(p >> 16);
		} else
			c->r[R_AX] = (uint16_t)p;
		c->fl = (uint16_t)fl;
		break;
	default:
		divide(c, d->reg, v, w, ip0, d->rep != 0);
		break;
	}
}

static void
string(struct i86 *c, const struct dec *d, unsigned op)
{
	int w = op & 1;
	unsigned step = (c->fl & F_DF) ? (w ? 0xfffe : 0xffff) : (w ? 2 : 1);
	unsigned sseg = d->ovr >= 0 ? (unsigned)d->ovr : S_DS;
	unsigned a, b;
	int compare = 0;

	if (d->rep && c->r[R_CX] == 0)
		return;
	for (;;) {
		switch (op & 0xfe) {
		case 0x6c:	/* INS */
			a = pc_in(c, c->r[R_DX], w);
			if (w)
				wr16(c, S_ES, c->r[R_DI], a);
			else
				wr8(c, S_ES, c->r[R_DI], a);
			c->r[R_DI] += step;
			break;
		case 0x6e:	/* OUTS */
			a = w ? rd16(c, sseg, c->r[R_SI]) : rd8(c, sseg, c->r[R_SI]);
			pc_out(c, c->r[R_DX], a, w);
			c->r[R_SI] += step;
			break;
		case 0xa4:	/* MOVS */
			if (w)
				wr16(c, S_ES, c->r[R_DI], rd16(c, sseg, c->r[R_SI]));
			else
				wr8(c, S_ES, c->r[R_DI], rd8(c, sseg, c->r[R_SI]));
			c->r[R_SI] += step;
			c->r[R_DI] += step;
			break;
		case 0xa6:	/* CMPS */
			a = w ? rd16(c, sseg, c->r[R_SI]) : rd8(c, sseg, c->r[R_SI]);
			b = w ? rd16(c, S_ES, c->r[R_DI]) : rd8(c, S_ES, c->r[R_DI]);
			(void)alu(c, 7, a, b, w);
			c->r[R_SI] += step;
			c->r[R_DI] += step;
			compare = 1;
			break;
		case 0xaa:	/* STOS */
			if (w)
				wr16(c, S_ES, c->r[R_DI], c->r[R_AX]);
			else
				wr8(c, S_ES, c->r[R_DI], c->r[R_AX]);
			c->r[R_DI] += step;
			break;
		case 0xac:	/* LODS */
			if (w)
				c->r[R_AX] = (uint16_t)rd16(c, sseg, c->r[R_SI]);
			else
				setr8(c, 0, rd8(c, sseg, c->r[R_SI]));
			c->r[R_SI] += step;
			break;
		default:	/* SCAS */
			a = getreg(c, R_AX, w);
			b = w ? rd16(c, S_ES, c->r[R_DI]) : rd8(c, S_ES, c->r[R_DI]);
			(void)alu(c, 7, a, b, w);
			c->r[R_DI] += step;
			compare = 1;
			break;
		}
		if (!d->rep || --c->r[R_CX] == 0)
			break;
		if (compare && ((c->fl & F_ZF) != 0) != (d->rep == 0xf3))
			break;
	}
}

static void
bcd(struct i86 *c, unsigned op)
{
	unsigned al = c->r[R_AX] & 0xff, ah = c->r[R_AX] >> 8;
	unsigned fl = c->fl, old = al, cf = fl & F_CF;
	/* On the 8086 the high-digit test moves with the auxiliary carry;
	 * the 80286 tests against 99h alone and lets DAS borrow into the
	 * carry. Both are measured: the SingleStepTests vectors of 27 and
	 * 2F for the 8088 and for the 80286. */
	unsigned top = (fl & F_AF) && c->model != I86_286 ? 0x9f : 0x99;

	fl &= ~(unsigned)(F_CF | F_ZF | F_SF | F_PF);
	switch (op) {
	case 0x27:	/* DAA */
		if ((al & 15) > 9 || (fl & F_AF)) {
			al += 6;
			fl |= F_AF;
		} else
			fl &= ~(unsigned)F_AF;
		if (old > top || cf) {
			al += 0x60;
			fl |= F_CF;
		}
		break;
	case 0x2f:	/* DAS */
		if ((al & 15) > 9 || (fl & F_AF)) {
			if (c->model == I86_286 && al < 6)
				fl |= F_CF;
			al -= 6;
			fl |= F_AF;
		} else
			fl &= ~(unsigned)F_AF;
		if (old > top || cf) {
			al -= 0x60;
			fl |= F_CF;
		}
		break;
	default:	/* 37 AAA, 3F AAS */
		if ((al & 15) > 9 || (fl & F_AF)) {
			if (c->model == I86_286) {
				unsigned ax = (ah << 8 | al) +
				    (op == 0x37 ? 0x106u : 0u - 0x106u);

				al = ax & 0xff;
				ah = (ax >> 8) & 0xff;
			} else if (op == 0x37) {
				al += 6;
				ah += 1;
			} else {
				al -= 6;
				ah -= 1;
			}
			fl |= F_AF | F_CF;
		} else
			fl &= ~(unsigned)F_AF;
		c->r[R_AX] = (uint16_t)((ah & 0xff) << 8 | (al & 15));
		c->fl = (uint16_t)szp(fl, al & 15, 0x80);
		return;
	}
	al &= 0xff;
	c->r[R_AX] = (uint16_t)(ah << 8 | al);
	c->fl = (uint16_t)szp(fl, al, 0x80);
}

static void
setseg(struct i86 *c, unsigned i, unsigned v)
{
	c->s[i] = (uint16_t)v;
	if (i == S_SS)
		c->inhibit = 1;
}

/* Opcode 0F on the 80286 in real mode. */
static void
op0f(struct i86 *c, struct dec *d, unsigned ip0)
{
	unsigned b = fetch8(c);

	if (b == 1) {
		modrm(c, d);
		switch (d->reg) {
		case 0:		/* SGDT */
		case 1:		/* SIDT */
			if (d->mod == 3)
				break;
			wr16(c, d->seg, d->off, d->reg ? 0x3ff : 0);
			wr16(c, d->seg, d->off + 2, 0);
			wr16(c, d->seg, d->off + 4, 0xff00);
			return;
		case 2:		/* LGDT */
		case 3:		/* LIDT */
			if (d->mod == 3)
				break;
			return;
		case 4:		/* SMSW */
			setrm(c, d, 1, 0xfff0);
			return;
		case 6:		/* LMSW */
			return;
		default:
			break;
		}
	} else if (b == 6)	/* CLTS */
		return;
	fault(c, 6, ip0, 1);
}

void
i86_step(struct i86 *c)
{
	struct dec d;
	unsigned op, ip0, a, b, tf;
	int w;

	if (c->events) {
		if (c->inhibit)
			c->inhibit = 0;
		else if (c->irq && (c->fl & F_IF)) {
			unsigned n = pc_inta(c);

			c->halted = 0;
			i86_int(c, n);
		}
		if (c->halted)
			return;
	}
	tf = c->fl & F_TF;
	ip0 = c->ip;
	d.ovr = -1;
	d.rep = 0;
	d.mod = 3;
prefixed:
	op = fetch8(c);
	w = op & 1;

	switch (op) {
	case 0x26: case 0x2e: case 0x36: case 0x3e:
		d.ovr = (int8_t)((op >> 3) & 3);
		goto prefixed;
	case 0xf2: case 0xf3:
		d.rep = (uint8_t)op;
		goto prefixed;
	case 0xf0:
		goto prefixed;
	CASE6(0x00): CASE6(0x08): CASE6(0x10): CASE6(0x18):
	CASE6(0x20): CASE6(0x28): CASE6(0x30): CASE6(0x38): {
		unsigned aop = op >> 3;

		if (op & 4) {
			b = w ? fetch16(c) : fetch8(c);
			a = alu(c, aop, getreg(c, R_AX, w), b, w);
			if (aop != 7)
				setreg(c, R_AX, w, a);
		} else {
			modrm(c, &d);
			if (op & 2) {
				a = alu(c, aop, getreg(c, d.reg, w),
				    getrm(c, &d, w), w);
				if (aop != 7)
					setreg(c, d.reg, w, a);
			} else {
				a = alu(c, aop, getrm(c, &d, w),
				    getreg(c, d.reg, w), w);
				if (aop != 7)
					setrm(c, &d, w, a);
			}
		}
		break;
	}
	CASE8(0x40):
		c->r[op & 7] = (uint16_t)incdec(c, c->r[op & 7], 0, 1);
		break;
	CASE8(0x48):
		c->r[op & 7] = (uint16_t)incdec(c, c->r[op & 7], 1, 1);
		break;
	CASE8(0x50):
		if (op == 0x50 + R_SP && c->model != I86_286)
			push(c, c->r[R_SP] - 2);
		else
			push(c, c->r[op & 7]);
		break;
	CASE8(0x58):
		c->r[op & 7] = (uint16_t)pop(c);
		break;
	CASE8(0x70): CASE8(0x78):
	jcc:
		a = (unsigned)(int8_t)fetch8(c);
		if (cond(c, op & 15))
			c->ip += a;
		break;
	CASE8(0x90):
		a = c->r[op & 7];
		c->r[op & 7] = c->r[R_AX];
		c->r[R_AX] = (uint16_t)a;
		break;
	CASE8(0xb0):
		setr8(c, op & 7, fetch8(c));
		break;
	CASE8(0xb8):
		c->r[op & 7] = (uint16_t)fetch16(c);
		break;
	CASE8(0xd8):	/* ESC: no coprocessor answers */
		modrm(c, &d);
		break;
	case 0x06: case 0x0e: case 0x16: case 0x1e:
		push(c, c->s[op >> 3]);
		break;
	case 0x07: case 0x17: case 0x1f:
		setseg(c, op >> 3, pop(c));
		break;
	case 0x0f:
		if (c->s[S_CS] == 0xf000 &&
		    mem_rb(LIN(0xf000, c->ip)) == 0xff) {
			c->ip++;
			a = fetch8(c);
			b = (unsigned)pc_trap(c, a);
			if (b != 1) {
				c->ip = (uint16_t)pop(c);
				c->s[S_CS] = (uint16_t)pop(c);
				a = pop(c) & (F_IF | F_TF | F_DF);
				if (b == 2)
					a |= F_IF;
				i86_setfl(c, (c->fl &
				    ~(unsigned)(F_IF | F_TF | F_DF)) | a);
			}
			return;
		}
		if (c->model == I86_8086)
			c->s[S_CS] = (uint16_t)pop(c);
		else if (c->model == I86_286)
			op0f(c, &d, ip0);
		else
			fault(c, 6, ip0, 1);
		break;
	case 0x27: case 0x2f: case 0x37: case 0x3f:
		bcd(c, op);
		break;
	case 0x60:	/* PUSHA */
		ALIAS_8086(jcc);
		a = c->r[R_SP];
		for (b = 0; b < 8; b++)
			push(c, b == R_SP ? a : c->r[b]);
		break;
	case 0x61:	/* POPA */
		ALIAS_8086(jcc);
		for (b = 8; b-- > 0;) {
			a = pop(c);
			if (b != R_SP)
				c->r[b] = (uint16_t)a;
		}
		break;
	case 0x62:	/* BOUND */
		ALIAS_8086(jcc);
		modrm(c, &d);
		if (d.mod == 3) {
			fault(c, 6, ip0, 1);
			break;
		}
		a = c->r[d.reg];
		if ((int16_t)a < (int16_t)rd16(c, d.seg, d.off) ||
		    (int16_t)a > (int16_t)rd16(c, d.seg, d.off + 2))
			fault(c, 5, ip0, 1);
		break;
	case 0x68:
		ALIAS_8086(jcc);
		push(c, fetch16(c));
		break;
	case 0x6a:
		ALIAS_8086(jcc);
		push(c, (unsigned)(int8_t)fetch8(c));
		break;
	case 0x69:
	case 0x6b: {	/* IMUL r16, r/m16, imm */
		int32_t p;

		ALIAS_8086(jcc);
		modrm(c, &d);
		a = getrm(c, &d, 1);
		b = op == 0x69 ? fetch16(c) : (unsigned)(int8_t)fetch8(c);
		p = (int32_t)(int16_t)a * (int16_t)b;
		c->r[d.reg] = (uint16_t)p;
		c->fl &= (uint16_t)~(F_CF | F_OF);
		if (p != (int16_t)p)
			c->fl |= F_CF | F_OF;
		break;
	}
	case 0x6c: case 0x6d: case 0x6e: case 0x6f:
		ALIAS_8086(jcc);
		/* FALLTHROUGH */
	case 0xa4: case 0xa5: case 0xa6: case 0xa7:
	case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
		string(c, &d, op);
		break;
	case 0x80: case 0x81: case 0x82: case 0x83:
		modrm(c, &d);
		a = getrm(c, &d, w);
		if (op == 0x81)
			b = fetch16(c);
		else if (op == 0x83)
			b = (unsigned)(int8_t)fetch8(c) & 0xffff;
		else
			b = fetch8(c);
		a = alu(c, d.reg, a, b, w);
		if (d.reg != 7)
			setrm(c, &d, w, a);
		break;
	case 0x84: case 0x85:
		modrm(c, &d);
		(void)alu(c, 4, getrm(c, &d, w), getreg(c, d.reg, w), w);
		break;
	case 0x86: case 0x87:
		modrm(c, &d);
		a = getrm(c, &d, w);
		setrm(c, &d, w, getreg(c, d.reg, w));
		setreg(c, d.reg, w, a);
		break;
	case 0x88: case 0x89:
		modrm(c, &d);
		setrm(c, &d, w, getreg(c, d.reg, w));
		break;
	case 0x8a: case 0x8b:
		modrm(c, &d);
		setreg(c, d.reg, w, getrm(c, &d, w));
		break;
	case 0x8c:
		modrm(c, &d);
		UNDEFINED_286(d.reg > 3);
		setrm(c, &d, 1, c->s[d.reg & 3]);
		break;
	case 0x8d:
		modrm(c, &d);
		UNDEFINED_286(d.mod == 3);
		c->r[d.reg] = d.off;
		break;
	case 0x8e:
		modrm(c, &d);
		UNDEFINED_286(d.reg > 3 || d.reg == S_CS);
		setseg(c, d.reg & 3, getrm(c, &d, 1));
		break;
	case 0x8f:
		modrm(c, &d);
		UNDEFINED_286(d.reg != 0);
		setrm(c, &d, 1, pop(c));
		break;
	case 0x98:
		c->r[R_AX] = (uint16_t)(int8_t)c->r[R_AX];
		break;
	case 0x99:
		c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xffff : 0;
		break;
	case 0x9a:
		a = fetch16(c);
		b = fetch16(c);
		push(c, c->s[S_CS]);
		push(c, c->ip);
		c->ip = (uint16_t)a;
		c->s[S_CS] = (uint16_t)b;
		break;
	case 0x9b:
		break;
	case 0x9c:
		push(c, c->fl);
		break;
	case 0x9d:
		i86_setfl(c, pop(c));
		break;
	case 0x9e:
		i86_setfl(c, (c->fl & 0xff00u) | (c->r[R_AX] >> 8));
		break;
	case 0x9f:
		setr8(c, 4, c->fl);
		break;
	case 0xa0: case 0xa1:
		a = fetch16(c);
		b = d.ovr >= 0 ? (unsigned)d.ovr : S_DS;
		setreg(c, R_AX, w, w ? rd16(c, b, a) : rd8(c, b, a));
		break;
	case 0xa2: case 0xa3:
		a = fetch16(c);
		b = d.ovr >= 0 ? (unsigned)d.ovr : S_DS;
		if (w)
			wr16(c, b, a, c->r[R_AX]);
		else
			wr8(c, b, a, c->r[R_AX]);
		break;
	case 0xa8: case 0xa9:
		(void)alu(c, 4, getreg(c, R_AX, w),
		    w ? fetch16(c) : fetch8(c), w);
		break;
	case 0xc0: case 0xc1:
		ALIAS_8086(ret_near);
		modrm(c, &d);
		a = getrm(c, &d, w);
		b = fetch8(c);
		setrm(c, &d, w, shift(c, d.reg, a, b, w));
		break;
	case 0xd0: case 0xd1: case 0xd2: case 0xd3:
		modrm(c, &d);
		a = getrm(c, &d, w);
		b = (op & 2) ? (c->r[R_CX] & 0xffu) : 1;
		setrm(c, &d, w, shift(c, d.reg, a, b, w));
		break;
	case 0xc2:
	case 0xc3:
	ret_near:
		a = w ? 0 : fetch16(c);
		c->ip = (uint16_t)pop(c);
		c->r[R_SP] += a;
		break;
	case 0xc4: case 0xc5:
		modrm(c, &d);
		UNDEFINED_286(d.mod == 3);
		a = rd16(c, d.seg, d.off);
		b = rd16(c, d.seg, d.off + 2);
		c->r[d.reg] = (uint16_t)a;
		c->s[op == 0xc4 ? S_ES : S_DS] = (uint16_t)b;
		break;
	case 0xc6: case 0xc7:
		modrm(c, &d);
		UNDEFINED_286(d.reg != 0);
		setrm(c, &d, w, w ? fetch16(c) : fetch8(c));
		break;
	case 0xc8: {	/* ENTER */
		unsigned size, level, frame;

		ALIAS_8086(ret_far);
		size = fetch16(c);
		level = fetch8(c) & 31;
		push(c, c->r[R_BP]);
		frame = c->r[R_SP];
		if (level) {
			while (--level) {
				c->r[R_BP] -= 2;
				push(c, rd16(c, S_SS, c->r[R_BP]));
			}
			push(c, frame);
		}
		c->r[R_BP] = (uint16_t)frame;
		c->r[R_SP] -= size;
		break;
	}
	case 0xc9:
		ALIAS_8086(ret_far);
		c->r[R_SP] = c->r[R_BP];
		c->r[R_BP] = (uint16_t)pop(c);
		break;
	case 0xca:
	case 0xcb:
	ret_far:
		a = w ? 0 : fetch16(c);
		c->ip = (uint16_t)pop(c);
		c->s[S_CS] = (uint16_t)pop(c);
		c->r[R_SP] += a;
		break;
	case 0xcc:
		i86_int(c, 3);
		break;
	case 0xcd:
		a = fetch8(c);
		i86_int(c, a);
		break;
	case 0xce:
		if (c->fl & F_OF)
			i86_int(c, 4);
		break;
	case 0xcf:
		c->ip = (uint16_t)pop(c);
		c->s[S_CS] = (uint16_t)pop(c);
		i86_setfl(c, pop(c));
		break;
	case 0xd4:	/* AAM */
		b = fetch8(c);
		if (b == 0) {
			fault(c, 0, ip0, c->model == I86_286);
			break;
		}
		a = c->r[R_AX] & 0xff;
		c->r[R_AX] = (uint16_t)((a / b) << 8 | (a % b));
		c->fl = (uint16_t)szp(c->fl & ~(unsigned)(F_ZF | F_SF | F_PF),
		    a % b, 0x80);
		break;
	case 0xd5:	/* AAD */
		b = fetch8(c);
		a = alu(c, 0, c->r[R_AX] & 0xff, ((c->r[R_AX] >> 8) * b) & 0xff, 0);
		c->r[R_AX] = (uint16_t)a;
		break;
	case 0xd6:	/* SALC */
		setr8(c, 0, (c->fl & F_CF) ? 0xff : 0);
		break;
	case 0xd7:
		setr8(c, 0, rd8(c, d.ovr >= 0 ? (unsigned)d.ovr : S_DS,
		    c->r[R_BX] + (c->r[R_AX] & 0xff)));
		break;
	case 0xe0: case 0xe1: case 0xe2:
		a = (unsigned)(int8_t)fetch8(c);
		if (--c->r[R_CX] != 0 && (op == 0xe2 ||
		    ((c->fl & F_ZF) != 0) == (op == 0xe1)))
			c->ip += a;
		break;
	case 0xe3:
		a = (unsigned)(int8_t)fetch8(c);
		if (c->r[R_CX] == 0)
			c->ip += a;
		break;
	case 0xe4: case 0xe5:
		a = fetch8(c);
		setreg(c, R_AX, w, pc_in(c, a, w));
		break;
	case 0xe6: case 0xe7:
		a = fetch8(c);
		pc_out(c, a, getreg(c, R_AX, w), w);
		break;
	case 0xec: case 0xed:
		setreg(c, R_AX, w, pc_in(c, c->r[R_DX], w));
		break;
	case 0xee: case 0xef:
		pc_out(c, c->r[R_DX], getreg(c, R_AX, w), w);
		break;
	case 0xe8:
		a = fetch16(c);
		push(c, c->ip);
		c->ip += a;
		break;
	case 0xe9:
		a = fetch16(c);
		c->ip += a;
		break;
	case 0xea:
		a = fetch16(c);
		b = fetch16(c);
		c->ip = (uint16_t)a;
		c->s[S_CS] = (uint16_t)b;
		break;
	case 0xeb:
		a = (unsigned)(int8_t)fetch8(c);
		c->ip += a;
		break;
	case 0xf1:
		break;
	case 0xf4:
		c->halted = 1;
		break;
	case 0xf5:
		c->fl ^= F_CF;
		break;
	case 0xf6: case 0xf7:
		group3(c, &d, w, ip0);
		break;
	case 0xf8:
		c->fl &= (uint16_t)~F_CF;
		break;
	case 0xf9:
		c->fl |= F_CF;
		break;
	case 0xfa:
		c->fl &= (uint16_t)~F_IF;
		break;
	case 0xfb:
		if (!(c->fl & F_IF))
			c->inhibit = 1;
		c->fl |= F_IF;
		break;
	case 0xfc:
		c->fl &= (uint16_t)~F_DF;
		break;
	case 0xfd:
		c->fl |= F_DF;
		break;
	case 0xfe: case 0xff:
		modrm(c, &d);
		/* The byte group has two members and the word group seven;
		 * past the 8086 the rest raise interrupt 6. */
		if (c->model != I86_8086 && (d.reg == 7 || (!w && d.reg > 1))) {
			fault(c, 6, ip0, 1);
			break;
		}
		switch (d.reg) {
		case 0:
		case 1:
			setrm(c, &d, w, incdec(c, getrm(c, &d, w), d.reg, w));
			break;
		case 2:
			a = getrm(c, &d, 1);
			push(c, c->ip);
			c->ip = (uint16_t)a;
			break;
		case 3:
		case 5:
			if (d.mod == 3) {
				fault(c, 6, ip0, 1);
				break;
			}
			a = rd16(c, d.seg, d.off);
			b = rd16(c, d.seg, d.off + 2);
			if (d.reg == 3) {
				push(c, c->s[S_CS]);
				push(c, c->ip);
			}
			c->ip = (uint16_t)a;
			c->s[S_CS] = (uint16_t)b;
			break;
		case 4:
			c->ip = (uint16_t)getrm(c, &d, 1);
			break;
		default:
			a = getrm(c, &d, 1);
			if (d.mod == 3 && d.rm == R_SP && c->model != I86_286)
				a -= 2;
			push(c, a);
			break;
		}
		break;
	default:	/* 63-67 */
		ALIAS_8086(jcc);
		fault(c, 6, ip0, 1);
		break;
	}
	if (tf && !c->inhibit)
		i86_int(c, 1);
}
