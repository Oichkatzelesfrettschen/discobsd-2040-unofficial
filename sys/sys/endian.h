/*
 * Byte-order constants and conversions in the BSD spelling. <machine/endian.h>
 * selects _BYTE_ORDER for the target; this header derives everything else
 * from it. The constants carry the values every BSD and the compiler's own
 * __ORDER_*_ENDIAN__ predefines use (1234, 4321, 3412), so a source that
 * compares against either family compares like for like.
 *
 * Every conversion is a macro over the compiler's byte-swap builtin, so a
 * constant operand folds at compile time and a variable one is a single rev
 * or rev16 instruction on ARMv6-M. The htonl family keeps its libc functions
 * for link compatibility; <arpa/inet.h> maps the names onto these macros.
 */
#ifndef _SYS_ENDIAN_H_
#define _SYS_ENDIAN_H_

#include <sys/cdefs.h>

/*
 * The conversions are spelled with the base types that carry exactly 16,
 * 32 and 64 bits on every target and host this tree compiles for, so the
 * header declares no typedef: an assembler source reaches it through
 * <machine/machparam.h>, and a host test harness reaches it beside the
 * host's own <stdint.h>, which a second set of typedefs would contradict.
 */
#define __endian_u16	unsigned short
#define __endian_u32	unsigned int
#define __endian_u64	unsigned long long

#define _LITTLE_ENDIAN	1234	/* least-significant byte first */
#define _BIG_ENDIAN	4321	/* most-significant byte first */
#define _PDP_ENDIAN	3412	/* LSB first in word, MSW first in long */

#include <machine/endian.h>

#ifndef _BYTE_ORDER
#error "<machine/endian.h> defines no _BYTE_ORDER"
#endif
#if _BYTE_ORDER != _LITTLE_ENDIAN && _BYTE_ORDER != _BIG_ENDIAN
#error "_BYTE_ORDER names an order this header does not convert"
#endif

/*
 * The public names are guarded: a host test harness that stages this header
 * beside glibc's <endian.h> meets the same four names with the same values
 * already defined, and the first definition stands.
 */
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN	_LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN	_BIG_ENDIAN
#endif
#ifndef PDP_ENDIAN
#define PDP_ENDIAN	_PDP_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER	_BYTE_ORDER
#endif

#if defined(__GNUC__) || defined(__clang__)
#define __swap16(x)	((__endian_u16)__builtin_bswap16((__endian_u16)(x)))
#define __swap32(x)	((__endian_u32)__builtin_bswap32((__endian_u32)(x)))
#define __swap64(x)	((__endian_u64)__builtin_bswap64((__endian_u64)(x)))
#else
#define __swap16(x)	((__endian_u16)( \
	(((__endian_u16)(x) & 0x00ffU) << 8) | \
	(((__endian_u16)(x) & 0xff00U) >> 8)))
#define __swap32(x)	((__endian_u32)( \
	(((__endian_u32)(x) & 0x000000ffUL) << 24) | \
	(((__endian_u32)(x) & 0x0000ff00UL) <<  8) | \
	(((__endian_u32)(x) & 0x00ff0000UL) >>  8) | \
	(((__endian_u32)(x) & 0xff000000UL) >> 24)))
#define __swap64(x)	((__endian_u64)( \
	((__endian_u64)__swap32((__endian_u32)((__endian_u64)(x) & 0xffffffffULL)) << 32) | \
	(__endian_u64)__swap32((__endian_u32)((__endian_u64)(x) >> 32))))
#endif

/*
 * A host libc that defines the conversions defines the whole family, so one
 * guard on htobe16 covers the twelve; the OpenBSD aliases are guarded on
 * their own because glibc has the family and not the aliases.
 */
#ifndef htobe16
#if _BYTE_ORDER == _LITTLE_ENDIAN
#define htobe16(x)	__swap16(x)
#define htobe32(x)	__swap32(x)
#define htobe64(x)	__swap64(x)
#define htole16(x)	((__endian_u16)(x))
#define htole32(x)	((__endian_u32)(x))
#define htole64(x)	((__endian_u64)(x))
#else
#define htobe16(x)	((__endian_u16)(x))
#define htobe32(x)	((__endian_u32)(x))
#define htobe64(x)	((__endian_u64)(x))
#define htole16(x)	__swap16(x)
#define htole32(x)	__swap32(x)
#define htole64(x)	__swap64(x)
#endif

/* Each conversion is its own inverse, so the two directions share a body. */
#define be16toh(x)	htobe16(x)
#define be32toh(x)	htobe32(x)
#define be64toh(x)	htobe64(x)
#define le16toh(x)	htole16(x)
#define le32toh(x)	htole32(x)
#define le64toh(x)	htole64(x)
#endif /* !htobe16 */

/* The OpenBSD spellings of the same six. */
#ifndef betoh16
#define betoh16(x)	be16toh(x)
#define betoh32(x)	be32toh(x)
#define betoh64(x)	be64toh(x)
#define letoh16(x)	le16toh(x)
#define letoh32(x)	le32toh(x)
#define letoh64(x)	le64toh(x)
#endif /* !betoh16 */

/* The NetBSD <sys/bswap.h> spellings of the swaps themselves. */
#ifndef bswap16
#define bswap16(x)	__swap16(x)
#endif
#ifndef bswap32
#define bswap32(x)	__swap32(x)
#endif
#ifndef bswap64
#define bswap64(x)	__swap64(x)
#endif

#endif /* !_SYS_ENDIAN_H_ */
