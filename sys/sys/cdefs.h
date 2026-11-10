/*
 * Compiler attributes and declaration helpers, named once so a declaration
 * carries its contract in the BSD spelling and a ported source needs no
 * private shim. Every definition is guarded, because the vendored CMSIS and
 * STM32 HAL headers under sys/arch define some of the same names for other
 * compilers and a second unguarded definition would conflict with theirs.
 */
#ifndef _SYS_CDEFS_H_
#define _SYS_CDEFS_H_

#if defined(__cplusplus)
#define __BEGIN_DECLS	extern "C" {
#define __END_DECLS	}
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

/* Pre-ANSI prototype bridge: the sources all take prototypes. */
#ifndef __P
#define __P(protos)	protos
#endif

#ifndef __CONCAT
#define __CONCAT(x, y)	x ## y
#endif
#ifndef __STRING
#define __STRING(x)	#x
#endif

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define __GNUC_PREREQ__(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
#define __GNUC_PREREQ__(maj, min)	0
#endif

/*
 * __dead is a function specifier: `static __dead void usage(void)`. The C11
 * keyword is the standard spelling; the GNU attribute serves an older
 * dialect; any other compiler drops the annotation and loses only the
 * diagnostics it enables.
 */
#ifndef __dead
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define __dead		_Noreturn
#elif defined(__GNUC__) || defined(__clang__)
#define __dead		__attribute__((__noreturn__))
#else
#define __dead
#endif
#endif
#ifndef __dead2
#define __dead2		__dead
#endif

#if defined(__GNUC__) || defined(__clang__)
#ifndef __unused
#define __unused	__attribute__((__unused__))
#endif
#ifndef __used
#define __used		__attribute__((__used__))
#endif
#ifndef __pure
#define __pure		__attribute__((__pure__))
#endif
#ifndef __pure2
#define __pure2		__attribute__((__const__))
#endif
#ifndef __constfunc
#define __constfunc	__attribute__((__const__))
#endif
#ifndef __packed
#define __packed	__attribute__((__packed__))
#endif
#ifndef __aligned
#define __aligned(x)	__attribute__((__aligned__(x)))
#endif
#ifndef __section
#define __section(s)	__attribute__((__section__(s)))
#endif
#ifndef __printflike
#define __printflike(fmtarg, firstvararg) \
	__attribute__((__format__(__printf__, fmtarg, firstvararg)))
#endif
#ifndef __scanflike
#define __scanflike(fmtarg, firstvararg) \
	__attribute__((__format__(__scanf__, fmtarg, firstvararg)))
#endif
#ifndef __format_arg
#define __format_arg(fmtarg)	__attribute__((__format_arg__(fmtarg)))
#endif
#ifndef __nonnull
#define __nonnull(args)	__attribute__((__nonnull__ args))
#endif
#ifndef __predict_true
#define __predict_true(exp)	__builtin_expect((exp) != 0, 1)
#endif
#ifndef __predict_false
#define __predict_false(exp)	__builtin_expect((exp) != 0, 0)
#endif
#else /* !__GNUC__ && !__clang__ */
#ifndef __unused
#define __unused
#endif
#ifndef __used
#define __used
#endif
#ifndef __pure
#define __pure
#endif
#ifndef __pure2
#define __pure2
#endif
#ifndef __constfunc
#define __constfunc
#endif
#ifndef __packed
#define __packed
#endif
#ifndef __aligned
#define __aligned(x)
#endif
#ifndef __section
#define __section(s)
#endif
#ifndef __printflike
#define __printflike(fmtarg, firstvararg)
#endif
#ifndef __scanflike
#define __scanflike(fmtarg, firstvararg)
#endif
#ifndef __format_arg
#define __format_arg(fmtarg)
#endif
#ifndef __nonnull
#define __nonnull(args)
#endif
#ifndef __predict_true
#define __predict_true(exp)	(exp)
#endif
#ifndef __predict_false
#define __predict_false(exp)	(exp)
#endif
#endif /* !__GNUC__ && !__clang__ */

/*
 * Revision and copyright tags expand to nothing: the tree keeps provenance
 * in git and the NetBSD imports under sys/fs/v7fs carry the tags verbatim.
 */
#ifndef __RCSID
#define __RCSID(s)
#endif
#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(n, s)
#endif
#ifndef __COPYRIGHT
#define __COPYRIGHT(s)
#endif

#endif /* !_SYS_CDEFS_H_ */
