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

#ifndef	_MACHINE_MPU_H_
#define	_MACHINE_MPU_H_

/*
 * The Cortex-M0+ memory protection unit, RP2040 datasheet 2.4.6 and the
 * ARMv6-M Architecture Reference Manual B3.5 (PMSAv6). Eight unified
 * regions, each a power of two of 256 bytes or more at a base aligned to
 * its size, each with eight subregion disables; a higher-numbered region
 * wins where two overlap, and a permission violation or an access that no
 * enabled region admits raises HardFault. MPU_CTRL.PRIVDEFENA supplies the
 * default memory map to privileged accesses as a background region, so a
 * map that names only what unprivileged code may touch leaves the kernel's
 * view of memory unchanged.
 *
 * The five registers follow the System Control Block at 0xed90; scb.h
 * carries the block's base and the barrier helpers.
 */
#include <machine/scb.h>

#define	MPU_TYPE		(SCB_BASE + 0xd90)	/* Table 113. */
#define	MPU_CTRL		(SCB_BASE + 0xd94)	/* Table 114. */
#define	MPU_RNR			(SCB_BASE + 0xd98)	/* Table 115. */
#define	MPU_RBAR		(SCB_BASE + 0xd9c)	/* Table 116. */
#define	MPU_RASR		(SCB_BASE + 0xda0)	/* Table 117. */

#define	MPU_TYPE_SEPARATE	0x00000001UL	/* Reads 0: unified only. */
#define	MPU_TYPE_DREGION_MASK	0x0000ff00UL	/* Data regions, reset 8. */
#define	MPU_TYPE_DREGION_SHIFT	8
#define	MPU_TYPE_IREGION_MASK	0x00ff0000UL	/* Reads 0: unified only. */

#define	MPU_CTRL_ENABLE		0x00000001UL
#define	MPU_CTRL_HFNMIENA	0x00000002UL	/* MPU stays on in HardFault. */
#define	MPU_CTRL_PRIVDEFENA	0x00000004UL	/* Default map for privileged. */

#define	MPU_RNR_REGION_MASK	0x0000000fUL

#define	MPU_RBAR_ADDR_MASK	0xffffff00UL	/* 256-byte granule. */
#define	MPU_RBAR_VALID		0x00000010UL	/* REGION selects and sets RNR. */
#define	MPU_RBAR_REGION_MASK	0x0000000fUL

#define	MPU_RASR_ENABLE		0x00000001UL
#define	MPU_RASR_SIZE_SHIFT	1		/* Bytes = 2^(SIZE+1), SIZE >= 7. */
#define	MPU_RASR_SIZE_MASK	0x0000003eUL
#define	MPU_RASR_SRD_SHIFT	8		/* One disable bit per eighth. */
#define	MPU_RASR_SRD_MASK	0x0000ff00UL
#define	MPU_RASR_B		0x00010000UL
#define	MPU_RASR_C		0x00020000UL
#define	MPU_RASR_S		0x00040000UL
#define	MPU_RASR_AP_SHIFT	24
#define	MPU_RASR_AP_MASK	0x07000000UL
#define	MPU_RASR_XN		0x10000000UL	/* Instruction fetch faults. */

/*
 * Access permission encodings, ARMv6-M ARM table B3-15: the privileged
 * right first, then the unprivileged one.
 */
#define	MPU_AP_NONE		(0UL << MPU_RASR_AP_SHIFT)	/* --  / --  */
#define	MPU_AP_PRIV_RW		(1UL << MPU_RASR_AP_SHIFT)	/* rw  / --  */
#define	MPU_AP_PRIV_RW_USER_RO	(2UL << MPU_RASR_AP_SHIFT)	/* rw  / r-  */
#define	MPU_AP_RW		(3UL << MPU_RASR_AP_SHIFT)	/* rw  / rw  */
#define	MPU_AP_PRIV_RO		(5UL << MPU_RASR_AP_SHIFT)	/* r-  / --  */
#define	MPU_AP_RO		(6UL << MPU_RASR_AP_SHIFT)	/* r-  / r-  */

/* RASR SIZE field for a region of 2^log2 bytes. */
#define	MPU_RASR_SIZE(log2)	((u_int)((log2) - 1) << MPU_RASR_SIZE_SHIFT)

#define	MPU_REG32(a)		(*(volatile u_int *)(a))

/*
 * What mpu_init programmed, read back from the registers after the write
 * so a core or an emulator that drops the writes reports itself; sysctl.c
 * and the cpu: banner print these rather than the values that were sent.
 */
struct mpu_state {
	u_int	type;			/* MPU_TYPE as read. */
	u_int	ctrl;			/* MPU_CTRL read back after enable. */
	u_int	nregions;		/* DREGION from MPU_TYPE. */
	u_int	programmed;		/* Regions written and read back intact. */
};
extern struct mpu_state mpu_state;

void	mpu_init(void);
void	mpu_identify(void);

#endif	/* !_MACHINE_MPU_H_ */
