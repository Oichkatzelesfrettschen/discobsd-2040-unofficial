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
 * Diagnostic reset at the Nth chip command, compiled into flash.c only for
 * STORAGE_STATS kernels.
 *
 * flash_program() and flash_erase() hand the boot ROM a whole request with
 * interrupts masked, so a reset asked for over USB lands before the request
 * or after it and never between the chip commands inside it. A Dhara
 * checkpoint page is one 1 KiB flash_program() request, four 256-byte chip
 * program commands, and a reset between two of them leaves a page whose
 * first part is written and whose rest is erased. This hook issues an armed
 * request to the ROM one chip command at a time, counts the commands of the
 * armed kind, and calls a reset routine after the Nth one has completed.
 *
 * A chip command is the unit the ROM's own loop issues: one 256-byte program
 * page, one 4096-byte sector erase, or one 65536-byte block erase where the
 * range holds an aligned whole block (the ROM erases a block with the block
 * command when the count covers one and the address is block aligned, and
 * with the sector command otherwise). The ROM calls keep the block size and
 * block command the unarmed request passes, so each command is the one the
 * unarmed request would have issued.
 *
 * Both entry points run from RAM while execute-in-place is down, so they
 * carry __ramfunc and call only through pointers into the ROM and the
 * RAM-resident reset routine. The host test (tests/rp2040/flash_reset_hook)
 * includes this header over a NOR model, with __ramfunc empty.
 *
 * The counters are written under splhigh(): the sysctl writes them with
 * interrupts masked, and the entry points run with them masked.
 */
#ifndef _RP2040_DEV_FLASH_RESET_HOOK_H_
#define	_RP2040_DEV_FLASH_RESET_HOOK_H_

#include <rp2040/dev/flash.h>

/*
 * armed is the command number the reset follows (0 for disarmed) and count the
 * commands of this kind issued since arming.
 */
struct flash_hook_kind {
	u_int	armed;
	u_int	count;
};

struct flash_reset_hook {
	struct flash_hook_kind	program;
	struct flash_hook_kind	erase;
};

typedef void (*flash_hook_program_fn)(u_int addr, const u_char *data,
    u_int count);
typedef void (*flash_hook_erase_fn)(u_int addr, u_int count, u_int blk,
    u_char cmd);

/*
 * Called after the command that reached the armed count has completed:
 * erase is nonzero for an erase command, offset is that command's flash
 * offset. The kernel's routine requests the reset and does not return.
 */
typedef void (*flash_hook_fire_fn)(u_int erase, u_int offset);

/* Arm kind to fire after its nth command, or disarm it with 0. */
static inline void
flash_hook_arm(struct flash_hook_kind *kind, u_int n)
{
	kind->armed = n;
	kind->count = 0;
}

/*
 * Program len bytes at offset through rom(). Unarmed, one call carries the
 * whole request. Armed, one call per FLASH_PROG_BYTES chunk, each followed
 * by the count; the call that reaches the armed count disarms the kind,
 * calls fire() and ends the request. Returns 1 when fire() was called.
 */
static __ramfunc int
flash_hook_program(struct flash_reset_hook *hook, flash_hook_program_fn rom,
    flash_hook_fire_fn fire, u_int offset, const u_char *data, u_int len)
{
	u_int n;

	if (hook->program.armed == 0) {
		rom(offset, data, len);
		return 0;
	}
	while (len != 0) {
		n = len < FLASH_PROG_BYTES ? len : FLASH_PROG_BYTES;
		rom(offset, data, n);
		hook->program.count++;
		if (hook->program.count == hook->program.armed) {
			hook->program.armed = 0;
			fire(0, offset);
			return 1;
		}
		offset += n;
		data += n;
		len -= n;
	}
	return 0;
}

/*
 * Erase len bytes at offset through rom(), with the block size and block
 * command flash_erase() passes. Armed, the chunks follow the ROM's own
 * decomposition: a whole aligned block takes one command and any other
 * range proceeds in sectors.
 */
static __ramfunc int
flash_hook_erase(struct flash_reset_hook *hook, flash_hook_erase_fn rom,
    flash_hook_fire_fn fire, u_int offset, u_int len)
{
	u_int n;

	if (hook->erase.armed == 0) {
		rom(offset, len, FLASH_BLOCK_BYTES, FLASH_BLOCK_ERASE_CMD);
		return 0;
	}
	while (len != 0) {
		n = len >= FLASH_BLOCK_BYTES &&
		    (offset & (FLASH_BLOCK_BYTES - 1)) == 0 ?
		    FLASH_BLOCK_BYTES : FLASH_SECTOR_BYTES;
		if (n > len)
			n = len;
		rom(offset, n, FLASH_BLOCK_BYTES, FLASH_BLOCK_ERASE_CMD);
		hook->erase.count++;
		if (hook->erase.count == hook->erase.armed) {
			hook->erase.armed = 0;
			fire(1, offset);
			return 1;
		}
		offset += n;
		len -= n;
	}
	return 0;
}

#endif	/* !_RP2040_DEV_FLASH_RESET_HOOK_H_ */
