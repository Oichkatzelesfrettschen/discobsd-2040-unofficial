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

#ifndef _RP2040_USER_CONTEXT_H_
#define _RP2040_USER_CONTEXT_H_

/*
 * Consumers include <sys/param.h>, <sys/systm.h>, and <machine/frame.h>
 * first. The RP2040 MPU grants unprivileged execution from the 16 KB boot ROM
 * and from the resident process window. The diagnostic shared loader adds
 * the linker-reserved provider interval. Each PC names a complete Thumb
 * halfword in one executable region. baduaddr_span() owns the
 * process-window upper-bound check.
 */
#define RP2040_USER_BOOT_ROM_END 0x00004000U

#ifdef EXEC_XIP_LIBC
extern const char __xip_libc_start[], __xip_libc_end[];
#endif

static inline int
machine_user_pc_valid(u_int program_counter)
{
	if (program_counter % INSN_SZ != 0)
		return 0;
	if (program_counter <= RP2040_USER_BOOT_ROM_END &&
	    INSN_SZ <= RP2040_USER_BOOT_ROM_END - program_counter)
		return 1;

#ifdef EXEC_XIP_LIBC
	if (program_counter >= (u_int)__xip_libc_start &&
	    program_counter < (u_int)__xip_libc_end &&
	    INSN_SZ <= (u_int)__xip_libc_end - program_counter)
		return 1;
#endif
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
