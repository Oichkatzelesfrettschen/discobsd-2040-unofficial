/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _RP2040_BREAKGUARD_H_
#define _RP2040_BREAKGUARD_H_

/*
 * The break guard: MPU region 7 covers the 32 bytes at the process break
 * rounded up to 32 bytes, privileged read-write and unprivileged read-only,
 * so the first user store past the break and its pad raises HardFault while
 * every load, every kernel access and the bytes between the exact break and
 * the guard stay as they are. A 256-byte region divides into eight 32-byte
 * subregions (ARMv6-M ARM B3.5.9); the guard enables only the one holding
 * the rounded break. It is armed only while the stack, the lower of the
 * deepest stack recorded in p_saddr and the stack pointer the process
 * resumes with, lies at least BREAK_GUARD_BYTES + BREAK_GUARD_MARGIN bytes
 * above the rounded break, so no byte a process reaches with its stack is
 * ever covered. sys/arch/rp2040/doc/MPU.md owns the map and its evidence.
 *
 * The functions take and return plain unsigned integers so that the host
 * gate tests/rp2040/break_guard/check.py compiles this header unchanged;
 * mpu.c asserts each encoding below against machine/mpu.h.
 */
#define	BREAK_GUARD_REGION	7U
#define	BREAK_GUARD_BYTES	32U		/* One subregion. */
#define	BREAK_GUARD_MARGIN	1024U		/* Stack clearance to arm. */

#define	BREAK_GUARD_RBAR_VALID	0x00000010U	/* REGION selects the slot. */
#define	BREAK_GUARD_RBAR_ADDR	0xffffff00U	/* 256-byte region base. */
#define	BREAK_GUARD_RASR_ENABLE	0x00000001U
#define	BREAK_GUARD_RASR_SIZE	(7U << 1)	/* 2^(7+1) = 256 bytes. */
#define	BREAK_GUARD_RASR_SRD_SHIFT 8
#define	BREAK_GUARD_RASR_AP	(2U << 24)	/* Privileged rw, user r-. */
#define	BREAK_GUARD_RASR_XN	0x10000000U

/*
 * Return the lowest stack address that keeps the guard armed for a
 * process whose exact break is brk, or zero when the guard stays off. The
 * guard base is brk rounded up to BREAK_GUARD_BYTES and sits that many
 * bytes below the returned value minus BREAK_GUARD_MARGIN. A break so close
 * to 2^32 that the rounding wraps arms nothing.
 */
static inline unsigned int
rp2040_break_guard_floor(unsigned int brk, unsigned int saddr, unsigned int sp)
{
	unsigned int base = (brk + (BREAK_GUARD_BYTES - 1U)) &
	    ~(BREAK_GUARD_BYTES - 1U);
	unsigned int stack = saddr < sp ? saddr : sp;

	if (base < brk || stack < base ||
	    stack - base < BREAK_GUARD_BYTES + BREAK_GUARD_MARGIN)
		return 0;
	return base + BREAK_GUARD_BYTES + BREAK_GUARD_MARGIN;
}

/* The RBAR word, VALID set, that selects region 7 for a floor. */
static inline unsigned int
rp2040_break_guard_rbar(unsigned int floor)
{
	unsigned int base = floor - BREAK_GUARD_BYTES - BREAK_GUARD_MARGIN;

	if (floor == 0)
		base = 0;
	return (base & BREAK_GUARD_RBAR_ADDR) | BREAK_GUARD_RBAR_VALID |
	    BREAK_GUARD_REGION;
}

/* The RASR word for a floor: zero, a disabled region, when it is zero. */
static inline unsigned int
rp2040_break_guard_rasr(unsigned int floor)
{
	unsigned int base = floor - BREAK_GUARD_BYTES - BREAK_GUARD_MARGIN;
	unsigned int srd = 0xffU & ~(1U << ((base >> 5) & 7U));

	if (floor == 0)
		return 0;
	return BREAK_GUARD_RASR_XN | BREAK_GUARD_RASR_AP |
	    (srd << BREAK_GUARD_RASR_SRD_SHIFT) | BREAK_GUARD_RASR_SIZE |
	    BREAK_GUARD_RASR_ENABLE;
}

#endif	/* !_RP2040_BREAKGUARD_H_ */
