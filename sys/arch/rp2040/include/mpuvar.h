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

#ifndef	_MACHINE_MPUVAR_H_
#define	_MACHINE_MPUVAR_H_

/*
 * machdep.mpu: the memory protection unit as the kernel reads it back.
 * The first four names and their numbers are the STM32 port's
 * (sys/arch/stm32/include/mpuvar.h), so sbin/sysctl's machdep.mpu handler
 * serves both ports from one table; the RP2040 adds the count of regions
 * mpu_init programmed and read back intact, which is what a protection
 * claim for the user window rests on.
 */
#ifdef KERNEL
int	mpu_sysctl(int *, u_int, void *, size_t *, void *, size_t);
#endif

#define	CPU_MPU_ENABLE		1	/* int: MPU_CTRL.ENABLE as read */
#define	CPU_MPU_CTRL		2	/* int: MPU_CTRL as read */
#define	CPU_MPU_NREGIONS	3	/* int: MPU_TYPE.DREGION */
#define	CPU_MPU_SEPARATE	4	/* int: MPU_TYPE.SEPARATE */
#define	CPU_MPU_PROGRAMMED	5	/* int: regions written and verified */
#define	CPU_MPU_MAXID		6

#ifndef	KERNEL
#define	CTL_MPU_NAMES { \
	{ 0, 0 }, \
	{ "enable", CTLTYPE_INT }, \
	{ "ctrl", CTLTYPE_INT }, \
	{ "nregions", CTLTYPE_INT }, \
	{ "separate", CTLTYPE_INT }, \
	{ "programmed", CTLTYPE_INT }, \
}
#endif	/* !KERNEL */

#endif	/* !_MACHINE_MPUVAR_H_ */
