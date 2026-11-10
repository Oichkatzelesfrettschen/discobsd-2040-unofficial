/*
 * Record codecs for Seventh Edition (V7) volumes.
 *
 * The inode layer runs one copy of its algorithms over both formats and
 * reads the format's constants through <sys/fsgeom.h>. The functions
 * here translate the four records whose layout differs: the superblock,
 * the free-chain block, the disk inode and the directory block. Fields
 * are little-endian and read byte by byte, so no access depends on
 * alignment; PDP-11-order volumes belong to usr.sbin/v7fs.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/buf.h>
#include <sys/fs.h>
#include <sys/mount.h>
#include <sys/dir.h>
#include <sys/uio.h>
#include <sys/kernel.h>

#ifdef V7FS

#define V7_NADDR        13      /* ten direct addresses, three indirect */
#define V7_NICINOD      100     /* free inodes cached in the superblock */
#define V7_NAMELEN      14
#define V7_SLOT         16      /* a directory entry on the volume */
#define V7_NSLOT        32      /* entries in one 512-byte block */
#define V7_VREC         32      /* the record one entry expands to */
#define V7_VRECSHIFT    5
#define V7_DOTREC       12      /* "." as mkdir's template lays it out */
#define V7_ADDR_ESCAPE  0xffff  /* in i_a16[]: the address is on the disk */
#define V7_ADDR_MAX     0xffffffL

/* Byte offsets in the superblock. */
#define SB_ISIZE        0
#define SB_FSIZE        2
#define SB_NFREE        6
#define SB_FREE         8
#define SB_NINODE       208
#define SB_INODE        210
#define SB_LOCKS        410     /* flock, ilock, fmod, ronly */
#define SB_TIME         414
#define SB_TFREE        418
#define SB_TINODE       422

/* Byte offsets in the disk inode. */
#define DI_MODE         0
#define DI_NLINK        2
#define DI_UID          4
#define DI_GID          6
#define DI_SIZE         8
#define DI_ADDR         12
#define DI_ATIME        52
#define DI_MTIME        56
#define DI_CTIME        60

_Static_assert(sizeof(struct dinode) == 64, "both disk inodes are 64 bytes");
_Static_assert(sizeof(((struct inode *)0)->i_a16) <=
    sizeof(((struct inode *)0)->i_addr), "V7 addresses fit the address slot");
_Static_assert(V7_NSLOT * V7_VREC == DIRBLKSIZ,
    "one V7 directory block expands to one DIRBLKSIZ block");
_Static_assert(NICFREE <= 255, "g_nicfree is one byte");

const struct fs_geom fs_geom_table[] = {
    [FS_FORMAT_NATIVE] = { DEV_BSHIFT, NDADDR, SUPERB, NICFREE,
                           FS_FORMAT_NATIVE },
    [FS_FORMAT_V7]     = { 9, 10, 1, 50, FS_FORMAT_V7 },
};

static inline u_int
get16(const u_char *p)
{
    return (p[0] | (u_int)p[1] << 8);
}

static inline u_long
get24(const u_char *p)
{
    return (p[0] | (u_long)p[1] << 8 | (u_long)p[2] << 16);
}

static inline u_long
get32(const u_char *p)
{
    return (get24(p) | (u_long)p[3] << 24);
}

static inline void
put16(u_char *p, u_int v)
{
    p[0] = v;
    p[1] = v >> 8;
}

static inline void
put24(u_char *p, u_long v)
{
    put16(p, v);
    p[2] = v >> 16;
}

static inline void
put32(u_char *p, u_long v)
{
    put24(p, v);
    p[3] = v >> 24;
}

/*
 * Superblock. The in-core form is struct fs for both formats, so the
 * free-chain and inode-scan code never sees the layout. V7 has no magic
 * number: the range checks below are the only evidence that raw is a
 * superblock. The volume caches up to V7_NICINOD free inodes and struct
 * fs holds NICINOD; the cache is a hint ialloc() rebuilds by scanning,
 * so the entries nearest the top of the stack are kept.
 */
int
v7_sbload(struct fs *fs, const u_char *raw)
{
    u_long isize, fsize, bn;
    int nfree, ninode, keep, i;

    isize = get16(raw + SB_ISIZE);
    fsize = get32(raw + SB_FSIZE);
    nfree = (short)get16(raw + SB_NFREE);
    ninode = (short)get16(raw + SB_NINODE);
    if (isize < 3 || fsize <= isize || fsize > V7_ADDR_MAX ||
        nfree < 0 || nfree > fs_geom_table[FS_FORMAT_V7].g_nicfree ||
        ninode < 0 || ninode > V7_NICINOD)
        return (EINVAL);
    bzero((caddr_t)fs, sizeof(*fs));
    for (i = 0; i < nfree; i++) {
        bn = get32(raw + SB_FREE + 4 * i);
        /* Entry 0 links the chain and is 0 at its end. */
        if ((bn != 0 || i != 0) && (bn < isize || bn >= fsize))
            return (EINVAL);
        fs->fs_free[i] = bn;
    }
    keep = ninode < NICINOD ? ninode : NICINOD;
    for (i = 0; i < keep; i++)
        fs->fs_inode[i] = get16(raw + SB_INODE + 2 * (ninode - keep + i));
    fs->fs_magic1 = FSMAGIC1;
    fs->fs_magic2 = FSMAGIC2;
    fs->fs_isize = isize;
    fs->fs_fsize = fsize;
    fs->fs_nfree = nfree;
    fs->fs_ninode = keep;
    fs->fs_time = get32(raw + SB_TIME);
    fs->fs_tfree = get32(raw + SB_TFREE);
    fs->fs_tinode = get16(raw + SB_TINODE);
    return (0);
}

/*
 * Write the superblock into the second half of buffer block 0. The read
 * keeps the boot block in the first half and the fields this kernel
 * does not maintain (interleave, volume and pack names).
 */
int
v7_sbwrite(struct mount *mp, int waitfor)
{
    const struct fs *fs = &mp->m_filsys;
    struct buf *bp;
    u_char *raw;
    int i;

    bp = bread(mp->m_dev, (daddr_t)0);
    if (bp->b_flags & B_ERROR) {
        brelse(bp);
        return (EIO);
    }
    raw = (u_char *)bp->b_addr + 512;
    put16(raw + SB_ISIZE, fs->fs_isize);
    put32(raw + SB_FSIZE, fs->fs_fsize);
    put16(raw + SB_NFREE, fs->fs_nfree);
    for (i = 0; i < fs_geom_table[FS_FORMAT_V7].g_nicfree; i++)
        put32(raw + SB_FREE + 4 * i, i < fs->fs_nfree ? fs->fs_free[i] : 0);
    put16(raw + SB_NINODE, fs->fs_ninode);
    for (i = 0; i < V7_NICINOD; i++)
        put16(raw + SB_INODE + 2 * i,
            i < fs->fs_ninode ? fs->fs_inode[i] : 0);
    bzero((caddr_t)raw + SB_LOCKS, 4);
    put32(raw + SB_TIME, time.tv_sec);
    put32(raw + SB_TFREE, fs->fs_tfree);
    put16(raw + SB_TINODE, fs->fs_tinode);
    if (waitfor)
        return (bwrite(bp));
    bdwrite(bp);
    return (0);
}

/*
 * Free-chain block: a 16-bit count and that many 4-byte addresses. A
 * count outside the format's range loads as an empty chain, which
 * balloc() reports as a full volume.
 */
void
v7_fblk_load(struct fs *fs, const u_char *raw)
{
    int n, i;

    n = (short)get16(raw);
    if (n < 0 || n > fs_geom_table[FS_FORMAT_V7].g_nicfree)
        n = 0;
    for (i = 0; i < n; i++)
        fs->fs_free[i] = get32(raw + 2 + 4 * i);
    fs->fs_nfree = n;
}

void
v7_fblk_store(const struct fs *fs, u_char *raw)
{
    int i;

    put16(raw, fs->fs_nfree);
    for (i = 0; i < fs_geom_table[FS_FORMAT_V7].g_nicfree; i++)
        put32(raw + 2 + 4 * i, i < fs->fs_nfree ? fs->fs_free[i] : 0);
}

/*
 * The bytes of ip's disk inode inside a held buffer, or NULL with
 * u.u_error set. The caller releases *bpp.
 */
static u_char *
v7_dinode(struct inode *ip, struct buf **bpp)
{
    struct fs *fs = INODE_FILESYSTEM(ip);
    daddr_t bn = FS_ITOD(fs, ip->i_number);
    struct buf *bp;

    bp = bread(INODE_DEVICE(ip), FSBTODB(fs, bn));
    if (bp->b_flags & B_ERROR) {
        brelse(bp);
        u.u_error = EIO;
        return (NULL);
    }
    *bpp = bp;
    return ((u_char *)FSBADDR(fs, bp, bn) +
        FS_ITOO(fs, ip->i_number) * sizeof(struct dinode));
}

/*
 * Block addresses. The disk inode stores thirteen 3-byte addresses and
 * the in-core address slot is 28 bytes, so each address is kept in 16
 * bits. An address that does not fit is kept as V7_ADDR_ESCAPE and
 * stays on the disk: the reader fetches it from the disk inode and the
 * writer stores it there at once, as System V kept a 16-bit owner field
 * whose all-ones value sends the reader to the wide field. A volume of
 * fewer than 65,535 blocks never takes the escape.
 */
daddr_t
v7_addr_get(struct inode *ip, int i)
{
    struct buf *bp;
    u_char *raw;
    daddr_t bn;

    if (ip->i_a16[i] != V7_ADDR_ESCAPE)
        return (ip->i_a16[i]);
    raw = v7_dinode(ip, &bp);
    if (raw == NULL)
        return (0);
    bn = get24(raw + DI_ADDR + 3 * i);
    brelse(bp);
    return (bn);
}

void
v7_addr_set(struct inode *ip, int i, daddr_t bn)
{
    struct buf *bp;
    u_char *raw;

    if (bn < V7_ADDR_ESCAPE) {
        ip->i_a16[i] = bn;
        return;
    }
    raw = v7_dinode(ip, &bp);
    if (raw == NULL)
        return;
    put24(raw + DI_ADDR + 3 * i, bn);
    bdwrite(bp);
    ip->i_a16[i] = V7_ADDR_ESCAPE;
}

/*
 * Disk inode to in-core inode. A device node keeps its number in the
 * first address. A directory's size doubles, because the kernel sees
 * the directory through v7_dirread().
 */
void
v7_iread(struct inode *ip, const u_char *raw)
{
    u_long size;
    int i, type;

    ip->i_mode = get16(raw + DI_MODE);
    ip->i_nlink = get16(raw + DI_NLINK);
    ip->i_uid = get16(raw + DI_UID);
    ip->i_gid = get16(raw + DI_GID);
    ip->i_flags = 0;
    ip->i_atime = get32(raw + DI_ATIME);
    ip->i_mtime = get32(raw + DI_MTIME);
    ip->i_ctime = get32(raw + DI_CTIME);
    size = get32(raw + DI_SIZE);
    type = ip->i_mode & IFMT;
    bzero((caddr_t)ip->i_addr, sizeof(ip->i_addr));
    if (type == IFCHR || type == IFBLK)
        ip->i_rdev = (dev_t)get16(raw + DI_ADDR);
    else
        for (i = 0; i < V7_NADDR; i++) {
            u_long bn = get24(raw + DI_ADDR + 3 * i);

            ip->i_a16[i] = bn < V7_ADDR_ESCAPE ? bn : V7_ADDR_ESCAPE;
        }
    if (type == IFDIR)
        size <<= 1;
    ip->i_size = size;
}

/*
 * In-core inode to disk inode. The format's owner, group and device
 * fields are 16 bits and its link count 15; a value outside them fails
 * the update with nothing written.
 */
int
v7_iwrite(struct inode *ip, u_char *raw)
{
    int i, type;

    type = ip->i_mode & IFMT;
    if (ip->i_uid > 0xffff || ip->i_gid > 0xffff || ip->i_nlink > 0x7fff)
        return (EOVERFLOW);
    if ((type == IFCHR || type == IFBLK) && (u_long)ip->i_rdev > 0xffff)
        return (EOVERFLOW);
    put16(raw + DI_MODE, ip->i_mode);
    put16(raw + DI_NLINK, ip->i_nlink);
    put16(raw + DI_UID, ip->i_uid);
    put16(raw + DI_GID, ip->i_gid);
    put32(raw + DI_SIZE, type == IFDIR ? ip->i_size >> 1 : ip->i_size);
    if (type == IFCHR || type == IFBLK) {
        bzero((caddr_t)raw + DI_ADDR, 3 * V7_NADDR);
        put16(raw + DI_ADDR, ip->i_rdev);
    } else
        for (i = 0; i < V7_NADDR; i++)
            if (ip->i_a16[i] != V7_ADDR_ESCAPE)
                put24(raw + DI_ADDR + 3 * i, ip->i_a16[i]);
    put32(raw + DI_ATIME, ip->i_atime);
    put32(raw + DI_MTIME, ip->i_mtime);
    put32(raw + DI_CTIME, ip->i_ctime);
    return (0);
}

/*
 * Directories. A V7 directory block is 32 entries of a 2-byte inode
 * number and a 14-byte name. The kernel's directory code (ufs_namei.c,
 * rename, mkdir, rmdir) and libc's readdir() parse struct direct
 * records in DIRBLKSIZ blocks, so a directory block is presented as one
 * DIRBLKSIZ block in which entry j is a V7_VREC-byte record at byte
 * V7_VREC * j: the directory reads as a native one of twice its size
 * and an offset maps by a shift. Block 0 of a directory that starts
 * with "." gives "." 12 bytes and ".." the 52 after them, the layout
 * mkdir's template writes and the one rename and checkpath() read ".."
 * from.
 *
 * The expansion is rebuilt from the volume on every read and holds one
 * record per entry, so a live record never carries free space and the
 * native compaction path finds nothing to compact (DIRFOOT()).
 */
static void
v7_record(caddr_t p, u_int ino, const u_char *name, int reclen)
{
    struct direct *d = (struct direct *)p;
    int len;

    bzero(p, reclen);
    d->d_reclen = reclen;
    if (ino == 0)
        return;
    for (len = 0; len < V7_NAMELEN && name[len] != '\0'; len++)
        d->d_name[len] = name[len];
    d->d_ino = ino;
    d->d_namlen = len;
}

/*
 * Logical block lbn of directory ip, expanded into a buffer of its own
 * that the caller releases with brelse() or hands to v7_dirwrite().
 * Entries at and past the end of the directory, and a block the
 * directory has not allocated, read as free.
 */
struct buf *
v7_dirread(struct inode *ip, daddr_t lbn)
{
    struct fs *fs = INODE_FILESYSTEM(ip);
    struct buf *vbp, *bp = NULL;
    const u_char *src = NULL;
    u_long dsize = ip->i_size >> 1;
    u_long base = (u_long)lbn << fs_geom_table[FS_FORMAT_V7].g_bshift;
    daddr_t bn;
    int live = 0, j;
    caddr_t out;

    if (base < dsize) {
        bn = bmap(ip, lbn, B_READ, 0);
        if (u.u_error)
            return (NULL);
        if (bn > 0) {
            bp = bread(INODE_DEVICE(ip), FSBTODB(fs, bn));
            if (bp->b_flags & B_ERROR) {
                brelse(bp);
                u.u_error = EIO;
                return (NULL);
            }
            src = (const u_char *)FSBADDR(fs, bp, bn);
            live = (dsize - base) / V7_SLOT;
            if (live > V7_NSLOT)
                live = V7_NSLOT;
        }
    }
    vbp = geteblk();
    out = vbp->b_addr;
    j = 0;
    if (lbn == 0 && live >= 2 && get16(src) != 0 &&
        src[2] == '.' && src[3] == '\0') {
        v7_record(out, get16(src), src + 2, V7_DOTREC);
        v7_record(out + V7_DOTREC, get16(src + V7_SLOT), src + V7_SLOT + 2,
            2 * V7_VREC - V7_DOTREC);
        j = 2;
    }
    for (; j < V7_NSLOT; j++)
        v7_record(out + V7_VREC * j,
            j < live ? get16(src + V7_SLOT * j) : 0,
            j < live ? src + V7_SLOT * j + 2 : NULL, V7_VREC);
    if (bp)
        brelse(bp);
    return (vbp);
}

/*
 * Walk the records of an expanded block and place each live one in the
 * first entry at or after its byte offset that an earlier record has
 * not taken, which returns every record v7_dirread() produced to the
 * entry it came from. With dst NULL nothing is stored. Returns the
 * number of entries up to the last live one, or -1 for a block that
 * does not contract: a malformed record, a name longer than an entry
 * holds, or more live records than entries.
 */
static int
v7_contract(caddr_t vblk, u_char *dst)
{
    const struct direct *d;
    int off, slot, next = 0;

    for (off = 0; off < DIRBLKSIZ; off += d->d_reclen) {
        d = (const struct direct *)(vblk + off);
        if (d->d_reclen < V7_DOTREC || (d->d_reclen & 3) ||
            off + d->d_reclen > DIRBLKSIZ)
            return (-1);
        if (d->d_ino == 0)
            continue;
        slot = (off + V7_VREC - 1) >> V7_VRECSHIFT;
        if (slot < next)
            slot = next;
        if (slot >= V7_NSLOT || d->d_namlen > V7_NAMELEN ||
            d->d_ino > 0xffff)
            return (-1);
        if (dst) {
            put16(dst + V7_SLOT * slot, d->d_ino);
            bcopy((caddr_t)d->d_name, (caddr_t)dst + V7_SLOT * slot + 2,
                d->d_namlen);
        }
        next = slot + 1;
    }
    return (next);
}

/*
 * Write an expanded block back as logical block lbn of directory ip and
 * release it. The block is checked before the volume's copy is touched,
 * and the write is synchronous, as the kernel's directory writes are.
 */
int
v7_dirwrite(struct inode *ip, daddr_t lbn, struct buf *vbp)
{
    struct fs *fs = INODE_FILESYSTEM(ip);
    struct buf *bp;
    u_char *dst;
    daddr_t bn;
    u_long end;
    int used, error;

    used = v7_contract(vbp->b_addr, NULL);
    if (used < 0) {
        brelse(vbp);
        return (EIO);
    }
    bn = bmap(ip, lbn, B_WRITE, B_CLRBUF | B_SYNC);
    if (u.u_error || bn <= 0) {
        brelse(vbp);
        return (u.u_error ? u.u_error : EIO);
    }
    bp = bread(INODE_DEVICE(ip), FSBTODB(fs, bn));
    if (bp->b_flags & B_ERROR) {
        brelse(bp);
        brelse(vbp);
        return (EIO);
    }
    dst = (u_char *)FSBADDR(fs, bp, bn);
    bzero((caddr_t)dst, FS_BSIZE(fs));
    (void) v7_contract(vbp->b_addr, dst);
    brelse(vbp);
    error = bwrite(bp);
    if (error)
        return (error);
    end = ((u_long)lbn << DEV_BSHIFT) + ((u_long)used << V7_VRECSHIFT);
    if ((u_long)ip->i_size < end)
        ip->i_size = end;
    ip->i_flag |= IUPD|ICHG;
    return (0);
}

/*
 * read(2) and the kernel's rdwri() on a V7 directory: the bytes of the
 * expanded form.
 */
int
v7_dirrw(struct inode *ip, struct uio *uio)
{
    struct buf *vbp;
    daddr_t lbn;
    off_t off;
    int on, n, error = 0;

    while (error == 0 && uio->uio_resid != 0) {
        off = uio->uio_offset;
        lbn = lblkno(off);
        on = blkoff(off);
        n = MIN((u_int)(DIRBLKSIZ - on), uio->uio_resid);
        if (uio->uio_rw == UIO_READ) {
            if (off >= ip->i_size)
                break;
            if (ip->i_size - off < n)
                n = ip->i_size - off;
        }
        vbp = v7_dirread(ip, lbn);
        if (vbp == NULL) {
            error = u.u_error ? u.u_error : EIO;
            break;
        }
        error = uiomove(vbp->b_addr + on, n, uio);
        if (uio->uio_rw == UIO_READ || error) {
            brelse(vbp);
            continue;
        }
        error = v7_dirwrite(ip, lbn, vbp);
        if (error == 0 && off + n > ip->i_size)
            ip->i_size = off + n;
    }
    return (error);
}

#endif /* V7FS */
