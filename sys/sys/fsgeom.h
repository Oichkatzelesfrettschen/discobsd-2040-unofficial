/*
 * File-system format geometry for the inode layer (sys/kern/ufs_*.c).
 *
 * The native format and the Seventh Edition (V7) format run the same
 * block-map, free-chain, inode-scan and truncate algorithms over
 * different constants and record layouts. A kernel built with the V7FS
 * option reads the constants from a per-mount struct fs_geom and calls
 * the record codecs in kern/ufs_v7.c; a kernel built without it has one
 * format, and every accessor below expands to the constant or expression
 * the inode layer used before the accessor existed, so its code is
 * unchanged. docs/research/tiny-rump-components/v7-format-descriptor.md
 * derives the parameter set.
 *
 * Included by <sys/mount.h> after struct mount.
 */
#ifndef _SYS_FSGEOM_H_
#define _SYS_FSGEOM_H_

#define FS_FORMAT_NATIVE    0
#define FS_FORMAT_V7        1

/*
 * The independent parameters of a format. Block size and mask, inodes
 * per block (both disk inodes are 64 bytes), indirect fan-out (both
 * store 4-byte entries), the shift from file-system block to buffer
 * block and the first inode block (g_sblock + 1) derive from them.
 */
struct fs_geom {
    u_char  g_bshift;       /* log2 of the block size in bytes */
    u_char  g_ndaddr;       /* direct addresses; NIADDR indirect follow */
    u_char  g_sblock;       /* superblock position, in blocks */
    u_char  g_nicfree;      /* addresses in one free-chain block */
    u_char  g_format;       /* FS_FORMAT_* */
};

#ifdef V7FS

#ifdef SINGLE_UFS_ROOT
#error "V7FS needs a second mount; SINGLE_UFS_ROOT has one"
#endif

extern const struct fs_geom fs_geom_table[];

static inline struct mount *
fs_mount(const struct fs *fs)
{
    return ((struct mount *)((char *)fs - offsetof(struct mount, m_filsys)));
}

#define fs_geom(fs)             (fs_mount(fs)->m_geom)
#define FS_ISV7(fs)             (fs_geom(fs)->g_format == FS_FORMAT_V7)
#define FS_BSHIFT(fs)           (fs_geom(fs)->g_bshift)
#define FS_BSIZE(fs)            (1 << FS_BSHIFT(fs))
#define FS_DBSHIFT(fs)          (DEV_BSHIFT - FS_BSHIFT(fs))
#define FS_INOPB(fs)            (1 << (FS_BSHIFT(fs) - 6))
#define FS_NSHIFT(fs)           (FS_BSHIFT(fs) - 2)
#define FS_NINDIR(fs)           (1U << FS_NSHIFT(fs))
#define FS_NMASK(fs)            ((long)FS_NINDIR(fs) - 1)
#define FS_NDADDR(fs)           (fs_geom(fs)->g_ndaddr)
#define FS_NADDR(fs)            (FS_NDADDR(fs) + NIADDR)
#define FS_NICFREE(fs)          (fs_geom(fs)->g_nicfree)
#define FS_SUPERB(fs)           ((daddr_t)fs_geom(fs)->g_sblock)
#define FS_ITOD(fs, x)          ((daddr_t)(FS_SUPERB(fs) + 1 + \
                                    ((u_int)(x) - 1) / FS_INOPB(fs)))
#define FS_ITOO(fs, x)          ((int)(((u_int)(x) - 1) & (FS_INOPB(fs) - 1)))

/*
 * A file-system block is the whole of a buffer or one aligned part of
 * it: block b lives in buffer block FSBTODB(b) at FSBADDR(). Parts of
 * one buffer share its identity, so a writer of one part reads the
 * buffer first; FSGETBLK() is getblk() only where a block fills its
 * buffer.
 */
#define FSBTODB(fs, b)          ((daddr_t)(b) >> FS_DBSHIFT(fs))
#define FSBADDR(fs, bp, b)      ((bp)->b_addr + \
        (((int)(b) & ((1 << FS_DBSHIFT(fs)) - 1)) << FS_BSHIFT(fs)))
#define FSGETBLK(fs, dev, b)    (FS_DBSHIFT(fs) ? \
        bread((dev), FSBTODB((fs), (b))) : getblk((dev), (b)))
/* The block balloc() returned in bp, valid until the caller next sleeps. */
#define FS_ALLOCBN(fs, bp)      (fs_mount(fs)->m_lastalloc)
#define FS_REMAP(bap, bp)       ((void)0)
/* Release a buffer the caller may already have let go of. */
#define FS_BRELSE_HELD(bp)      do { if (bp) brelse(bp); } while (0)

/*
 * Byte offsets inside one file. A regular file or symbolic link on a
 * V7 volume has the volume's block size. A V7 directory is presented to
 * the kernel as a native-format directory of DIRBLKSIZ blocks (see
 * v7_dirread()), and a device node addresses whole buffers, so both
 * keep the native size.
 */
#define IN_BSHIFT(ip)           ((FS_ISV7(INODE_FILESYSTEM(ip)) && \
        (((ip)->i_mode & IFMT) == IFREG || \
        ((ip)->i_mode & IFMT) == IFLNK)) ? \
        FS_BSHIFT(INODE_FILESYSTEM(ip)) : DEV_BSHIFT)
#define IN_BSIZE(ip)            (1 << IN_BSHIFT(ip))
#define IN_BLKOFF(ip, loc)      ((loc) & (IN_BSIZE(ip) - 1))
#define IN_LBLKNO(ip, loc)      ((unsigned)(loc) >> IN_BSHIFT(ip))
/* rwip()'s view of one such block inside its buffer. */
#define IN_BTODB(ip, b)         ((daddr_t)(b) >> (DEV_BSHIFT - IN_BSHIFT(ip)))
#define IN_BADDR(ip, bp, b)     ((bp)->b_addr + (((int)(b) & \
        ((1 << (DEV_BSHIFT - IN_BSHIFT(ip))) - 1)) << IN_BSHIFT(ip)))
#define IN_GETBLK(ip, dev, b)   (IN_BSHIFT(ip) != DEV_BSHIFT ? \
        bread((dev), IN_BTODB((ip), (b))) : getblk((dev), (b)))
#define IN_SETBASE(base, addr)  ((base) = (addr))
#define IN_BASE(base, bp)       (base)
/* The first block of a symbolic link, read into bp as block bn. */
#define NAMEI_LINKADDR(ip, bp, bn) \
        FSBADDR(INODE_FILESYSTEM(ip), (bp), (bn))
/* Write a directory block blkatoff() returned for byte offset off. */
#define DIRBWRITE(ip, bp, off)  (FS_ISV7(INODE_FILESYSTEM(ip)) ? \
        v7_dirwrite((ip), lblkno(off), (bp)) : bwrite(bp))

/* Block address i of an inode. */
#define IADDR(ip, i)            (FS_ISV7(INODE_FILESYSTEM(ip)) ? \
        v7_addr_get((ip), (i)) : (ip)->i_addr[i])
#define IADDR_SET(ip, i, v)     do { \
        if (FS_ISV7(INODE_FILESYSTEM(ip))) \
            v7_addr_set((ip), (i), (v)); \
        else \
            (ip)->i_addr[i] = (v); \
    } while (0)

/* The same addresses by kind: direct i, and indirect of level l. */
#define IADDR_DB(ip, i)         IADDR((ip), (i))
#define IADDR_DB_SET(ip, i, v)  IADDR_SET((ip), (i), (v))
#define IADDR_IB(ip, l)         \
        IADDR((ip), FS_NDADDR(INODE_FILESYSTEM(ip)) + (l))
#define IADDR_IB_SET(ip, l, v)  \
        IADDR_SET((ip), FS_NDADDR(INODE_FILESYSTEM(ip)) + (l), (v))

/* Space a live directory entry occupies; the rest of its record is free. */
#define DIRFOOT(ip, ep)         (FS_ISV7(INODE_FILESYSTEM(ip)) ? \
        (int)(ep)->d_reclen : (int)DIRSIZ(ep))

struct inode;
struct buf;
struct uio;

daddr_t v7_addr_get (struct inode *ip, int i);
void    v7_addr_set (struct inode *ip, int i, daddr_t bn);
void    v7_iread (struct inode *ip, const u_char *raw);
int     v7_iwrite (struct inode *ip, u_char *raw);
int     v7_sbload (struct fs *fs, const u_char *raw);
int     v7_sbwrite (struct mount *mp, int waitfor);
void    v7_fblk_load (struct fs *fs, const u_char *raw);
void    v7_fblk_store (const struct fs *fs, u_char *raw);
struct buf *v7_dirread (struct inode *ip, daddr_t lbn);
int     v7_dirwrite (struct inode *ip, daddr_t lbn, struct buf *vbp);
int     v7_dirrw (struct inode *ip, struct uio *uio);

#else /* !V7FS */

#define FS_ISV7(fs)             0
#define FS_BSIZE(fs)            DEV_BSIZE
#define FS_INOPB(fs)            INOPB
#define FS_NSHIFT(fs)           NSHIFT
#define FS_NINDIR(fs)           NINDIR
#define FS_NMASK(fs)            NMASK
#define FS_NDADDR(fs)           NDADDR
#define FS_NADDR(fs)            NADDR
#define FS_NICFREE(fs)          NICFREE
#define FS_SUPERB(fs)           SUPERB
#define FS_ITOD(fs, x)          itod(x)
#define FS_ITOO(fs, x)          itoo(x)
#define FSBTODB(fs, b)          (b)
#define FSBADDR(fs, bp, b)      ((bp)->b_addr)
#define FSGETBLK(fs, dev, b)    getblk((dev), (b))
#define FS_ALLOCBN(fs, bp)      dbtofsb((bp)->b_blkno)
#define FS_REMAP(bap, bp)       ((bap) = (daddr_t *)(bp)->b_addr)
#define FS_BRELSE_HELD(bp)      brelse(bp)
#define IN_BSIZE(ip)            DEV_BSIZE
#define IN_BLKOFF(ip, loc)      blkoff(loc)
#define IN_LBLKNO(ip, loc)      lblkno(loc)
#define IN_BTODB(ip, b)         (b)
#define IN_BADDR(ip, bp, b)     ((bp)->b_addr)
#define IN_GETBLK(ip, dev, b)   getblk((dev), (b))
#define IN_SETBASE(base, addr)  ((void)0)
#define IN_BASE(base, bp)       ((bp)->b_addr)
#define DIRBWRITE(ip, bp, off)  bwrite(bp)
#define NAMEI_LINKADDR(ip, bp, bn) ((bp)->b_addr)
#define IADDR(ip, i)            ((ip)->i_addr[i])
#define IADDR_SET(ip, i, v)     ((ip)->i_addr[i] = (v))
#define IADDR_DB(ip, i)         ((ip)->i_db[i])
#define IADDR_DB_SET(ip, i, v)  ((ip)->i_db[i] = (v))
#define IADDR_IB(ip, l)         ((ip)->i_ib[l])
#define IADDR_IB_SET(ip, l, v)  ((ip)->i_ib[l] = (v))
#define DIRFOOT(ip, ep)         DIRSIZ(ep)

#endif /* V7FS */

#endif /* _SYS_FSGEOM_H_ */
