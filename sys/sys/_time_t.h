/*
 * The widths of a count of seconds since 1970, in one place for sys/types.h
 * and time.h.
 *
 * utime32_t is the count as the kernel clock, the inode and the superblock
 * hold it: 32 bits on the target, read unsigned, valid to 2106. The
 * kernel's time_t is this type, and a structure the kernel and a program
 * both lay out (struct icommon2, struct fs, struct vattr) names it so that
 * the layout does not follow the program's time_t.
 *
 * A program's time_t is 64 bits wide: a unit the cross compiler builds
 * gets this view unless it defines _TIME_BITS as 32. The cross compiler is
 * the ARM EABI compiler with no hosted system (__ARM_EABI__ defined,
 * __unix__ not); a host compiler that reads these headers for a contract
 * test links the host's libc, which defines the existing names, so it
 * keeps the 32-bit view unless it defines _TIME_BITS as 64. The
 * structures that hold a time_t (struct timeval, struct timespec, struct
 * stat, struct rusage) take the wider layout, and __TIME64_NAME() binds each function
 * that passes one to a second symbol, __NAME64. libc defines it as a
 * wrapper around the 32-bit entry point (lib/libc/gen/time64_*.c) or, for
 * a function that converts or carries the count itself (ctime, mktime,
 * time, utime and the others), as the function's own source compiled a
 * second time with this view (NAME64.o in lib/libc/gen/Makefile and
 * lib/libc/compat/Makefile).
 *
 * The existing symbol names keep the 32-bit view: a signed long time_t
 * and the kernel's layouts. libc compiles its members with _TIME_BITS
 * defined as 32 (lib/libc/Makefile), and a program compiled by Smaller C
 * on the board declares these names by hand; that compiler has no 64-bit
 * integer, so the wide view is closed to it.
 */
#ifndef _SYS__TIME_T_H_
#define _SYS__TIME_T_H_

typedef unsigned long   utime32_t;

#if !defined(KERNEL) && !defined(__SMALLER_C__)
#if !defined(_TIME_BITS)
#if defined(__ARM_EABI__) && !defined(__unix__)
#define __TIME64_VIEW   1
#endif
#elif _TIME_BITS == 64
#define __TIME64_VIEW   1
#elif _TIME_BITS != 32
#error "_TIME_BITS is 32 or 64"
#endif
#endif

#ifdef __TIME64_VIEW
#define __TIME64_NAME(name)     __asm__("__" #name "64")
#else
#define __TIME64_NAME(name)
#endif

#ifndef _TIME_T
#define _TIME_T
#if defined(KERNEL)
typedef utime32_t time_t;
#elif defined(__TIME64_VIEW)
typedef long long time_t;
#else
typedef long    time_t;
#endif
#endif

#endif /* !_SYS__TIME_T_H_ */
