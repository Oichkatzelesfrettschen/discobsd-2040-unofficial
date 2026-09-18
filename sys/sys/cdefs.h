/*
 * Compiler attributes, named once.
 *
 * The tree had no home for them, so __unused was #defined separately in
 * sys/arch/pic32/pic32/conf.c, sys/arch/stm32/stm32/conf.c,
 * sys/arch/rp2040/rp2040/conf.c and sys/arch/rp2040/dev/flash.c, three of
 * them marked XXX for the duplication, and every unused parameter those
 * four files did not cover was answered by a (void)parameter; statement in
 * the function body instead. The attribute says the same thing where the
 * parameter is declared: a reader sees it in the signature rather than
 * several lines into the body, and a parameter that becomes used again
 * loses the mark in the one place that declares it.
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
