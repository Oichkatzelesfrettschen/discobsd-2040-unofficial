/*
 * The Unibus: memory below MEMSIZE, the I/O page at 0760000, and a
 * nonexistent-memory hole between them that traps through vector 4. An
 * odd word address traps the same way. CPU accesses trap; DMA accesses
 * (the RK11) return a status instead, so a device error never masquerades
 * as a CPU fault.
 */
#include "pdp11.h"

uint16_t mem[MEMSIZE / 2];

void cpu_setps(uint16_t v);

static uint16_t
io_read16(uint32_t a)
{
	switch (a) {
	case 0777546:
		return LKS;
	case 0777570:		/* console switches: 0173030 would mean single user */
		return 0;
	case 0777572:
		return SR0;
	case 0777574:		/* SR1: no 11/40 instruction is restartable */
		return 0;
	case 0777576:
		return SR2;
	case 0777776:
		return PS;
	}
	if ((a & 0777770) == 0777560)
		return cons_read16(a);
	if ((a & 0777760) == 0777400)
		return rk_read16(a);
	if (((a & 0777600) == 0772200) || ((a & 0777600) == 0777600))
		return mmu_read16(a);
	TRAP(INTBUS);
	return 0;
}

static void
io_write16(uint32_t a, uint16_t v)
{
	switch (a) {
	case 0777776:
		cpu_setps(v);
		return;
	case 0777546:
		LKS = v & 0300;
		return;
	case 0777570:
		return;
	case 0777572:
		SR0 = v;
		return;
	}
	if ((a & 0777770) == 0777560) {
		cons_write16(a, v);
		return;
	}
	if ((a & 0777760) == 0777400) {
		rk_write16(a, v);
		return;
	}
	if (((a & 0777600) == 0772200) || ((a & 0777600) == 0777600)) {
		mmu_write16(a, v);
		return;
	}
	TRAP(INTBUS);
}

uint16_t
ub_read16(uint32_t a)
{
	if (a & 1)
		TRAP(INTBUS);
	if (a < MEMSIZE)
		return mem[a >> 1];
	if (a < IOPAGE)
		TRAP(INTBUS);
	return io_read16(a);
}

void
ub_write16(uint32_t a, uint16_t v)
{
	if (a & 1)
		TRAP(INTBUS);
	if (a < MEMSIZE) {
		mem[a >> 1] = v;
		return;
	}
	if (a < IOPAGE)
		TRAP(INTBUS);
	io_write16(a, v);
}

uint16_t
ub_read8(uint32_t a)
{
	uint16_t w;

	if (a < MEMSIZE)
		w = mem[a >> 1];
	else
		w = ub_read16(a & ~1UL);
	return (a & 1) ? (w >> 8) : (w & 0xFF);
}

/*
 * A byte store to a device register is a read-modify-write of the word;
 * the console and RK11 registers tolerate that because reading them has
 * no side effect except TKB's done-bit clear, which V6 never reaches
 * with a byte store.
 */
void
ub_write8(uint32_t a, uint16_t v)
{
	uint16_t w;

	if (a < MEMSIZE) {
		w = mem[a >> 1];
		if (a & 1)
			w = (w & 0x00FF) | ((v & 0xFF) << 8);
		else
			w = (w & 0xFF00) | (v & 0xFF);
		mem[a >> 1] = w;
		return;
	}
	w = ub_read16(a & ~1UL);
	if (a & 1)
		w = (w & 0x00FF) | ((v & 0xFF) << 8);
	else
		w = (w & 0xFF00) | (v & 0xFF);
	ub_write16(a & ~1UL, w);
}

int
dma_read16(uint32_t a, uint16_t *v)
{
	if ((a & 1) || a >= MEMSIZE)
		return -1;
	*v = mem[a >> 1];
	return 0;
}

int
dma_write16(uint32_t a, uint16_t v)
{
	if ((a & 1) || a >= MEMSIZE)
		return -1;
	mem[a >> 1] = v;
	return 0;
}
