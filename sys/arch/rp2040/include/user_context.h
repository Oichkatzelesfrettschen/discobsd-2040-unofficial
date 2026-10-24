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

#ifndef _RP2040_USER_CONTEXT_H_
#define _RP2040_USER_CONTEXT_H_

/*
 * Consumers include <sys/param.h>, <sys/systm.h>, and <machine/frame.h>
 * first. The RP2040 MPU grants unprivileged execution from the 16 KB boot ROM
 * and from the resident process window. A candidate PC therefore names a
 * complete Thumb halfword in either region. baduaddr_span() owns the
 * process-specific upper bound, including a conditional USER_TOP() extension.
 */
#define RP2040_USER_BOOT_ROM_END 0x00004000U

static inline int
machine_user_pc_valid(u_int program_counter)
{
	if (program_counter % INSN_SZ != 0)
		return 0;
	if (program_counter <= RP2040_USER_BOOT_ROM_END &&
	    INSN_SZ <= RP2040_USER_BOOT_ROM_END - program_counter)
		return 1;

	return !baduaddr_span((caddr_t)(u_long)program_counter, INSN_SZ);
}

static inline int
machine_user_stack_valid(u_int stack_pointer, u_int frame_size)
{
	if (stack_pointer % 8U != 0)
		return 0;

	return !baduaddr_span((caddr_t)(u_long)stack_pointer, frame_size);
}

#endif /* !_RP2040_USER_CONTEXT_H_ */
