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
 * Stamp the identity words of a linked RP2040 kernel, or check them.
 *
 * The flash image of a kernel is the bytes its PT_LOAD segments place from
 * 0x10000000 up, gaps zero-filled, which is what objcopy -O binary writes
 * to unix.bin. Three reserved words of the vector table at 0x10000100,
 * entries 8, 9 and 10, which ARMv6-M never fetches, identify that image:
 *
 *   0x10000120  length of the image in bytes
 *   0x10000124  CRC32 of the image, as zlib's crc32() computes it, with
 *               this word and the board descriptor read as zero
 *   0x10000128  address of the 16-byte board descriptor, set by the linker
 *               from __board_start (locore0.S)
 *
 * The descriptor is outside the sum because distrib/rp2040/mkboarduf2.py
 * rewrites it per board in a linked image, so one kernel build keeps one
 * identity on every board it is issued for. The kernel recomputes the sum
 * over its own flash at boot (image_identify() in machdep.c) and
 * tools/check_rp2040_vectors.py recomputes it from the ELF.
 *
 * Usage: kernsum unix       stamp the length and CRC words, both zero
 *        kernsum -c unix    check them; print the length and the CRC
 *
 * The exit status is 0 on success, 1 when the words are absent, already
 * stamped or wrong, and 2 for a file that is not such a kernel.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_BASE	0x10000000UL
#define IMAGE_LIMIT	0x00200000UL	/* the 2 MB flash */
#define LENGTH_OFFSET	0x120UL
#define CRC_OFFSET	0x124UL
#define BOARD_OFFSET	0x128UL
#define BOARD_SIZE	16UL
#define PT_LOAD		1UL

static unsigned long
get16(const unsigned char *p)
{
	return p[0] | (unsigned long)p[1] << 8;
}

static unsigned long
get32(const unsigned char *p)
{
	return get16(p) | get16(p + 2) << 16;
}

static void
put32(unsigned char *p, unsigned long v)
{
	p[0] = v & 0xff;
	p[1] = v >> 8 & 0xff;
	p[2] = v >> 16 & 0xff;
	p[3] = v >> 24 & 0xff;
}

/* CRC-32 with the reflected polynomial 0xedb88320: zlib's crc32(). */
static unsigned long
crc32_zlib(const unsigned char *p, unsigned long n)
{
	unsigned long crc = 0xffffffffUL;
	int bit;

	while (n-- > 0) {
		crc ^= *p++;
		for (bit = 0; bit < 8; bit++)
			crc = crc >> 1 ^ ((crc & 1) ? 0xedb88320UL : 0);
	}
	return crc ^ 0xffffffffUL;
}

static void
refuse(const char *file, const char *why)
{
	fprintf(stderr, "kernsum: %s: %s\n", file, why);
	exit(2);
}

int
main(int argc, char **argv)
{
	unsigned char *elf, *image;
	unsigned long phoff, phentsize, phnum, i, length, board, crc, stored;
	long size, word_at[2];
	const char *file;
	int check;
	FILE *f;

	check = argc == 3 && strcmp(argv[1], "-c") == 0;
	if (argc != 2 && !check) {
		fprintf(stderr, "usage: kernsum [-c] unix\n");
		return 2;
	}
	file = argv[argc - 1];
	f = fopen(file, check ? "rb" : "r+b");
	if (f == NULL || fseek(f, 0L, SEEK_END) != 0 || (size = ftell(f)) < 52) {
		perror(file);
		return 2;
	}
	elf = malloc((size_t)size);
	image = calloc(1, IMAGE_LIMIT);
	if (elf == NULL || image == NULL)
		refuse(file, "out of memory");
	rewind(f);
	if (fread(elf, 1, (size_t)size, f) != (size_t)size)
		refuse(file, "short read");

	/* ELF32, little-endian, ARM. */
	if (memcmp(elf, "\177ELF\001\001", 6) != 0 || get16(elf + 18) != 40)
		refuse(file, "not a little-endian ARM ELF32 file");
	phoff = get32(elf + 28);
	phentsize = get16(elf + 42);
	phnum = get16(elf + 44);
	if (phentsize < 32 || phoff > (unsigned long)size ||
	    phnum * phentsize > (unsigned long)size - phoff)
		refuse(file, "program headers lie outside the file");

	length = 0;
	word_at[0] = word_at[1] = -1;
	for (i = 0; i < phnum; i++) {
		const unsigned char *ph = elf + phoff + i * phentsize;
		unsigned long offset = get32(ph + 4), paddr = get32(ph + 12);
		unsigned long filesz = get32(ph + 16), start;

		if (get32(ph) != PT_LOAD || filesz == 0)
			continue;
		if (offset > (unsigned long)size ||
		    filesz > (unsigned long)size - offset)
			refuse(file, "a segment lies outside the file");
		if (paddr < IMAGE_BASE || paddr - IMAGE_BASE > IMAGE_LIMIT ||
		    filesz > IMAGE_LIMIT - (paddr - IMAGE_BASE))
			refuse(file, "a segment loads outside the flash");
		start = paddr - IMAGE_BASE;
		memcpy(image + start, elf + offset, filesz);
		if (start + filesz > length)
			length = start + filesz;
		if (start <= LENGTH_OFFSET && CRC_OFFSET + 4 <= start + filesz) {
			word_at[0] = (long)(offset + LENGTH_OFFSET - start);
			word_at[1] = (long)(offset + CRC_OFFSET - start);
		}
	}
	if (word_at[0] < 0)
		refuse(file, "no segment holds the vector table");
	board = get32(image + BOARD_OFFSET);
	if (board < IMAGE_BASE + BOARD_OFFSET + 4 ||
	    board - IMAGE_BASE > length - BOARD_SIZE || (board & 255) != 0)
		refuse(file, "vector entry 10 does not address a board descriptor");
	board -= IMAGE_BASE;

	stored = get32(image + CRC_OFFSET);
	if (!check && (stored != 0 || get32(image + LENGTH_OFFSET) != 0)) {
		fprintf(stderr, "kernsum: %s: identity words are not zero\n", file);
		return 1;
	}
	if (check && get32(image + LENGTH_OFFSET) != length) {
		fprintf(stderr, "kernsum: %s: length word %lu, image %lu bytes\n",
		    file, get32(image + LENGTH_OFFSET), length);
		return 1;
	}
	put32(image + LENGTH_OFFSET, length);
	put32(image + CRC_OFFSET, 0);
	memset(image + board, 0, BOARD_SIZE);
	crc = crc32_zlib(image, length);

	if (check) {
		if (stored != crc) {
			fprintf(stderr, "kernsum: %s: CRC word %08lx, image %08lx\n",
			    file, stored, crc);
			return 1;
		}
		printf("%lu %08lx\n", length, crc);
		return 0;
	}
	put32(elf + word_at[0], length);
	put32(elf + word_at[1], crc);
	if (fseek(f, word_at[0], SEEK_SET) != 0 ||
	    fwrite(elf + word_at[0], 1, 4, f) != 4 ||
	    fseek(f, word_at[1], SEEK_SET) != 0 ||
	    fwrite(elf + word_at[1], 1, 4, f) != 4 || fclose(f) != 0) {
		perror(file);
		return 2;
	}
	return 0;
}
