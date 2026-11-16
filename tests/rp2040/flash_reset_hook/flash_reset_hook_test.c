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
 * Host oracle for the flash reset hook (sys/arch/rp2040/dev/
 * flash_reset_hook.h): the production header runs over a model of the boot
 * ROM's erase and program entry points and of the 2 MiB NOR part behind
 * them.
 *
 * The model issues chip commands the way the ROM's loops do: a program
 * request becomes one command per 256-byte page, an erase request one
 * command per 4096-byte sector, or one 65536-byte block command wherever
 * the remaining range covers an aligned whole block. Each command is
 * logged. The expectations:
 *
 *   unarmed    one ROM call per request, as flash_program() makes it without the hook
 *   split      armed with a count the request never reaches, one ROM call
 *              per chip command, and the command log, the bytes and the
 *              erase arguments equal the unarmed run's
 *   fire       the reset routine runs once, after exactly the Nth command
 *              of the armed kind, with that command's offset, and the
 *              chip holds the commands up to and including the Nth only
 *   kind       a request of the other kind neither counts nor fires
 *   rearm      arming restarts the count; 0 disarms
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

/* The kernel places these in RAM; the host has no such section. */
#undef	__ramfunc
#define	__ramfunc

#include <rp2040/dev/flash_reset_hook.h>

#define	CHIP_BYTES	FLASH_TOTAL_BYTES
#define	MAX_COMMANDS	4096

struct command {
	u_int	erase;		/* 0 program, 1 sector erase, 2 block erase */
	u_int	offset;
	u_int	len;
};

static u_char nor[CHIP_BYTES];
static struct command log_[MAX_COMMANDS];
static u_int log_count;
static u_int rom_calls;
static u_int rom_bad_args;
static u_int model_faults;
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
chip_reset(void)
{
	memset(nor, 0xff, sizeof nor);
	log_count = 0;
	rom_calls = 0;
	rom_bad_args = 0;
	model_faults = 0;
}

static void
note(u_int kind, u_int offset, u_int len)
{
	if (log_count == MAX_COMMANDS)
		return;
	log_[log_count].erase = kind;
	log_[log_count].offset = offset;
	log_[log_count].len = len;
	log_count++;
}

/* The ROM's program entry: one chip command per FLASH_PROG_BYTES page. */
static void
rom_program(u_int addr, const u_char *data, u_int count)
{
	u_int i, page;

	rom_calls++;
	if (addr % FLASH_PROG_BYTES != 0 || count % FLASH_PROG_BYTES != 0 ||
	    addr > CHIP_BYTES || count > CHIP_BYTES - addr) {
		model_faults++;
		return;
	}
	for (page = 0; page < count; page += FLASH_PROG_BYTES) {
		note(0, addr + page, FLASH_PROG_BYTES);
		for (i = 0; i < FLASH_PROG_BYTES; i++) {
			if (nor[addr + page + i] != 0xff)
				model_faults++;
			nor[addr + page + i] &= data[page + i];
		}
	}
}

/*
 * The ROM's erase entry: the block command where the range holds an aligned
 * whole block, the sector command everywhere else.
 */
static void
rom_erase(u_int addr, u_int count, u_int blk, u_char cmd)
{
	u_int n;

	rom_calls++;
	if (blk != FLASH_BLOCK_BYTES || cmd != FLASH_BLOCK_ERASE_CMD)
		rom_bad_args++;
	if (addr % FLASH_SECTOR_BYTES != 0 || count % FLASH_SECTOR_BYTES != 0 ||
	    addr > CHIP_BYTES || count > CHIP_BYTES - addr) {
		model_faults++;
		return;
	}
	while (count != 0) {
		if (count >= blk && addr % blk == 0) {
			n = blk;
			note(2, addr, n);
		} else {
			n = FLASH_SECTOR_BYTES;
			note(1, addr, n);
		}
		memset(nor + addr, 0xff, n);
		addr += n;
		count -= n;
	}
}

static u_int fires;
static u_int fire_erase;
static u_int fire_offset;
static u_int fire_commands_at;

static void
fire(u_int erase, u_int offset)
{
	fires++;
	fire_erase = erase;
	fire_offset = offset;
	fire_commands_at = log_count;
}

static void
fire_reset(void)
{
	fires = 0;
	fire_erase = 0;
	fire_offset = 0;
	fire_commands_at = 0;
}

static int
program_request(struct flash_reset_hook *hook, u_int offset, u_char fill,
    u_int len)
{
	static u_char data[64 * 1024];

	memset(data, fill, sizeof data);
	return flash_hook_program(hook, rom_program, fire, offset, data, len);
}

static int
erase_request(struct flash_reset_hook *hook, u_int offset, u_int len)
{
	return flash_hook_erase(hook, rom_erase, fire, offset, len);
}

#define	BASE	FLASH_FS_OFFSET		/* 128 KiB: 64 KiB block aligned */

/* The request mix both the unarmed and the split runs execute. */
static void
mixed_requests(struct flash_reset_hook *hook)
{
	/* A Dhara block: two sectors. */
	erase_request(hook, BASE, 8192);
	/* A checkpoint page: one 1 KiB request, four chip pages. */
	program_request(hook, BASE, 0x5a, 1024);
	program_request(hook, BASE + 1024, 0xa5, 1024);
	/* A swap-style range: sectors, then a whole aligned block, then more
	 * sectors, then a second block. */
	erase_request(hook, BASE + 0x10000 - 0x2000, 0x2000 + 0x10000 + 0x3000);
	erase_request(hook, BASE + 0x40000, 0x20000);
	/* Sectors from an unaligned start, then a whole aligned block. */
	erase_request(hook, BASE + 0x80000 + 0x1000, 0x1f000);
	program_request(hook, BASE + 0x10000, 0x3c, 256);
}

static void
test_split(void)
{
	static struct command ref[MAX_COMMANDS];
	static u_char ref_nor[CHIP_BYTES];
	struct flash_reset_hook hook;
	u_int ref_count, ref_calls;

	/* Unarmed: the reference run. One ROM call per request. */
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	mixed_requests(&hook);
	EXPECT("unarmed", rom_calls == 7,
	    "unarmed requests did not make one ROM call each");
	EXPECT("unarmed", rom_bad_args == 0 && model_faults == 0,
	    "unarmed ROM call carried other arguments");
	EXPECT("unarmed", fires == 0, "unarmed hook fired");
	EXPECT("unarmed", hook.program.count == 0 && hook.erase.count == 0,
	    "unarmed hook counted");
	ref_count = log_count;
	ref_calls = rom_calls;
	memcpy(ref, log_, sizeof(struct command) * ref_count);
	memcpy(ref_nor, nor, sizeof nor);
	EXPECT("unarmed", ref_count > 7, "model produced no per-command log");

	/* Armed beyond what the requests issue: the split run. */
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.program, 1000000);
	flash_hook_arm(&hook.erase, 1000000);
	mixed_requests(&hook);
	EXPECT("split", fires == 0, "split run fired");
	EXPECT("split", log_count == ref_count &&
	    memcmp(log_, ref, sizeof(struct command) * ref_count) == 0,
	    "split chip commands differ from the unarmed run's");
	EXPECT("split", memcmp(nor, ref_nor, sizeof nor) == 0,
	    "split chip bytes differ from the unarmed run's");
	EXPECT("split", rom_calls == ref_count,
	    "split run did not make one ROM call per chip command");
	EXPECT("split", rom_calls > ref_calls, "split run made no extra calls");
	EXPECT("split", rom_bad_args == 0 && model_faults == 0,
	    "split ROM call carried other arguments");
	EXPECT("split", hook.program.count + hook.erase.count == ref_count,
	    "split counts do not equal the chip commands");
}

static void
test_fire_program(void)
{
	struct flash_reset_hook hook;
	u_int i, rc;

	/* Four chip pages per checkpoint request; reset after the third. */
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.program, 3);
	rc = (u_int)program_request(&hook, BASE, 0x11, 1024);
	EXPECT("fire", rc == 1, "request did not report the reset");
	EXPECT("fire", fires == 1, "reset routine did not run exactly once");
	EXPECT("fire", fire_erase == 0, "reset named an erase");
	EXPECT("fire", fire_offset == BASE + 2 * FLASH_PROG_BYTES,
	    "reset named another command's offset");
	EXPECT("fire", fire_commands_at == 3,
	    "reset ran before the third command was issued or after the fourth");
	EXPECT("fire", log_count == 3, "chip saw other than three commands");
	EXPECT("fire", rom_calls == 3, "ROM calls differ from three");
	for (i = 0; i < 3 * FLASH_PROG_BYTES; i++)
		if (nor[BASE + i] != 0x11)
			break;
	EXPECT("fire", i == 3 * FLASH_PROG_BYTES,
	    "the first three pages are not programmed");
	for (; i < 4 * FLASH_PROG_BYTES; i++)
		if (nor[BASE + i] != 0xff)
			break;
	EXPECT("fire", i == 4 * FLASH_PROG_BYTES,
	    "the fourth page is not erased");
	EXPECT("fire", hook.program.armed == 0, "fired hook stayed armed");

	/* The Nth command is the first, and the last of a request. */
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.program, 1);
	program_request(&hook, BASE, 0x22, 1024);
	EXPECT("fire-first", fires == 1 && fire_commands_at == 1 &&
	    log_count == 1, "N = 1 did not reset after the first command");
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.program, 4);
	program_request(&hook, BASE, 0x22, 1024);
	EXPECT("fire-last", fires == 1 && fire_commands_at == 4 &&
	    fire_offset == BASE + 3 * FLASH_PROG_BYTES && log_count == 4,
	    "N = 4 did not reset after the request's last command");

	/* The count carries across requests: 4 + 2. */
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.program, 6);
	rc = (u_int)program_request(&hook, BASE, 0x33, 1024);
	EXPECT("fire-across", rc == 0 && fires == 0 &&
	    hook.program.count == 4, "first request counted wrongly");
	rc = (u_int)program_request(&hook, BASE + 1024, 0x33, 1024);
	EXPECT("fire-across", rc == 1 && fires == 1 &&
	    fire_offset == BASE + 1024 + FLASH_PROG_BYTES &&
	    log_count == 6, "second request did not reset after its second page");
}

static void
test_fire_erase(void)
{
	struct flash_reset_hook hook;
	u_int rc;

	/* An 8 KiB Dhara block is two sector commands; reset after the 3rd
	 * command overall, which is the first sector of the second request. */
	chip_reset();
	memset(nor, 0x00, sizeof nor);
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.erase, 3);
	rc = (u_int)erase_request(&hook, BASE, 8192);
	EXPECT("fire-erase", rc == 0 && fires == 0 && hook.erase.count == 2,
	    "first request counted wrongly");
	rc = (u_int)erase_request(&hook, BASE + 8192, 8192);
	EXPECT("fire-erase", rc == 1 && fires == 1 && fire_erase == 1 &&
	    fire_offset == BASE + 8192 && fire_commands_at == 3 &&
	    log_count == 3, "reset did not follow the third erase command");
	EXPECT("fire-erase", nor[BASE + 8192] == 0xff &&
	    nor[BASE + 8192 + FLASH_SECTOR_BYTES] == 0x00,
	    "the second sector of the second request was erased");

	/* A whole block is one command. */
	chip_reset();
	memset(nor, 0x00, sizeof nor);
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.erase, 1);
	erase_request(&hook, BASE, 0x20000);
	EXPECT("fire-block", fires == 1 && log_count == 1 &&
	    log_[0].erase == 2 && log_[0].len == FLASH_BLOCK_BYTES &&
	    nor[BASE + FLASH_BLOCK_BYTES] == 0x00,
	    "a whole block was not one command");
}

static void
test_kind(void)
{
	struct flash_reset_hook hook;

	/* Program armed: erase requests neither count nor fire. */
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.program, 2);
	erase_request(&hook, BASE, 0x20000);
	erase_request(&hook, BASE + 0x40000, 0x6000);
	EXPECT("kind", fires == 0 && hook.program.count == 0 &&
	    hook.erase.count == 0, "an erase advanced or fired a program arm");
	EXPECT("kind", rom_calls == 2, "erase of an unarmed kind was split");

	/* Erase armed: program requests neither count nor fire. */
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.erase, 2);
	program_request(&hook, BASE, 0x44, 4096);
	EXPECT("kind", fires == 0 && hook.program.count == 0 &&
	    hook.erase.count == 0, "a program advanced or fired an erase arm");
	EXPECT("kind", rom_calls == 1, "program of an unarmed kind was split");

	/* Both armed: each fires on its own count only. */
	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.program, 2);
	flash_hook_arm(&hook.erase, 1000);
	erase_request(&hook, BASE, 8 * FLASH_SECTOR_BYTES);
	EXPECT("kind", fires == 0 && hook.erase.count == 8 &&
	    hook.program.count == 0,
	    "eight erase commands reached the program count");
	program_request(&hook, BASE, 0x55, 1024);
	EXPECT("kind", fires == 1 && fire_erase == 0 && hook.erase.count == 8,
	    "program did not fire on its own count");
}

static void
test_rearm(void)
{
	struct flash_reset_hook hook;

	chip_reset();
	memset(&hook, 0, sizeof hook);
	fire_reset();
	flash_hook_arm(&hook.program, 100);
	program_request(&hook, BASE, 0x66, 768);
	EXPECT("rearm", hook.program.count == 3 && fires == 0,
	    "three commands counted wrongly");
	/* Arming again restarts the count: the reset follows the second
	 * command from here, not the fifth overall. */
	flash_hook_arm(&hook.program, 2);
	EXPECT("rearm", hook.program.count == 0, "arming kept the count");
	program_request(&hook, BASE + 1024, 0x66, 1024);
	EXPECT("rearm", fires == 1 && log_count == 5 &&
	    fire_offset == BASE + 1024 + FLASH_PROG_BYTES,
	    "reset did not follow the second command after arming");

	/* 0 disarms and the request runs whole. */
	fire_reset();
	flash_hook_arm(&hook.program, 3);
	program_request(&hook, BASE + 2048, 0x77, 256);
	flash_hook_arm(&hook.program, 0);
	EXPECT("rearm", hook.program.count == 0 && hook.program.armed == 0,
	    "disarming kept state");
	log_count = 0;
	rom_calls = 0;
	program_request(&hook, BASE + 3072, 0x77, 1024);
	EXPECT("rearm", fires == 0 && rom_calls == 1 && log_count == 4,
	    "a disarmed hook split or fired");
}

int
main(void)
{
	test_split();
	test_fire_program();
	test_fire_erase();
	test_kind();
	test_rearm();
	if (failures != 0) {
		printf("flash_reset_hook: %d expectation(s) failed\n", failures);
		return 1;
	}
	printf("flash_reset_hook: all expectations met\n");
	return 0;
}
