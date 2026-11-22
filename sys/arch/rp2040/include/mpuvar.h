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

#ifndef	_MACHINE_MPUVAR_H_
#define	_MACHINE_MPUVAR_H_

/*
 * machdep.mpu: the memory protection unit as the kernel reads it back.
 * The first four names and their numbers are the STM32 port's
 * (sys/arch/stm32/include/mpuvar.h), so sbin/sysctl's machdep.mpu handler
 * serves both ports from one table; the RP2040 adds the count of regions
 * mpu_init programmed and read back intact, which is what a protection
 * claim for the user window rests on, and region 7's RBAR and RASR as read
 * back while the calling process runs: the break guard mpu.c places.
 */
#ifdef KERNEL
void	mpu_init(void);
void	mpu_identify(void);
int	mpu_user_rospan(u_int, u_int);
int	mpu_sysctl(int *, u_int, void *, size_t *, void *, size_t);
void	mpu_guard_update(u_int);
void	mpu_guard_off(void);
extern u_int mpu_guard_floor;
#endif

#define	CPU_MPU_ENABLE		1	/* int: MPU_CTRL.ENABLE as read */
#define	CPU_MPU_CTRL		2	/* int: MPU_CTRL as read */
#define	CPU_MPU_NREGIONS	3	/* int: MPU_TYPE.DREGION */
#define	CPU_MPU_SEPARATE	4	/* int: MPU_TYPE.SEPARATE */
#define	CPU_MPU_PROGRAMMED	5	/* int: regions written and verified */
#define	CPU_MPU_GUARDBASE	6	/* int: region 7 RBAR as read */
#define	CPU_MPU_GUARDATTR	7	/* int: region 7 RASR as read */
#define	CPU_MPU_MAXID		8

#ifndef	KERNEL
#define	CTL_MPU_NAMES { \
	{ 0, 0 }, \
	{ "enable", CTLTYPE_INT }, \
	{ "ctrl", CTLTYPE_INT }, \
	{ "nregions", CTLTYPE_INT }, \
	{ "separate", CTLTYPE_INT }, \
	{ "programmed", CTLTYPE_INT }, \
	{ "guardbase", CTLTYPE_INT }, \
	{ "guardattr", CTLTYPE_INT }, \
}
#endif	/* !KERNEL */

#endif	/* !_MACHINE_MPUVAR_H_ */
