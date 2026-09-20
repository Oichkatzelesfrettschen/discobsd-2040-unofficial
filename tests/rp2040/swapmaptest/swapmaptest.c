/*
 * swapmaptest: read the swap allocation map through sysctl(3) on the board
 * and hold what comes back to the bounds of the swap device.
 *
 * A CTL_VM.VM_SWAPMAP read returns the free runs of the map as struct
 * mapent, so every entry the kernel hands out describes a span inside the
 * device: m_addr is at least SWAP_IMAGE_ALIGN, the blocks below it carrying
 * the image the swap cursor keeps, and m_addr + m_size is at most nswap,
 * which CTL_VM.VM_NSWAP reports. A copy that started at the struct map
 * descriptor instead would put the low and high halves of a kernel address
 * in the first entry, and 0x2000 alone exceeds a swap device this port
 * configures, so the bound is what separates entries from pointers.
 *
 * The extent is a whole number of entries and holds steady across two
 * reads: the size query and the copy derive one length in the kernel.
 */
#include <sys/param.h>
#include <sys/sysctl.h>	/* sys/map.h and sys/vmparam.h, which self-include */
#include <stdio.h>
#include <string.h>

/* SMAPSIZ is 29 compact slots on this port; the buffer covers any of them. */
#define BUFENTS		64

static struct mapent ent[BUFENTS];
static int failures;

static void
fail(const char *what)
{
	printf("FAIL %s\n", what);
	failures++;
}

int
main(void)
{
	int name[2];
	size_t queried, copied;
	unsigned nswap;
	size_t nswaplen;
	unsigned i, runs;
	unsigned long top;

	name[0] = CTL_VM;
	name[1] = VM_NSWAP;
	nswaplen = sizeof(nswap);
	if (sysctl(name, 2, &nswap, &nswaplen, NULL, 0) < 0) {
		perror("vm.nswap");
		return (1);
	}
	if (nswaplen != sizeof(nswap))
		fail("vm.nswap length");
	printf("vm.nswap = %u blocks (%u kbytes)\n", nswap,
	    (unsigned)(nswap * DEV_BSIZE / 1024));

	name[1] = VM_SWAPMAP;
	queried = 0;
	if (sysctl(name, 2, NULL, &queried, NULL, 0) < 0) {
		perror("vm.swapmap size");
		return (1);
	}
	printf("vm.swapmap = %u bytes, %u entries of %u\n", (unsigned)queried,
	    (unsigned)(queried / sizeof(struct mapent)),
	    (unsigned)sizeof(struct mapent));

	if (queried == 0 || queried % sizeof(struct mapent) != 0)
		fail("extent is a whole number of entries");
	if (queried > sizeof(ent)) {
		fail("extent exceeds the test buffer");
		return (1);
	}

	memset(ent, 0xa5, sizeof(ent));
	copied = sizeof(ent);
	if (sysctl(name, 2, ent, &copied, NULL, 0) < 0) {
		perror("vm.swapmap");
		return (1);
	}
	if (copied != queried)
		fail("the copy returns the length the query stated");

	/* Nothing past the stated length is written. */
	for (i = copied; i < sizeof(ent); i++)
		if (((unsigned char *)ent)[i] != 0xa5) {
			fail("the copy stays inside the stated length");
			break;
		}

	/*
	 * Every run lies inside the device, above the image the cursor holds
	 * at block SWAP_IMAGE_ALIGN. The map ends at the first zero-size
	 * entry, which subr_rmap.c keeps as its terminator.
	 */
	runs = 0;
	for (i = 0; i < copied / sizeof(struct mapent); i++) {
		if (ent[i].m_size == 0)
			break;
		top = (unsigned long)ent[i].m_addr +
		    (unsigned long)ent[i].m_size;
		printf("  run %u: addr %lu size %lu top %lu\n", i,
		    (unsigned long)ent[i].m_addr,
		    (unsigned long)ent[i].m_size, top);
		if (ent[i].m_addr < SWAP_IMAGE_ALIGN)
			fail("run starts at or above the image alignment");
		if (top > nswap)
			fail("run ends inside the swap device");
		runs++;
	}
	if (runs == 0)
		fail("the map describes at least one free run");
	printf("%u free runs, terminator at entry %u\n", runs, i);

	if (failures) {
		printf("SWAPMAPTEST FAILED (%d)\n", failures);
		return (1);
	}
	printf("SWAPMAPTEST OK\n");
	return (0);
}
