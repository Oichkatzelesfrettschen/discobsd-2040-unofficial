/*
 * The 11/40 instruction set. Registers are 16-bit; every arithmetic
 * result is masked to the operand length before it lands in a register
 * or memory, so a wider host int never leaks a carry into the guest.
 */
#include "pdp11.h"

uint16_t R[8];
uint16_t PS, PC, KSP, USP, LKS;
int curuser, prevuser, waiting;

/* RK05 bootstrap, deposited at 02000 by reset: reads block 0 to 0 and
 * jumps to it. */
static const uint16_t bootrom[] = {
	0042113,		/* "KD" */
	0012706, 02000,		/* MOV #boot_start, SP */
	0012700, 0000000,	/* MOV #unit, R0 */
	0010003,		/* MOV R0, R3 */
	0000303,		/* SWAB R3 */
	0006303,		/* ASL R3 */
	0006303,		/* ASL R3 */
	0006303,		/* ASL R3 */
	0006303,		/* ASL R3 */
	0006303,		/* ASL R3 */
	0012701, 0177412,	/* MOV #RKDA, R1 */
	0010311,		/* MOV R3, (R1) */
	0005041,		/* CLR -(R1) */
	0012741, 0177000,	/* MOV #-256.*2, -(R1) */
	0012741, 0000005,	/* MOV #READ+GO, -(R1) */
	0005002,		/* CLR R2 */
	0005003,		/* CLR R3 */
	0012704, 02020,		/* MOV #START+20, R4 */
	0005005,		/* CLR R5 */
	0105711,		/* TSTB (R1) */
	0100376,		/* BPL .-2 */
	0105011,		/* CLRB (R1) */
	0005007,		/* CLR PC */
};

struct intr {
	uint8_t vec;
	uint8_t pri;
};
#define ITABN 8
static struct intr itab[ITABN];

void
cpu_reset(void)
{
	unsigned i;

	for (i = 0; i < sizeof(bootrom) / sizeof(bootrom[0]); i++)
		mem[(02000 >> 1) + i] = bootrom[i];
	for (i = 0; i < 8; i++)
		R[i] = 0;
	R[7] = 02002;
	PS = 0;
	KSP = USP = 0;
	LKS = 1 << 7;
	curuser = prevuser = 0;
	waiting = 0;
	for (i = 0; i < ITABN; i++)
		itab[i].vec = itab[i].pri = 0;
	mmu_reset();
	cons_reset();
	rk_reset();
}

static uint16_t
read8(uint16_t a)
{
	return ub_read8(mmu_decode(a, 0, curuser));
}

static uint16_t
read16(uint16_t a)
{
	return ub_read16(mmu_decode(a, 0, curuser));
}

static void
write8(uint16_t a, uint16_t v)
{
	ub_write8(mmu_decode(a, 1, curuser), v);
}

static void
write16(uint16_t a, uint16_t v)
{
	ub_write16(mmu_decode(a, 1, curuser), v);
}

/*
 * aget resolves an operand to a virtual address. A register operand
 * yields 0170000 | n, which no guest address ever decodes to.
 */
#define ISREG(a) (((a) & 0177770) == 0170000)

static uint16_t
memread16(uint16_t a)
{
	if (ISREG(a))
		return R[a & 7];
	return read16(a);
}

static uint16_t
memread(uint16_t a, int l)
{
	if (ISREG(a)) {
		if (l == 2)
			return R[a & 7];
		return R[a & 7] & 0xFF;
	}
	if (l == 2)
		return read16(a);
	return read8(a);
}

static void
memwrite16(uint16_t a, uint16_t v)
{
	if (ISREG(a))
		R[a & 7] = v;
	else
		write16(a, v);
}

static void
memwrite(uint16_t a, int l, uint16_t v)
{
	if (ISREG(a)) {
		if (l == 2)
			R[a & 7] = v;
		else
			R[a & 7] = (R[a & 7] & 0xFF00) | (v & 0xFF);
		return;
	}
	if (l == 2)
		write16(a, v);
	else
		write8(a, v);
}

static uint16_t
fetch16(void)
{
	uint16_t val = read16(R[7]);

	R[7] += 2;
	return val;
}

static void
push(uint16_t v)
{
	R[6] -= 2;
	write16(R[6], v);
}

static uint16_t
pop(void)
{
	uint16_t val = read16(R[6]);

	R[6] += 2;
	return val;
}

static uint16_t
aget(uint8_t v, int l)
{
	uint16_t addr = 0;

	if ((v & 070) == 000)
		return 0170000 | (v & 7);
	if (((v & 7) >= 6) || (v & 010))
		l = 2;
	switch (v & 060) {
	case 000:		/* mode 1, register deferred: the address is in R */
		return R[v & 7];
	case 020:
		addr = R[v & 7];
		R[v & 7] += l;
		break;
	case 040:
		R[v & 7] -= l;
		addr = R[v & 7];
		break;
	case 060:
		addr = fetch16();
		addr += R[v & 7];
		break;
	}
	if (v & 010)
		addr = read16(addr);
	return addr;
}

static void
branch(uint16_t o)
{
	int16_t off = (int16_t)(int8_t)(o & 0xFF);

	R[7] += (uint16_t)(off * 2);
}

static void
switchmode(int newm)
{
	prevuser = curuser;
	curuser = newm;
	if (prevuser)
		USP = R[6];
	else
		KSP = R[6];
	if (curuser)
		R[6] = USP;
	else
		R[6] = KSP;
	PS &= 0007777;
	if (curuser)
		PS |= (1 << 15) | (1 << 14);
	if (prevuser)
		PS |= (1 << 13) | (1 << 12);
}

/* unibus.c calls this for a store to the PS at 0777776. */
void cpu_setps(uint16_t v);

void
cpu_setps(uint16_t v)
{
	switchmode((v >> 14) != 0);
	prevuser = ((v >> 12) & 3) != 0;
	PS = v;
}

#define SETZ(b) do { if (b) PS |= FLAGZ; } while (0)

static void
op_mov(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t uval = memread(aget(s, l), l);
	uint16_t da = aget(d, l);

	PS &= 0xFFF1;
	if (uval & msb)
		PS |= FLAGN;
	SETZ(uval == 0);
	if (ISREG(da) && l == 1) {
		l = 2;
		if (uval & msb)
			uval |= 0xFF00;
	}
	memwrite(da, l, uval);
}

static void
op_cmp(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t val1 = memread(aget(s, l), l);
	uint16_t da = aget(d, l);
	uint16_t val2 = memread(da, l);
	uint16_t sval = (val1 - val2) & max;

	PS &= 0xFFF0;
	SETZ(sval == 0);
	if (sval & msb)
		PS |= FLAGN;
	if (((val1 ^ val2) & msb) && (!((val2 ^ sval) & msb)))
		PS |= FLAGV;
	if (val1 < val2)
		PS |= FLAGC;
}

static void
op_bit(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t val1 = memread(aget(s, l), l);
	uint16_t da = aget(d, l);
	uint16_t val2 = memread(da, l);
	uint16_t uval = val1 & val2;

	PS &= 0xFFF1;
	SETZ(uval == 0);
	if (uval & msb)
		PS |= FLAGN;
}

static void
op_bic(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t val1 = memread(aget(s, l), l);
	uint16_t da = aget(d, l);
	uint16_t val2 = memread(da, l);
	uint16_t uval = (max ^ val1) & val2;

	PS &= 0xFFF1;
	SETZ(uval == 0);
	if (uval & msb)
		PS |= FLAGN;
	memwrite(da, l, uval);
}

static void
op_bis(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t val1 = memread(aget(s, l), l);
	uint16_t da = aget(d, l);
	uint16_t val2 = memread(da, l);
	uint16_t uval = val1 | val2;

	PS &= 0xFFF1;
	SETZ(uval == 0);
	if (uval & msb)
		PS |= FLAGN;
	memwrite(da, l, uval);
}

static void
op_add(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	uint16_t val1 = memread16(aget(s, 2));
	uint16_t da = aget(d, 2);
	uint16_t val2 = memread16(da);
	uint32_t sum = (uint32_t)val1 + val2;
	uint16_t uval = sum & 0xFFFF;

	PS &= 0xFFF0;
	SETZ(uval == 0);
	if (uval & 0x8000)
		PS |= FLAGN;
	if (!((val1 ^ val2) & 0x8000) && ((val2 ^ uval) & 0x8000))
		PS |= FLAGV;
	if (sum > 0xFFFF)
		PS |= FLAGC;
	memwrite16(da, uval);
}

static void
op_sub(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	uint16_t val1 = memread16(aget(s, 2));
	uint16_t da = aget(d, 2);
	uint16_t val2 = memread16(da);
	uint16_t uval = (val2 - val1) & 0xFFFF;

	PS &= 0xFFF0;
	SETZ(uval == 0);
	if (uval & 0x8000)
		PS |= FLAGN;
	if (((val1 ^ val2) & 0x8000) && (!((val2 ^ uval) & 0x8000)))
		PS |= FLAGV;
	if (val1 > val2)
		PS |= FLAGC;
	memwrite16(da, uval);
}

static void
op_jsr(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	uint16_t uval = aget(d, 2);

	if (ISREG(uval))
		TRAP(INTINVAL);
	push(R[s & 7]);
	R[s & 7] = R[7];
	R[7] = uval;
}

static void
op_mul(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	int32_t val1 = (int16_t)R[s & 7];
	uint16_t da = aget(d, 2);
	int32_t val2 = (int16_t)memread16(da);
	int32_t sval = val1 * val2;

	R[s & 7] = (uint32_t)sval >> 16;
	R[(s & 7) | 1] = sval & 0xFFFF;
	PS &= 0xFFF0;
	if (sval < 0)
		PS |= FLAGN;
	SETZ(sval == 0);
	if (sval < -32768 || sval > 32767)
		PS |= FLAGC;
}

static void
op_div(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	int32_t val1 = (int32_t)(((uint32_t)R[s & 7] << 16) | R[(s & 7) | 1]);
	uint16_t da = aget(d, 2);
	int32_t val2 = (int16_t)memread16(da);
	int32_t q;

	PS &= 0xFFF0;
	if (val2 == 0) {
		PS |= FLAGC | FLAGV;
		return;
	}
	if (val1 == (int32_t)0x80000000 && val2 == -1) {
		PS |= FLAGV;
		return;
	}
	q = val1 / val2;
	if (q > 32767 || q < -32768) {
		PS |= FLAGV;
		return;
	}
	R[s & 7] = q & 0xFFFF;
	R[(s & 7) | 1] = (val1 % val2) & 0xFFFF;
	SETZ(R[s & 7] == 0);
	if (R[s & 7] & 0100000)
		PS |= FLAGN;
}

static void
op_ash(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	uint16_t val1 = R[s & 7];
	uint16_t da = aget(d, 2);
	uint16_t val2 = memread16(da) & 077;
	uint32_t sval;

	PS &= 0xFFF0;
	if (val2 & 040) {
		val2 = (077 ^ val2) + 1;
		if (val1 & 0100000) {
			sval = 0xFFFF ^ (0xFFFF >> val2);
			sval |= val1 >> val2;
		} else
			sval = val1 >> val2;
		if (val1 & (1 << (val2 - 1)))
			PS |= FLAGC;
	} else {
		sval = ((uint32_t)val1 << val2) & 0xFFFF;
		if (val2 && (val1 & (1 << (16 - val2))))
			PS |= FLAGC;
	}
	R[s & 7] = sval;
	SETZ(sval == 0);
	if (sval & 0100000)
		PS |= FLAGN;
	if ((sval & 0100000) != (val1 & 0100000))
		PS |= FLAGV;
}

static void
op_ashc(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	uint32_t val1 = ((uint32_t)R[s & 7] << 16) | R[(s & 7) | 1];
	uint16_t da = aget(d, 2);
	uint16_t val2 = memread16(da) & 077;
	uint32_t sval;

	PS &= 0xFFF0;
	if (val2 & 040) {
		val2 = (077 ^ val2) + 1;
		if (val1 & 0x80000000) {
			sval = 0xFFFFFFFF ^ (0xFFFFFFFF >> val2);
			sval |= val1 >> val2;
		} else
			sval = val1 >> val2;
		if (val1 & (1UL << (val2 - 1)))
			PS |= FLAGC;
	} else {
		sval = (val1 << val2) & 0xFFFFFFFF;
		if (val2 && (val1 & (1UL << (32 - val2))))
			PS |= FLAGC;
	}
	R[s & 7] = (sval >> 16) & 0xFFFF;
	R[(s & 7) | 1] = sval & 0xFFFF;
	SETZ(sval == 0);
	if (sval & 0x80000000)
		PS |= FLAGN;
	if ((sval & 0x80000000) != (val1 & 0x80000000))
		PS |= FLAGV;
}

static void
op_xor(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint8_t s = (instr & 07700) >> 6;
	uint16_t val1 = R[s & 7];
	uint16_t da = aget(d, 2);
	uint16_t val2 = memread16(da);
	uint16_t uval = val1 ^ val2;

	PS &= 0xFFF1;
	SETZ(uval == 0);
	if (uval & 0x8000)
		PS |= FLAGN;
	memwrite16(da, uval);
}

static void
op_sob(uint16_t instr)
{
	uint8_t s = (instr & 07700) >> 6;
	uint8_t o = instr & 077;

	R[s & 7]--;
	if (R[s & 7])
		R[7] -= o << 1;
}

static void
op_clr(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);

	PS &= 0xFFF0;
	PS |= FLAGZ;
	memwrite(aget(d, l), l, 0);
}

static void
op_com(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);
	uint16_t uval = memread(da, l) ^ max;

	PS &= 0xFFF0;
	PS |= FLAGC;
	if (uval & msb)
		PS |= FLAGN;
	SETZ(uval == 0);
	memwrite(da, l, uval);
}

static void
op_inc(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);
	uint16_t uval = (memread(da, l) + 1) & max;

	PS &= 0xFFF1;
	if (uval & msb)
		PS |= FLAGN;
	if (uval == msb)
		PS |= FLAGV;
	SETZ(uval == 0);
	memwrite(da, l, uval);
}

static void
op_dec(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t maxp = l == 2 ? 0x7FFF : 0x7f;
	uint16_t da = aget(d, l);
	uint16_t uval = (memread(da, l) - 1) & max;

	PS &= 0xFFF1;
	if (uval & msb)
		PS |= FLAGN;
	if (uval == maxp)
		PS |= FLAGV;
	SETZ(uval == 0);
	memwrite(da, l, uval);
}

static void
op_neg(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);
	uint16_t sval = (-memread(da, l)) & max;

	PS &= 0xFFF0;
	if (sval & msb)
		PS |= FLAGN;
	if (sval == 0)
		PS |= FLAGZ;
	else
		PS |= FLAGC;
	if (sval == msb)
		PS |= FLAGV;
	memwrite(da, l, sval);
}

static void
op_adc(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);
	uint16_t uval = memread(da, l);

	if (PS & FLAGC) {
		PS &= 0xFFF0;
		if ((uval + 1) & msb)
			PS |= FLAGN;
		SETZ(uval == max);
		if (uval == (uint16_t)(msb - 1))
			PS |= FLAGV;
		if (uval == max)
			PS |= FLAGC;
		memwrite(da, l, (uval + 1) & max);
	} else {
		PS &= 0xFFF0;
		if (uval & msb)
			PS |= FLAGN;
		SETZ(uval == 0);
	}
}

static void
op_sbc(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);
	uint16_t sval = memread(da, l);

	if (PS & FLAGC) {
		PS &= 0xFFF0;
		if ((sval - 1) & msb)
			PS |= FLAGN;
		SETZ(sval == 1);
		if (sval)
			PS |= FLAGC;
		if (sval == msb)
			PS |= FLAGV;
		memwrite(da, l, (sval - 1) & max);
	} else {
		PS &= 0xFFF0;
		if (sval & msb)
			PS |= FLAGN;
		SETZ(sval == 0);
		if (sval == msb)
			PS |= FLAGV;
		PS |= FLAGC;
	}
}

static void
op_tst(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t uval = memread(aget(d, l), l);

	PS &= 0xFFF0;
	if (uval & msb)
		PS |= FLAGN;
	SETZ(uval == 0);
}

static void
op_ror(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint32_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);
	uint32_t sval = memread(da, l);

	if (PS & FLAGC)
		sval |= max + 1;
	PS &= 0xFFF0;
	if (sval & 1)
		PS |= FLAGC;
	if (sval & (max + 1))
		PS |= FLAGN;
	SETZ(!(sval & max));
	if (((sval & 1) != 0) ^ ((sval & (max + 1)) != 0))
		PS |= FLAGV;
	sval >>= 1;
	memwrite(da, l, sval);
}

static void
op_rol(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint32_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);
	uint32_t sval = (uint32_t)memread(da, l) << 1;

	if (PS & FLAGC)
		sval |= 1;
	PS &= 0xFFF0;
	if (sval & (max + 1))
		PS |= FLAGC;
	if (sval & msb)
		PS |= FLAGN;
	SETZ(!(sval & max));
	if ((sval ^ (sval >> 1)) & msb)
		PS |= FLAGV;
	sval &= max;
	memwrite(da, l, sval);
}

static void
op_asr(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t da = aget(d, l);
	uint16_t uval = memread(da, l);

	PS &= 0xFFF0;
	if (uval & 1)
		PS |= FLAGC;
	if (uval & msb)
		PS |= FLAGN;
	if (((uval & msb) != 0) ^ ((uval & 1) != 0))
		PS |= FLAGV;
	uval = (uval & msb) | (uval >> 1);
	SETZ(uval == 0);
	memwrite(da, l, uval);
}

static void
op_asl(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t msb = l == 2 ? 0x8000 : 0x80;
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);
	uint32_t sval = memread(da, l);

	PS &= 0xFFF0;
	if (sval & msb)
		PS |= FLAGC;
	if (sval & (msb >> 1))
		PS |= FLAGN;
	if ((sval ^ (sval << 1)) & msb)
		PS |= FLAGV;
	sval = (sval << 1) & max;
	SETZ(sval == 0);
	memwrite(da, l, sval);
}

static void
op_sxt(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t max = l == 2 ? 0xFFFF : 0xff;
	uint16_t da = aget(d, l);

	if (PS & FLAGN)
		memwrite(da, l, max);
	else {
		PS |= FLAGZ;
		memwrite(da, l, 0);
	}
}

static void
op_jmp(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint16_t uval = aget(d, 2);

	if (ISREG(uval))
		TRAP(INTINVAL);
	R[7] = uval;
}

static void
op_swab(uint16_t instr)
{
	uint8_t d = instr & 077;
	int l = 2 - (instr >> 15);
	uint16_t da = aget(d, l);
	uint16_t uval = memread(da, l);

	uval = ((uval >> 8) | (uval << 8)) & 0xFFFF;
	PS &= 0xFFF0;
	SETZ((uval & 0xFF) == 0);
	if (uval & 0x80)
		PS |= FLAGN;
	memwrite(da, l, uval);
}

static void
op_mark(uint16_t instr)
{
	R[6] = R[7] + ((instr & 077) << 1);
	R[7] = R[5];
	R[5] = pop();
}

static void
op_mfpi(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint16_t da = aget(d, 2);
	uint16_t uval;

	if (da == 0170006) {
		if (curuser == prevuser)
			uval = R[6];
		else
			uval = prevuser ? USP : KSP;
	} else if (ISREG(da))
		uval = R[da & 7];
	else
		uval = ub_read16(mmu_decode(da, 0, prevuser));
	push(uval);
	PS &= 0xFFF0;
	PS |= FLAGC;
	SETZ(uval == 0);
	if (uval & 0x8000)
		PS |= FLAGN;
}

static void
op_mtpi(uint16_t instr)
{
	uint8_t d = instr & 077;
	uint16_t da = aget(d, 2);
	uint16_t uval = pop();

	if (da == 0170006) {
		if (curuser == prevuser)
			R[6] = uval;
		else if (prevuser)
			USP = uval;
		else
			KSP = uval;
	} else if (ISREG(da))
		R[da & 7] = uval;
	else
		ub_write16(mmu_decode(da, 1, prevuser), uval);
	PS &= 0xFFF0;
	PS |= FLAGC;
	SETZ(uval == 0);
	if (uval & 0x8000)
		PS |= FLAGN;
}

static void
op_rts(uint16_t instr)
{
	uint8_t d = instr & 7;

	R[7] = R[d];
	R[d] = pop();
}

/* EMT, TRAP, BPT, IOT: a trap through the vector, entered from the
 * current PC (already advanced past the instruction). */
static void
op_emtx(uint16_t instr)
{
	uint16_t vec;
	uint16_t prev;

	if ((instr & 0177400) == 0104000)
		vec = INTEMT;
	else if ((instr & 0177400) == 0104400)
		vec = INTTRAP;
	else if (instr == 3)
		vec = INTBPT;
	else
		vec = INTIOT;
	prev = PS;
	switchmode(0);
	push(prev);
	push(R[7]);
	R[7] = ub_read16(vec);
	PS = ub_read16(vec + 2);
	if (prevuser)
		PS |= (1 << 13) | (1 << 12);
}

static void
op_rtt(void)
{
	uint16_t uval;

	R[7] = pop();
	uval = pop();
	if (curuser) {
		uval &= 047;
		uval |= PS & 0177730;
	}
	cpu_setps(uval);
}

void
cpu_step(void)
{
	uint16_t instr;

	PC = R[7];
	instr = ub_read16(mmu_decode(PC, 0, curuser));
	R[7] += 2;

	switch ((instr >> 12) & 007) {
	case 001:
		op_mov(instr);
		return;
	case 002:
		op_cmp(instr);
		return;
	case 003:
		op_bit(instr);
		return;
	case 004:
		op_bic(instr);
		return;
	case 005:
		op_bis(instr);
		return;
	}
	switch ((instr >> 12) & 017) {
	case 006:
		op_add(instr);
		return;
	case 016:
		op_sub(instr);
		return;
	}
	switch ((instr >> 9) & 0177) {
	case 0004:
		op_jsr(instr);
		return;
	case 0070:
		op_mul(instr);
		return;
	case 0071:
		op_div(instr);
		return;
	case 0072:
		op_ash(instr);
		return;
	case 0073:
		op_ashc(instr);
		return;
	case 0074:
		op_xor(instr);
		return;
	case 0077:
		op_sob(instr);
		return;
	}
	switch ((instr >> 6) & 00777) {
	case 00050:
		op_clr(instr);
		return;
	case 00051:
		op_com(instr);
		return;
	case 00052:
		op_inc(instr);
		return;
	case 00053:
		op_dec(instr);
		return;
	case 00054:
		op_neg(instr);
		return;
	case 00055:
		op_adc(instr);
		return;
	case 00056:
		op_sbc(instr);
		return;
	case 00057:
		op_tst(instr);
		return;
	case 00060:
		op_ror(instr);
		return;
	case 00061:
		op_rol(instr);
		return;
	case 00062:
		op_asr(instr);
		return;
	case 00063:
		op_asl(instr);
		return;
	case 00067:
		op_sxt(instr);
		return;
	}
	switch (instr & 0177700) {
	case 0000100:
		op_jmp(instr);
		return;
	case 0000300:
		op_swab(instr);
		return;
	case 0006400:
		op_mark(instr);
		return;
	case 0006500:
		op_mfpi(instr);
		return;
	case 0006600:
		op_mtpi(instr);
		return;
	}
	if ((instr & 0177770) == 0000200) {
		op_rts(instr);
		return;
	}
	switch (instr & 0177400) {
	case 0000400:
		branch(instr);
		return;
	case 0001000:
		if (!(PS & FLAGZ))
			branch(instr);
		return;
	case 0001400:
		if (PS & FLAGZ)
			branch(instr);
		return;
	case 0002000:
		if (!(!!(PS & FLAGN) ^ !!(PS & FLAGV)))
			branch(instr);
		return;
	case 0002400:
		if (!!(PS & FLAGN) ^ !!(PS & FLAGV))
			branch(instr);
		return;
	case 0003000:
		if (!(!!(PS & FLAGN) ^ !!(PS & FLAGV)) && !(PS & FLAGZ))
			branch(instr);
		return;
	case 0003400:
		if ((!!(PS & FLAGN) ^ !!(PS & FLAGV)) || (PS & FLAGZ))
			branch(instr);
		return;
	case 0100000:
		if (!(PS & FLAGN))
			branch(instr);
		return;
	case 0100400:
		if (PS & FLAGN)
			branch(instr);
		return;
	case 0101000:
		if (!(PS & FLAGC) && !(PS & FLAGZ))
			branch(instr);
		return;
	case 0101400:
		if ((PS & FLAGC) || (PS & FLAGZ))
			branch(instr);
		return;
	case 0102000:
		if (!(PS & FLAGV))
			branch(instr);
		return;
	case 0102400:
		if (PS & FLAGV)
			branch(instr);
		return;
	case 0103000:
		if (!(PS & FLAGC))
			branch(instr);
		return;
	case 0103400:
		if (PS & FLAGC)
			branch(instr);
		return;
	}
	if (((instr & 0177000) == 0104000) || (instr == 3) || (instr == 4)) {
		op_emtx(instr);
		return;
	}
	if ((instr & 0177740) == 0240) {
		if (instr & 020)
			PS |= instr & 017;
		else
			PS &= ~instr & 017;
		return;
	}
	switch (instr) {
	case 0:
		if (curuser)
			TRAP(INTBUS);
		fatal("halt", PC);
		return;
	case 1:
		if (!curuser)
			waiting = 1;
		return;
	case 2:
	case 6:
		op_rtt();
		return;
	case 5:
		if (!curuser) {
			cons_reset();
			rk_reset();
		}
		return;
	case 0170011:		/* SETD: no FPP, ignored as UNIX expects */
		return;
	}
	TRAP(INTINVAL);
}

/*
 * Enter the vector: push PS and PC on the kernel stack and load the new
 * PC and PS. A fault inside the push is the double fault main() stops on.
 */
void
cpu_trapat(uint16_t vec)
{
	uint16_t prev = PS;

	switchmode(0);
	push(prev);
	push(R[7]);
	R[7] = ub_read16(vec);
	PS = ub_read16(vec + 2);
	if (prevuser)
		PS |= (1 << 13) | (1 << 12);
	waiting = 0;
}

void
cpu_interrupt(uint8_t vec, uint8_t pri)
{
	int i, j;

	for (i = 0; i < ITABN; i++)
		if (itab[i].vec == vec)
			return;	/* already pending */
	for (i = 0; i < ITABN; i++)
		if (itab[i].vec == 0 || itab[i].pri < pri)
			break;
	if (i >= ITABN)
		fatal("interrupt table full", vec);
	for (j = ITABN - 1; j > i; j--)
		itab[j] = itab[j - 1];
	itab[i].vec = vec;
	itab[i].pri = pri;
	waiting = 0;
}

int
cpu_intpending(void)
{
	return itab[0].vec != 0 && itab[0].pri > ((PS >> 5) & 7);
}

void
cpu_handleinterrupt(void)
{
	uint8_t vec = itab[0].vec;
	int i;

	for (i = 0; i < ITABN - 1; i++)
		itab[i] = itab[i + 1];
	itab[ITABN - 1].vec = 0;
	itab[ITABN - 1].pri = 0;
	cpu_trapat(vec);
}
