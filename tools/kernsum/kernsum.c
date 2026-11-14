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
 * to unix.bin. Two reserved words of the vector table at 0x10000100,
 * entries 12 and 13, which ARMv6-M never fetches, identify that image:
 *
 *   0x10000130  length of the image in bytes
 *   0x10000134  CRC32 of the image, as zlib's crc32() computes it, over
 *               the image with four spans read as zero (below)
 *
 * The word after the 48-entry vector table, 0x100001C0, holds the address
 * of the 16-byte board descriptor, set by the linker from __board_start
 * (locore0.S); the length word and that address word are inside the sum.
 *
 * The spans the sum reads as zero are the bytes another party rewrites
 * without changing the kernel that runs:
 *
 *   0x000-0x0FF  the second stage; the PicoCalc UF2 loader replaces it
 *                with its own
 *   0x110-0x12B  vector entries 4 to 10; the loader stamps a magic word,
 *                the end of its flash area and the start of the UF2 path
 *                there after it flashes an application
 *   0x134-0x137  the CRC word itself
 *   the board descriptor; distrib/rp2040/mkboarduf2.py rewrites it per
 *                board in a linked image, so one kernel build keeps one
 *                identity on every board it is issued for
 *
 * The kernel recomputes the sum over its own flash at boot
 * (image_identify() in board.c) and tools/check_rp2040_vectors.py
 * recomputes it from the ELF.
 *
 * Usage: kernsum unix       stamp the length and CRC words, both zero
 *        kernsum -c unix    check them; print the length and the CRC
 *
 * The exit status is 0 on success, 1 when the words are already stamped or
 * wrong, and 2 for a file that is not such a kernel: one whose loadable
 * segments overlap in flash or share file bytes is refused, because a word
 * written through one mapping would then change a byte the sum has already
 * covered through another.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_BASE	0x10000000UL
#define IMAGE_LIMIT	0x00200000UL	/* the 2 MB flash */
#define BOOT2_SIZE	0x100UL		/* the second stage, read as zero */
#define STAMP_OFFSET	0x110UL		/* the loader's stamp, read as zero */
#define STAMP_SIZE	0x1cUL		/* through 0x12b */
#define LENGTH_OFFSET	0x130UL
#define CRC_OFFSET	0x134UL
#define BOARD_WORD	0x1c0UL		/* address of the board descriptor */
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

/*
 * Place the PT_LOAD segments of elf into image and return the image length.
 * word_at receives the file offsets of the length and CRC words.
 */
static unsigned long
load(const char *file, const unsigned char *elf, unsigned long size,
    unsigned char *image, unsigned char *placed, long *word_at)
{
	unsigned long phoff, phentsize, phnum, i, j, length;

	/* ELF32, little-endian, ARM. */
	if (size < 52 || memcmp(elf, "\177ELF\001\001", 6) != 0 ||
	    get16(elf + 18) != 40)
		refuse(file, "not a little-endian ARM ELF32 file");
	phoff = get32(elf + 28);
	phentsize = get16(elf + 42);
	phnum = get16(elf + 44);
	if (phentsize < 32 || phoff > size || phnum * phentsize > size - phoff)
		refuse(file, "program headers lie outside the file");

	length = 0;
	word_at[0] = word_at[1] = -1;
	memset(image, 0, IMAGE_LIMIT);
	memset(placed, 0, IMAGE_LIMIT);
	for (i = 0; i < phnum; i++) {
		const unsigned char *ph = elf + phoff + i * phentsize;
		unsigned long offset = get32(ph + 4), paddr = get32(ph + 12);
		unsigned long filesz = get32(ph + 16), start;

		if (get32(ph) != PT_LOAD || filesz == 0)
			continue;
		if (offset > size || filesz > size - offset)
			refuse(file, "a segment lies outside the file");
		if (paddr < IMAGE_BASE || paddr - IMAGE_BASE > IMAGE_LIMIT ||
		    filesz > IMAGE_LIMIT - (paddr - IMAGE_BASE))
			refuse(file, "a segment loads outside the flash");
		start = paddr - IMAGE_BASE;
		if (memchr(placed + start, 1, filesz) != NULL)
			refuse(file, "two segments load to the same flash bytes");
		for (j = 0; j < i; j++) {
			const unsigned char *other = elf + phoff + j * phentsize;
			unsigned long other_offset = get32(other + 4);
			unsigned long other_filesz = get32(other + 16);

			if (get32(other) == PT_LOAD && other_filesz != 0 &&
			    offset < other_offset + other_filesz &&
			    other_offset < offset + filesz)
				refuse(file, "two segments share file bytes");
		}
		memset(placed + start, 1, filesz);
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
	return length;
}

/*
 * Return the sum of an image of the given length, whose length word must
 * hold that length; the second stage, the loader's stamp, the CRC word and
 * the board descriptor read as zero.
 */
static unsigned long
image_sum(const char *file, unsigned char *image, unsigned long length)
{
	unsigned long board = get32(image + BOARD_WORD);

	if (length < BOARD_WORD + 4 + BOARD_SIZE ||
	    board < IMAGE_BASE + BOARD_WORD + 4 ||
	    board - IMAGE_BASE > length - BOARD_SIZE || (board & 255) != 0)
		refuse(file, "the word after the vector table does not address a board descriptor");
	memset(image, 0, BOOT2_SIZE);
	memset(image + STAMP_OFFSET, 0, STAMP_SIZE);
	put32(image + CRC_OFFSET, 0);
	memset(image + (board - IMAGE_BASE), 0, BOARD_SIZE);
	return crc32_zlib(image, length);
}

int
main(int argc, char **argv)
{
	unsigned char *elf, *image, *placed;
	unsigned long length, crc, stored, stored_length;
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
	if (f == NULL || fseek(f, 0L, SEEK_END) != 0 || (size = ftell(f)) < 0) {
		perror(file);
		return 2;
	}
	elf = malloc((size_t)size + 1);
	image = malloc(IMAGE_LIMIT);
	placed = malloc(IMAGE_LIMIT);
	if (elf == NULL || image == NULL || placed == NULL)
		refuse(file, "out of memory");
	rewind(f);
	if (fread(elf, 1, (size_t)size, f) != (size_t)size)
		refuse(file, "short read");

	length = load(file, elf, (unsigned long)size, image, placed, word_at);
	stored_length = get32(image + LENGTH_OFFSET);
	stored = get32(image + CRC_OFFSET);
	if (check) {
		if (stored_length != length) {
			fprintf(stderr, "kernsum: %s: length word %lu, image %lu bytes\n",
			    file, stored_length, length);
			return 1;
		}
		crc = image_sum(file, image, length);
		if (stored != crc) {
			fprintf(stderr, "kernsum: %s: CRC word %08lx, image %08lx\n",
			    file, stored, crc);
			return 1;
		}
		printf("%lu %08lx\n", length, crc);
		return 0;
	}
	if (stored != 0 || stored_length != 0) {
		fprintf(stderr, "kernsum: %s: identity words are not zero\n", file);
		return 1;
	}
	put32(image + LENGTH_OFFSET, length);
	crc = image_sum(file, image, length);

	/* Stamp the file image, then require a fresh load of it to check. */
	put32(elf + word_at[0], length);
	put32(elf + word_at[1], crc);
	if (load(file, elf, (unsigned long)size, image, placed, word_at) != length ||
	    get32(image + LENGTH_OFFSET) != length ||
	    get32(image + CRC_OFFSET) != crc ||
	    image_sum(file, image, length) != crc)
		refuse(file, "the stamped image does not sum to its CRC word");
	if (fseek(f, word_at[0], SEEK_SET) != 0 ||
	    fwrite(elf + word_at[0], 1, 4, f) != 4 ||
	    fseek(f, word_at[1], SEEK_SET) != 0 ||
	    fwrite(elf + word_at[1], 1, 4, f) != 4 || fclose(f) != 0) {
		perror(file);
		return 2;
	}
	return 0;
}
