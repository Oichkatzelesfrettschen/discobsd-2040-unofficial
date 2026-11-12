/*
 * The NetBSD <sys/endian.h> subset sys/fs/msdosfs reads: little-endian
 * decode and encode of unaligned 16- and 32-bit fields (bpb.h's getushort,
 * getulong, putushort, putulong) and le16toh/htole16 for the long-name
 * entries in msdosfs_conv.c. Each macro yields to a host definition.
 */
#ifndef MSDOSFS_COMPAT_SYS_ENDIAN_H
#define MSDOSFS_COMPAT_SYS_ENDIAN_H

#include <stdint.h>

static inline uint16_t
msdosfs_le16dec(const void *buf)
{
	const uint8_t *p = buf;

	return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t
msdosfs_le32dec(const void *buf)
{
	const uint8_t *p = buf;

	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void
msdosfs_le16enc(void *buf, uint32_t v)
{
	uint8_t *p = buf;

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static inline void
msdosfs_le32enc(void *buf, uint32_t v)
{
	uint8_t *p = buf;

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static inline uint16_t
msdosfs_le16swap(uint16_t v)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return __builtin_bswap16(v);
#else
	return v;
#endif
}

#ifndef le16dec
#define le16dec(p) msdosfs_le16dec(p)
#endif
#ifndef le32dec
#define le32dec(p) msdosfs_le32dec(p)
#endif
#ifndef le16enc
#define le16enc(p, v) msdosfs_le16enc((p), (v))
#endif
#ifndef le32enc
#define le32enc(p, v) msdosfs_le32enc((p), (v))
#endif
#ifndef le16toh
#define le16toh(v) msdosfs_le16swap(v)
#endif
#ifndef htole16
#define htole16(v) msdosfs_le16swap(v)
#endif

#endif /* !MSDOSFS_COMPAT_SYS_ENDIAN_H */
