/*
 * Compiler attributes, named once. __unused belongs in a declaration so the
 * signature carries the intentional unused-parameter contract.
 *
 * __packed stays out. sys/arch/arm/include/core_cm4.h defines it empty and
 * sys/arch/stm32/hal/stm32f4xx_hal_def.h defines it as the attribute, both
 * vendored, and a third definition here would conflict with one of them.
 */
#ifndef _SYS_CDEFS_H_
#define _SYS_CDEFS_H_

#if defined(__GNUC__) || defined(__clang__)
#define __unused	__attribute__((__unused__))
#else
#define __unused
#endif

#endif /* !_SYS_CDEFS_H_ */
