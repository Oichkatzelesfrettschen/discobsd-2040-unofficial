/* Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Host oracle for the swap-sector erase counters in
 * sys/arch/rp2040/dev/flash_wear.h, the code flash_erase() runs on a
 * STORAGE_STATS kernel. flash_wear_note() is held to an integer model of the
 * same rules:
 *
 *   sizing     one counter per FLASH_SECTOR_BYTES of FLASH_SWAP_BYTES
 *   sector     a one-sector erase counts that sector alone
 *   block      a 64 KiB block erase counts each of its sixteen sectors once
 *   root       erases below FLASH_SWAP_OFFSET count nothing
 *   edges      a request crossing the swap start or the last counter counts
 *              the part inside and writes nothing outside the array
 *   saturate   a counter at 65535 stays there and its neighbors still count
 *   model      a pseudo-random request stream leaves every counter equal to
 *              the model's, with saturation reached
 *
 * Every scenario prints "FAIL <scenario>: <what>" for an unmet expectation;
 * the program exits 1 when any failed.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char	u_char;
typedef unsigned short	u_short;
typedef unsigned int	u_int;

#include <rp2040/dev/flash.h>
#include <rp2040/dev/flash_wear.h>

#define	GUARD	16	/* A block erase covers 16 sectors. */
#define	GUARD_FILL	0xa5a5

/* The counters sit between guard halfwords that catch a stray store. */
static struct {
	u_short	before[GUARD];
	u_short	wear[FLASH_SWAP_SECTORS];
	u_short	after[GUARD];
} arena;

static int failures;

static void
fail(const char *scenario, const char *what)
{
	printf("FAIL %s: %s\n", scenario, what);
	failures++;
}

#define	EXPECT(scenario, condition, what) \
	do { \
		if (!(condition)) \
			fail(scenario, what); \
	} while (0)

static void
arena_reset(void)
{
	u_int i;

	for (i = 0; i < GUARD; i++)
		arena.before[i] = arena.after[i] = GUARD_FILL;
	memset(arena.wear, 0, sizeof arena.wear);
}

static int
guards_intact(void)
{
	u_int i;

	for (i = 0; i < GUARD; i++)
		if (arena.before[i] != GUARD_FILL || arena.after[i] != GUARD_FILL)
			return 0;
	return 1;
}

static void
note(u_int offset, u_int len)
{
	flash_wear_note(arena.wear, FLASH_SWAP_SECTORS, FLASH_SWAP_OFFSET,
	    offset, len);
}

/* Counters other than those in [lo, hi) all read zero. */
static int
zero_outside(u_int lo, u_int hi)
{
	u_int i;

	for (i = 0; i < FLASH_SWAP_SECTORS; i++)
		if ((i < lo || i >= hi) && arena.wear[i] != 0)
			return 0;
	return 1;
}

static void
scenario_sizing(void)
{
	EXPECT("sizing", sizeof arena.wear / sizeof arena.wear[0] ==
	    FLASH_SWAP_BYTES / FLASH_SECTOR_BYTES, "one counter per sector");
	EXPECT("sizing", FLASH_SWAP_SECTORS * FLASH_SECTOR_BYTES ==
	    FLASH_SWAP_BYTES, "sectors tile the region");
}

static void
scenario_sector(void)
{
	arena_reset();
	note(FLASH_SWAP_OFFSET, FLASH_SECTOR_BYTES);
	note(FLASH_SWAP_OFFSET + 5 * FLASH_SECTOR_BYTES, FLASH_SECTOR_BYTES);
	note(FLASH_SWAP_OFFSET + 5 * FLASH_SECTOR_BYTES, FLASH_SECTOR_BYTES);
	EXPECT("sector", arena.wear[0] == 1, "first sector counts once");
	EXPECT("sector", arena.wear[5] == 2, "sector 5 counts twice");
	EXPECT("sector", arena.wear[1] == 0 && arena.wear[4] == 0 &&
	    arena.wear[6] == 0, "neighbors stay zero");
	EXPECT("sector", guards_intact(), "guards intact");
}

static void
scenario_block(void)
{
	u_int i;

	arena_reset();
	note(FLASH_SWAP_OFFSET + FLASH_BLOCK_BYTES, FLASH_BLOCK_BYTES);
	for (i = 16; i < 32; i++)
		EXPECT("block", arena.wear[i] == 1, "each sector of the block");
	EXPECT("block", zero_outside(16, 32), "no sector outside the block");
}

static void
scenario_root(void)
{
	arena_reset();
	note(0, FLASH_SECTOR_BYTES);
	note(FLASH_SWAP_OFFSET - FLASH_SECTOR_BYTES, FLASH_SECTOR_BYTES);
	note(FLASH_SWAP_OFFSET - FLASH_BLOCK_BYTES, FLASH_BLOCK_BYTES);
	EXPECT("root", zero_outside(0, 0), "root erases count nothing");
	EXPECT("root", guards_intact(), "guards intact");
}

static void
scenario_edges(void)
{
	/* Two sectors, one below the swap start and one at it. */
	arena_reset();
	note(FLASH_SWAP_OFFSET - FLASH_SECTOR_BYTES, 2 * FLASH_SECTOR_BYTES);
	EXPECT("edges", arena.wear[0] == 1 && zero_outside(0, 1),
	    "a request across the swap start counts its swap part");
	/* Two sectors at the last counter: only the last is inside. */
	arena_reset();
	note(FLASH_SWAP_OFFSET + (FLASH_SWAP_SECTORS - 1) * FLASH_SECTOR_BYTES,
	    2 * FLASH_SECTOR_BYTES);
	EXPECT("edges", arena.wear[FLASH_SWAP_SECTORS - 1] == 1 &&
	    zero_outside(FLASH_SWAP_SECTORS - 1, FLASH_SWAP_SECTORS),
	    "a request past the last counter counts its inside part");
	EXPECT("edges", guards_intact(), "no store outside the array");
	/* A block starting at the last sector writes nothing outside. */
	arena_reset();
	note(FLASH_SWAP_OFFSET + 90 * FLASH_SECTOR_BYTES, FLASH_BLOCK_BYTES);
	EXPECT("edges", guards_intact(), "block past the end leaves guards");
	EXPECT("edges", arena.wear[95] == 1 && arena.wear[90] == 1,
	    "block clipped to the array");
	note(FLASH_SWAP_OFFSET, 0);
	EXPECT("edges", arena.wear[0] == 0, "an empty request counts nothing");
}

static void
scenario_saturate(void)
{
	u_int i;

	arena_reset();
	arena.wear[3] = 65534;
	note(FLASH_SWAP_OFFSET + 2 * FLASH_SECTOR_BYTES,
	    3 * FLASH_SECTOR_BYTES);
	EXPECT("saturate", arena.wear[3] == 65535, "65534 reaches 65535");
	EXPECT("saturate", arena.wear[2] == 1 && arena.wear[4] == 1,
	    "neighbors count");
	note(FLASH_SWAP_OFFSET + 3 * FLASH_SECTOR_BYTES, FLASH_SECTOR_BYTES);
	EXPECT("saturate", arena.wear[3] == 65535, "65535 does not wrap to 0");
	for (i = 0; i < 70000; i++)
		note(FLASH_SWAP_OFFSET + 3 * FLASH_SECTOR_BYTES,
		    FLASH_SECTOR_BYTES);
	EXPECT("saturate", arena.wear[3] == 65535,
	    "70000 further erases leave 65535");
}

static u_int
lcg(u_int *state)
{
	*state = *state * 1664525U + 1013904223U;
	return *state >> 8;
}

static void
scenario_model(void)
{
	static unsigned long model[FLASH_SWAP_SECTORS];
	u_int state = 12345, i, k, offset, len, sector, saturated = 0;
	long first, last;

	arena_reset();
	memset(model, 0, sizeof model);
	for (i = 0; i < 400000; i++) {
		/* Mostly one hot sector, so the counters can saturate. */
		if (lcg(&state) % 4 == 0) {
			sector = lcg(&state) % (FLASH_SWAP_SECTORS + 16);
			len = (1 + lcg(&state) % 16) * FLASH_SECTOR_BYTES;
			offset = FLASH_SWAP_OFFSET - 16 * FLASH_SECTOR_BYTES +
			    sector * FLASH_SECTOR_BYTES;
		} else {
			offset = FLASH_SWAP_OFFSET + 7 * FLASH_SECTOR_BYTES;
			len = FLASH_SECTOR_BYTES;
		}
		note(offset, len);
		first = ((long)offset - (long)FLASH_SWAP_OFFSET) /
		    (long)FLASH_SECTOR_BYTES;
		last = first + (long)(len / FLASH_SECTOR_BYTES);
		for (k = 0; k < FLASH_SWAP_SECTORS; k++)
			if ((long)k >= first && (long)k < last &&
			    model[k] < FLASH_WEAR_MAX)
				model[k]++;
	}
	for (k = 0; k < FLASH_SWAP_SECTORS; k++) {
		if (arena.wear[k] != model[k]) {
			fail("model", "a counter differs from the model");
			break;
		}
		if (model[k] == FLASH_WEAR_MAX)
			saturated++;
	}
	EXPECT("model", saturated > 0, "the stream reaches saturation");
	EXPECT("model", guards_intact(), "guards intact");
}

int
main(void)
{
	scenario_sizing();
	scenario_sector();
	scenario_block();
	scenario_root();
	scenario_edges();
	scenario_saturate();
	scenario_model();
	if (failures != 0) {
		printf("flash_wear: %d expectation(s) failed\n", failures);
		return 1;
	}
	printf("flash_wear: all expectations met\n");
	return 0;
}
