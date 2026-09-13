/*
 * romprobe: print what the RP2040 Boot ROM exposes to a user process, in
 * the order lib/libc/arm/gen/rom_float_resolver.S checks it, so a
 * resolver exit 70 on the board can be traced to the failing step.
 *
 * RP2040 datasheet section 2.8.2: the ROM header at 0x10 holds the magic
 * 'M' 'u' 0x01, then the version byte at 0x13; halfwords at 0x14 and 0x16
 * point at the function and data tables and the halfword at 0x18 at the
 * lookup helper. Section 2.8.3.2: the soft-float and soft-double tables
 * are data-table entries 'S','F' and 'S','D'.
 */
#include <stdio.h>

typedef void *(*lookup_fn)(unsigned short *table, unsigned code);

static unsigned
b(unsigned a)
{
	return *(volatile unsigned char *)a;
}

static unsigned
h(unsigned a)
{
	return *(volatile unsigned short *)a;
}

static unsigned
w(unsigned a)
{
	return *(volatile unsigned *)a;
}

static void
table(const char *name, unsigned code, int version)
{
	lookup_fn lookup = (lookup_fn)h(0x18);
	unsigned t = (unsigned)lookup((unsigned short *)h(0x16), code);
	unsigned size, i;

	printf("%s: table=%#x", name, t);
	if (t == 0) {
		printf(" (absent)\n");
		return;
	}
	if (version >= 2)
		size = b(t - 2) * 4;
	else
		size = 0x54;
	printf(" byte[-2]=%#x hword[-2]=%#x size=%u\n", b(t - 2), h(t - 2),
	    size);
	for (i = 0; i < 6; i++)
		printf("  [%u] %#x\n", i * 4, w(t + i * 4));
}

int
main(void)
{
	int version = b(0x13);

	printf("header %#x %#x %#x version %d\n", b(0x10), b(0x11), b(0x12),
	    version);
	printf("func table %#x data table %#x lookup %#x\n", h(0x14), h(0x16),
	    h(0x18));
	table("SF", 0x4653, version);
	table("SD", 0x4453, version);
	return 0;
}
