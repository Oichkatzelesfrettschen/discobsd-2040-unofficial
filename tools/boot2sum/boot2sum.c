/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Pad an RP2040 second-stage boot binary to 256 bytes with its CRC32
 * trailer and emit it as an assembler source placing the bytes in the
 * .boot2 section.
 *
 * The RP2040 boot ROM loads 256 bytes from flash offset 0 and accepts them
 * when the last four, read as a little-endian word, equal the CRC32 of the
 * first 252 with polynomial 0x04c11db7, initial value 0xffffffff, no input
 * or output reflection and no final XOR. RP2040 datasheet section 2.8.1.3.1
 * states those parameters, and pico-sdk's pad_checksum computes the same
 * value through a reflected library routine and two bit reversals.
 *
 * Usage: boot2sum input.bin output.S
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOT2_SIZE	256
#define BOOT2_PAYLOAD	(BOOT2_SIZE - 4)

static unsigned long
crc32_mpeg2(const unsigned char *p, size_t n)
{
	unsigned long crc = 0xffffffffUL;
	size_t i;
	int b;

	for (i = 0; i < n; i++) {
		crc ^= (unsigned long)p[i] << 24;
		for (b = 0; b < 8; b++) {
			if (crc & 0x80000000UL)
				crc = (crc << 1) ^ 0x04c11db7UL;
			else
				crc <<= 1;
			crc &= 0xffffffffUL;
		}
	}
	return crc;
}

int
main(int argc, char **argv)
{
	unsigned char image[BOOT2_SIZE];
	unsigned long crc;
	size_t n, i;
	FILE *in, *out;

	if (argc != 3) {
		fprintf(stderr, "usage: %s input.bin output.S\n", argv[0]);
		return 2;
	}
	in = fopen(argv[1], "rb");
	if (in == NULL) {
		perror(argv[1]);
		return 1;
	}
	memset(image, 0, sizeof(image));
	n = fread(image, 1, sizeof(image), in);
	if (ferror(in)) {
		perror(argv[1]);
		return 1;
	}
	fclose(in);
	if (n > BOOT2_PAYLOAD) {
		fprintf(stderr, "%s: %zu bytes, second stage holds %d\n",
		    argv[1], n, BOOT2_PAYLOAD);
		return 1;
	}

	crc = crc32_mpeg2(image, BOOT2_PAYLOAD);
	image[BOOT2_PAYLOAD + 0] = crc & 0xff;
	image[BOOT2_PAYLOAD + 1] = (crc >> 8) & 0xff;
	image[BOOT2_PAYLOAD + 2] = (crc >> 16) & 0xff;
	image[BOOT2_PAYLOAD + 3] = (crc >> 24) & 0xff;

	out = fopen(argv[2], "w");
	if (out == NULL) {
		perror(argv[2]);
		return 1;
	}
	fprintf(out, "/* Padded and checksummed second stage from %s. */\n\n",
	    argv[1]);
	fprintf(out, "\t.cpu\tcortex-m0plus\n\t.thumb\n\n");
	fprintf(out, "\t.section .boot2, \"ax\"\n\n");
	for (i = 0; i < BOOT2_SIZE; i += 16) {
		size_t j;

		fprintf(out, "\t.byte\t");
		for (j = 0; j < 16; j++)
			fprintf(out, "0x%02x%s", image[i + j],
			    j == 15 ? "\n" : ", ");
	}
	if (fclose(out) != 0) {
		perror(argv[2]);
		return 1;
	}
	return 0;
}
