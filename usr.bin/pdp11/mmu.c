/*
 * KT11-D segmentation as the 11/40 has it: eight kernel and eight user
 * pages, a PAR and a PDR each, SR0 and SR2. The access-control field of
 * the PDR decides what a reference may do:
 *
 *   ACF 0  nonresident       abort, SR0 bit 15
 *   ACF 1  read-only         abort on write, SR0 bit 13
 *   ACF 2  read-only         abort on write, SR0 bit 13
 *   ACF 4  read/write
 *   ACF 5  read/write
 *   ACF 6  read/write
 *   ACF 3, 7  unused         abort as nonresident
 *
 * The page length check (SR0 bit 14) follows, then a write sets the
 * PDR's W bit. An abort records the page and mode in SR0 and the
 * faulting PC in SR2, then traps through 0250.
 */
#include "pdp11.h"

struct page {
	uint16_t par;
	uint16_t pdr;
};

static struct page pages[16];
uint16_t SR0, SR2;

void
mmu_reset(void)
{
	int i;

	for (i = 0; i < 16; i++)
		pages[i].par = pages[i].pdr = 0;
	SR0 = SR2 = 0;
}

static void
abort_at(uint16_t a, int user, uint16_t why)
{
	SR0 = why | 1;
	SR0 |= (a >> 12) & 016;		/* page number, bits 1-3 */
	if (user)
		SR0 |= (1 << 5) | (1 << 6);
	SR2 = PC;
	TRAP(INTFAULT);
}

uint32_t
mmu_decode(uint16_t a, int w, int user)
{
	uint8_t i, acf, block, plf;
	uint32_t aa;

	if (!(SR0 & 1))
		return a >= 0160000 ? (uint32_t)a + 0600000 : a;

	i = user ? ((a >> 13) + 8) : (a >> 13);
	acf = pages[i].pdr & 7;
	if (acf == 0 || acf == 3 || acf == 7)
		abort_at(a, user, 1 << 15);
	if (w && acf < 4)
		abort_at(a, user, 1 << 13);
	block = (a >> 6) & 0177;
	plf = (pages[i].pdr >> 8) & 0177;
	if ((pages[i].pdr & 8) ? (block < plf) : (block > plf))
		abort_at(a, user, 1 << 14);
	if (w)
		pages[i].pdr |= 1 << 6;
	aa = pages[i].par & 07777;
	aa += block;
	aa <<= 6;
	aa += a & 077;
	return aa;
}

uint16_t
mmu_read16(uint32_t a)
{
	uint8_t i = (a & 017) >> 1;

	if (a >= 0772300 && a < 0772320)
		return pages[i].pdr;
	if (a >= 0772340 && a < 0772360)
		return pages[i].par;
	if (a >= 0777600 && a < 0777620)
		return pages[i + 8].pdr;
	if (a >= 0777640 && a < 0777660)
		return pages[i + 8].par;
	TRAP(INTBUS);
	return 0;
}

void
mmu_write16(uint32_t a, uint16_t v)
{
	uint8_t i = (a & 017) >> 1;

	if (a >= 0772300 && a < 0772320) {
		pages[i].pdr = v & 077516;	/* W bit and unused bits stay clear */
		return;
	}
	if (a >= 0772340 && a < 0772360) {
		pages[i].par = v & 07777;
		return;
	}
	if (a >= 0777600 && a < 0777620) {
		pages[i + 8].pdr = v & 077516;
		return;
	}
	if (a >= 0777640 && a < 0777660) {
		pages[i + 8].par = v & 07777;
		return;
	}
	TRAP(INTBUS);
}
