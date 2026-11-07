/*
 * Host gate for the block-device lifecycle of a mounted filesystem: how
 * unmount(2) and a failed mount(2) release the driver, what unmount(2) does
 * when the media was removed, and which write opens securelevel 1 refuses
 * while a filesystem is mounted.
 *
 * The gate links the kernel's own sys/kern/ufs_mount.c (smount, umount,
 * mountfs), ufs_fio.c (ufs_mountedon), ufs_bio.c (the buffer cache),
 * ufs_inode.c (iflush), ufs_syscalls2.c (ufs_sync), ufs_subr.c, ufs_alloc.c,
 * sys_inode.c (openi, closei) and vfs_vnops.c (vn_closefile) at the target's
 * width. Two further pieces are extracted textually from their owners, the
 * way the tier extracts a function from a file it cannot link whole:
 * ufs_disk_conf_functions.c holds isdisk, chrtoblk and blkoverlap from
 * sys/arch/rp2040/rp2040/conf.c, and ufs_disk_flash_functions.c holds the
 * partition state, flopen, flclose and flioctl from
 * sys/arch/rp2040/dev/flash.c. The fl driver is therefore the production
 * one except for its strategy routine, its table loader and its Dhara
 * checkpoint, which this file replaces with RAM and a fixed table. The sd
 * driver is a model of sys/arch/rp2040/dev/sd.c's contract (a per-partition
 * open mask that the strategy routine checks, DIOCGETPART from a table,
 * DIOCMEDIAGONE from a replaced flag); tests/picocalc/sdstrat_test.c holds
 * the production sd.c to the same contract.
 *
 * The disks are RAM. Each fake driver counts opens and closes, records every
 * write and the state of the media at that write, and refuses transfers
 * from removed media the way sdstrategy does.
 */
#include "hostkern.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/disk.h>
#include <sys/kernel.h>
#include <sys/ioctl.h>

#include <rp2040/dev/sd.h>

_Static_assert(sizeof(void *) == 4, "the target is ILP32");

struct user u;
struct mount mount[NMOUNT];
struct inode inode[NINODE];
struct file file[NFILE];
struct buf buf[NBUF];
struct buf bfreelist[BQUEUES];
char bufdata[NBUF * MAXBSIZE];
struct timeval time;
int securelevel;
dev_t rootdev;

/* Kernel services the linked sources call and this file replaces. */
int
suser(void)
{
	return 1;
}

/* The kernel's returns-twice setjmp is renamed to this at compile time. */
int
ufs_disk_setjmp(label_t *state)
{
	(void)state;
	return 0;
}

void
wakeup(caddr_t chan)
{
	(void)chan;
}

int
sleep(caddr_t chan, int pri)
{
	(void)chan;
	(void)pri;
	hk_fail(__FILE__, __LINE__, "kernel code slept");
	return 0;
}

int lbolt;

void
uprintf(char *fmt, ...)
{
	(void)fmt;
}

void
nchinval(dev_t dev)
{
	(void)dev;
}

void
cacheinval(struct inode *ip)
{
	(void)ip;
}

int
copystr(caddr_t from, caddr_t to, u_int maxlen, u_int *done)
{
	const char *f = from;
	char *t = to;
	u_int n = 0;

	while (n < maxlen) {
		n++;
		if ((*t++ = *f++) == '\0')
			break;
	}
	if (done != NULL)
		*done = n;
	return 0;
}

int
copyin(const caddr_t from, caddr_t to, u_int n)
{
	bcopy(from, to, n);
	return 0;
}

int
copyout(const caddr_t from, caddr_t to, u_int n)
{
	bcopy(from, to, n);
	return 0;
}

int
chown1(struct inode *ip, int uid, int gid)
{
	(void)ip;
	(void)uid;
	(void)gid;
	return 0;
}

int
chmod1(struct inode *ip, int mode)
{
	(void)ip;
	(void)mode;
	return 0;
}

struct file *
getf(int fd)
{
	(void)fd;
	return NULL;
}

struct inode *
getinode(int fd)
{
	(void)fd;
	return NULL;
}

/*
 * Pathnames. namei() resolves a name from this table and returns the inode
 * with its reference count raised and the lock held, as the kernel's does.
 */
struct pathent {
	const char *name;
	struct inode *ip;
};
static struct pathent paths[8];
static unsigned npaths;

static struct inode *
new_inode(int mode, dev_t rdev)
{
	static ino_t next = 10;
	struct inode *ip;

	for (ip = inode; ip < &inode[NINODE]; ip++)
		if (ip->i_count == 0 && ip->i_number == 0)
			break;
	HK_CHECK(ip < &inode[NINODE]);
	bzero(ip, sizeof(*ip));
	ip->i_number = next++;
	ip->i_dev = rootdev;
	ip->i_fs = &mount[0].m_filsys;
	ip->i_mode = mode;
	ip->i_nlink = 1;
	ip->i_rdev = rdev;
	return ip;
}

static void
add_path(const char *name, struct inode *ip)
{
	HK_CHECK(npaths < sizeof(paths) / sizeof(paths[0]));
	paths[npaths].name = name;
	paths[npaths].ip = ip;
	npaths++;
}

struct inode *
namei(struct nameidata *ndp)
{
	unsigned i;

	for (i = 0; i < npaths; i++)
		if (hk_streq(paths[i].name, ndp->ni_dirp)) {
			paths[i].ip->i_count++;
			paths[i].ip->i_flag |= ILOCKED;
			return paths[i].ip;
		}
	u.u_error = ENOENT;
	return NULL;
}

/*
 * The disks. A device is 64 blocks of DEV_BSIZE with a table of extents in
 * 512-byte sectors, entry 0 the whole device. Writes land in the blocks and
 * in a log.
 */
#define NDEVBLK		64
#define NLOG		256

enum ioctl_mode {
	IOCTL_FULL,		/* DIOCGETPART and DIOCMEDIAGONE answered */
	IOCTL_EINVAL,		/* unknown commands fail as flioctl's do */
	IOCTL_EIO		/* every command fails as noioctl does */
};

struct write_rec {
	int blkno;
	int replaced;		/* the media was gone at this write */
	unsigned generation;	/* which medium received it */
};

struct fakedisk {
	unsigned char blocks[NDEVBLK][DEV_BSIZE];
	struct diskpart part[8];
	u_int openmask;
	int check_open;		/* strategy refuses an unopened partition */
	int replaced;		/* the removal flag the driver keeps */
	int fail_writes;	/* a write error on media that is present */
	enum ioctl_mode mode;
	unsigned generation;	/* counts media inserted */
	unsigned nopen, nclose;
	unsigned nwrites;
	struct write_rec log[NLOG];
};

static struct fakedisk sd_disk, xd_disk, fl_disk;

static void
write_super(unsigned char *block, unsigned seed)
{
	struct fs fs;

	bzero(&fs, sizeof(fs));
	fs.fs_magic1 = FSMAGIC1;
	fs.fs_magic2 = FSMAGIC2;
	fs.fs_fsize = 40;
	fs.fs_isize = 4;
	fs.fs_tfree = (int)seed;
	bcopy(&fs, block, sizeof(fs));
}

static void
disk_reset(struct fakedisk *d, int check_open, enum ioctl_mode mode)
{
	bzero(d, sizeof(*d));
	d->check_open = check_open;
	d->mode = mode;
	d->generation = 1;
	d->part[0].dp_nsectors = NDEVBLK * 2;
}

static void
disk_insert(struct fakedisk *d)
{
	d->generation++;
	d->nwrites = 0;
	d->replaced = 0;
}

static int
disk_open(struct fakedisk *d, dev_t dev, int part_max)
{
	int part = minor(dev) & 7;

	if (part > part_max || d->part[part].dp_nsectors == 0)
		return ENXIO;
	if (d->replaced && d->openmask != 0)
		return EIO;
	d->openmask |= 1U << part;
	d->nopen++;
	return 0;
}

static int
disk_close(struct fakedisk *d, dev_t dev)
{
	d->openmask &= ~(1U << (minor(dev) & 7));
	d->nclose++;
	if (d->openmask == 0)
		d->replaced = 0;
	return 0;
}

static void
disk_strategy(struct fakedisk *d, struct buf *bp)
{
	int part = minor(bp->b_dev) & 7;
	u_int first = d->part[part].dp_offset / 2 + (u_int)bp->b_blkno;
	int write = (bp->b_flags & B_READ) == 0;

	bp->b_resid = 0;
	if (d->check_open && (d->openmask & (1U << part)) == 0) {
		bp->b_error = ENXIO;
		goto bad;
	}
	if (d->replaced || (write && d->fail_writes)) {
		if (write && d->nwrites < NLOG) {
			d->log[d->nwrites].blkno = bp->b_blkno;
			d->log[d->nwrites].replaced = d->replaced;
			d->log[d->nwrites].generation = d->generation;
		}
		if (write)
			d->nwrites++;
		bp->b_error = EIO;
		goto bad;
	}
	if (first >= NDEVBLK) {
		bp->b_error = EINVAL;
		goto bad;
	}
	if (write) {
		if (d->nwrites < NLOG) {
			d->log[d->nwrites].blkno = bp->b_blkno;
			d->log[d->nwrites].replaced = 0;
			d->log[d->nwrites].generation = d->generation;
		}
		d->nwrites++;
		bcopy(bp->b_addr, d->blocks[first], DEV_BSIZE);
	} else
		bcopy(d->blocks[first], bp->b_addr, DEV_BSIZE);
	biodone(bp);
	return;
bad:
	bp->b_flags |= B_ERROR;
	biodone(bp);
}

static int
disk_ioctl(struct fakedisk *d, dev_t dev, u_int cmd, caddr_t addr,
    int part_max)
{
	int part = minor(dev) & 7;

	if (d->mode == IOCTL_EIO)
		return EIO;
	if (part > part_max)
		return ENXIO;
	switch (cmd) {
	case DIOCGETPART:
		*(struct diskpart *)addr = d->part[part];
		return 0;
	case DIOCMEDIAGONE:
		if (d->mode != IOCTL_FULL)
			return EINVAL;
		*(int *)addr = d->replaced;
		return 0;
	}
	return EINVAL;
}

/* sd, block major SD_BMAJOR and raw major SD_CMAJOR: unit 0 only. */
int
sdopen(dev_t dev, int flag, int mode)
{
	(void)flag;
	(void)mode;
	if ((minor(dev) >> 3) != 0)
		return ENXIO;
	return disk_open(&sd_disk, dev, MAXPARTITIONS);
}

int
sdclose(dev_t dev, int flag, int mode)
{
	(void)flag;
	(void)mode;
	return disk_close(&sd_disk, dev);
}

void
sdstrategy(struct buf *bp)
{
	disk_strategy(&sd_disk, bp);
}

daddr_t
sdsize(dev_t dev)
{
	return sd_disk.part[minor(dev) & 7].dp_nsectors >> 1;
}

int
sdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	(void)flag;
	if ((minor(dev) >> 3) != 0)
		return ENXIO;
	return disk_ioctl(&sd_disk, dev, cmd, addr, MAXPARTITIONS);
}

int
sdread(dev_t dev, struct uio *uio, int flag)
{
	(void)dev;
	(void)uio;
	(void)flag;
	return ENODEV;
}

int
sdwrite(dev_t dev, struct uio *uio, int flag)
{
	(void)dev;
	(void)uio;
	(void)flag;
	return ENODEV;
}

void
sd_detect_poll(void)
{
}

/*
 * xd stands for a block driver the kernel's overlap rule knows nothing
 * about: its ioctl fails every command, and its minors are unrelated nodes.
 */
#define XD_MAJOR	3

static int
xdopen(dev_t dev, int flag, int mode)
{
	(void)flag;
	(void)mode;
	return disk_open(&xd_disk, dev, 7);
}

static int
xdclose(dev_t dev, int flag, int mode)
{
	(void)flag;
	(void)mode;
	return disk_close(&xd_disk, dev);
}

static void
xdstrategy(struct buf *bp)
{
	disk_strategy(&xd_disk, bp);
}

static daddr_t
xdsize(dev_t dev)
{
	(void)dev;
	return NDEVBLK;
}

static int
xdioctl(dev_t dev, u_int cmd, caddr_t addr, int flag)
{
	(void)flag;
	return disk_ioctl(&xd_disk, dev, cmd, addr, 7);
}

/*
 * fl: the production partition state and entry points, extracted from
 * flash.c. The table is loaded from a fixed layout in place of the MBR read
 * through Dhara; flstrategy keeps the production driver's property of never
 * consulting the open mask.
 */
static int fl_setup(int unit);
static int fl_sync(void);

#ifndef UFS_DISK_FLASH_INCLUDE
#define UFS_DISK_FLASH_INCLUDE "ufs_disk_flash_functions.c"
#endif
#include UFS_DISK_FLASH_INCLUDE

static unsigned fl_setup_calls;

static int
fl_setup(int unit)
{
	struct fldisk *du = &fldrives[unit];

	fl_setup_calls++;
	bzero(du->part, sizeof(du->part));
	du->part[RAWPART].dp_nsectors = NDEVBLK * 2;
	if (unit == FL_UNIT_FS) {
		du->part[1].dp_offset = 2;
		du->part[1].dp_nsectors = 100;
	}
	return 0;
}

static int
fl_sync(void)
{
	return 0;
}

void
flstrategy(struct buf *bp)
{
	int unit = minor(bp->b_dev) >> 3;
	struct fldisk *du = &fldrives[unit];
	int part = minor(bp->b_dev) & 7;
	u_int first;

	if (unit >= NFL || du->part[RAWPART].dp_nsectors == 0)
		fl_setup(unit);
	first = du->part[part].dp_offset / 2 + (u_int)bp->b_blkno;
	bp->b_resid = 0;
	if (first >= NDEVBLK) {
		bp->b_error = EINVAL;
		bp->b_flags |= B_ERROR;
		biodone(bp);
		return;
	}
	if (bp->b_flags & B_READ)
		bcopy(fl_disk.blocks[first], bp->b_addr, DEV_BSIZE);
	else {
		fl_disk.nwrites++;
		bcopy(bp->b_addr, fl_disk.blocks[first], DEV_BSIZE);
	}
	biodone(bp);
}

daddr_t
flsize(dev_t dev)
{
	return fldrives[minor(dev) >> 3].part[minor(dev) & 7].dp_nsectors >> 1;
}

const struct bdevsw bdevsw[] = {
	[0] = { flopen, flclose, flstrategy, flsize, flioctl, 0 },
	[XD_MAJOR] = { xdopen, xdclose, xdstrategy, xdsize, xdioctl, 0 },
	[SD_BMAJOR] = { sdopen, sdclose, sdstrategy, sdsize, sdioctl, 0 },
	{ 0 },
};
const int nblkdev = sizeof(bdevsw) / sizeof(bdevsw[0]) - 1;

const struct cdevsw cdevsw[] = {
	[SD_CMAJOR] = { .d_open = sdopen, .d_close = sdclose },
	{ 0 },
};
const int nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]) - 1;

/* isdisk, chrtoblk, iskmemdev and blkoverlap as sys/arch/rp2040/rp2040/conf.c */
#ifndef UFS_DISK_CONF_INCLUDE
#define UFS_DISK_CONF_INCLUDE "ufs_disk_conf_functions.c"
#endif
#include UFS_DISK_CONF_INCLUDE

/*
 * Descriptors. open_node() makes a special-file inode, runs openi() on it as
 * vn_open() does and gives the result a file table entry; close_node() is
 * closef() for it. A node the kernel refuses leaves no reference behind.
 */
struct node {
	struct file *fp;
	struct inode *ip;
};

static int
open_node(struct node *n, int mode, dev_t dev, int flag)
{
	struct file *fp;
	int error;

	for (fp = file; fp < &file[NFILE]; fp++)
		if (fp->f_count == 0)
			break;
	HK_CHECK(fp < &file[NFILE]);
	n->ip = new_inode(mode, dev);
	n->ip->i_count = 1;
	error = openi(n->ip, flag);
	if (error) {
		n->ip->i_count = 0;
		n->ip->i_number = 0;
		n->fp = NULL;
		return error;
	}
	fp->f_count = 1;
	fp->f_type = DTYPE_INODE;
	fp->f_flag = flag;
	fp->f_data = (caddr_t)n->ip;
	n->fp = fp;
	return 0;
}

static void
close_node(struct node *n)
{
	struct inode *ip = n->ip;

	HK_CHECK(n->fp != NULL);
	vn_closefile(n->fp);
	n->fp->f_count = 0;
	n->fp = NULL;
	if (ip->i_count == 0)
		ip->i_number = 0;
}

/* Read a block of the device through the buffer cache. */
static int
read_blkno(dev_t dev, daddr_t blkno)
{
	struct buf *bp = bread(dev, blkno);
	int error = (bp->b_flags & B_ERROR) ? (bp->b_error ? bp->b_error : EIO)
	    : 0;

	brelse(bp);
	return error;
}

static int
read_block(dev_t dev)
{
	return read_blkno(dev, 0);
}

static int
do_mount(const char *special, const char *dir, int flags)
{
	u.u_error = 0;
	u.u_arg[0] = (int)special;
	u.u_arg[1] = (int)dir;
	u.u_arg[2] = flags;
	smount();
	return u.u_error;
}

static int
do_umount(const char *special)
{
	u.u_error = 0;
	u.u_arg[0] = (int)special;
	umount();
	return u.u_error;
}

static struct mount *
mount_of(dev_t dev)
{
	struct mount *mp;

	for (mp = mount; mp < &mount[NMOUNT]; mp++)
		if (mp->m_inodp != NULL && mp->m_dev == dev)
			return mp;
	return NULL;
}

/* The mount of dev; a missing one fails the check and reads as all zeros. */
static struct mount *
must_mount(dev_t dev)
{
	static struct mount absent;
	struct mount *mp = mount_of(dev);

	HK_CHECK(mp != NULL);
	if (mp == NULL) {
		bzero(&absent, sizeof(absent));
		return &absent;
	}
	return mp;
}

static unsigned
valid_buffers(dev_t dev)
{
	unsigned n = 0;
	struct buf *bp;

	for (bp = buf; bp < &buf[NBUF]; bp++)
		if (bp->b_dev == dev && (bp->b_flags & B_INVAL) == 0)
			n++;
	return n;
}

static unsigned
delayed_buffers(dev_t dev)
{
	unsigned n = 0;
	struct buf *bp;

	for (bp = buf; bp < &buf[NBUF]; bp++)
		if (bp->b_dev == dev && (bp->b_flags & B_DELWRI) != 0)
			n++;
	return n;
}

#define SD_A	makedev(SD_BMAJOR, 1)
#define SD_B	makedev(SD_BMAJOR, 2)
#define SD_C	makedev(SD_BMAJOR, 3)
#define SD_WHOLE	makedev(SD_BMAJOR, 0)
#define RSD_WHOLE	makedev(SD_CMAJOR, 0)
#define RSD_A	makedev(SD_CMAJOR, 1)
#define RSD_B	makedev(SD_CMAJOR, 2)
#define RSD_C	makedev(SD_CMAJOR, 3)
#define FL_WHOLE	makedev(0, 0)
#define FL_ROOT	makedev(0, 1)
#define FL_SWAP	makedev(0, 8)

static struct inode *mntdir;

/* binit() of sys/kern/init_main.c, which keeps it static. */
static void
init_buffers(void)
{
	struct buf *bp;
	int i;

	for (bp = bfreelist; bp < &bfreelist[BQUEUES]; bp++)
		bp->av_forw = bp->av_back = bp;
	for (i = 0; i < NBUF; i++) {
		bp = &buf[i];
		bp->b_dev = NODEV;
		bp->b_bcount = 0;
		bp->b_addr = bufdata + MAXBSIZE * i;
		bp->b_flags = B_BUSY | B_INVAL;
		brelse(bp);
	}
}

/*
 * A fresh machine: the flash root mounted as init_main() mounts it, an sd
 * card whose partitions a (sectors 2 to 61, holding a filesystem), b
 * (62 to 121) and c (40 to 79, overlapping both) are written, one directory
 * to mount on, and the nodes the tests name.
 */
static void
boot_machine(void)
{
	struct fs *fs;
	unsigned i;

	bzero(mount, sizeof(mount));
	bzero(inode, sizeof(inode));
	bzero(file, sizeof(file));
	bzero(buf, sizeof(buf));
	bzero(fldrives, sizeof(fldrives));
	securelevel = 0;
	npaths = 0;
	fl_setup_calls = 0;
	hk_reset_output();

	disk_reset(&sd_disk, 1, IOCTL_FULL);
	sd_disk.part[1].dp_offset = 2;
	sd_disk.part[1].dp_nsectors = 60;
	sd_disk.part[2].dp_offset = 62;
	sd_disk.part[2].dp_nsectors = 60;
	sd_disk.part[3].dp_offset = 40;
	sd_disk.part[3].dp_nsectors = 40;
	write_super(sd_disk.blocks[1], 1001);
	write_super(sd_disk.blocks[31], 1002);
	write_super(sd_disk.blocks[20], 1003);
	disk_reset(&xd_disk, 0, IOCTL_EIO);
	for (i = 1; i < 8; i++)
		xd_disk.part[i].dp_nsectors = NDEVBLK * 2;
	write_super(xd_disk.blocks[0], 2001);
	disk_reset(&fl_disk, 0, IOCTL_FULL);
	write_super(fl_disk.blocks[1], 3001);

	init_buffers();

	rootdev = FL_ROOT;
	fs = mountfs(FL_ROOT, MNT_NOATIME, 0);
	HK_CHECK(fs != NULL);
	/* The swap unit is set up once swapping starts; it holds no partition. */
	fl_setup(FL_UNIT_SWAP);
	mount[0].m_inodp = (struct inode *)1;
	mntdir = new_inode(IFDIR | 0755, 0);
	mntdir->i_dev = FL_ROOT;
	add_path("/mnt", mntdir);
	add_path("/dev/sd0a", new_inode(IFBLK | 0600, SD_A));
	add_path("/dev/sd0b", new_inode(IFBLK | 0600, SD_B));
	add_path("/dev/sd0c", new_inode(IFBLK | 0600, SD_C));
	add_path("/dev/sd0", new_inode(IFBLK | 0600, SD_WHOLE));
	add_path("/dev/xd1", new_inode(IFBLK | 0600, makedev(XD_MAJOR, 1)));
	for (i = 1; i < npaths; i++) {
		paths[i].ip->i_nlink = 1;
		paths[i].ip->i_count = 0;
	}
	mntdir->i_count = 0;
	mntdir->i_flag = 0;
	/* The nodes sit in the table without a holder, as cached inodes do. */
	for (i = 1; i < npaths; i++)
		paths[i].ip->i_number = 100 + i;
}

/*
 * Mount and unmount keep a read-write filesystem on partition a open the way
 * a daily use does. Each case starts from boot_machine().
 */

static void
test_unmount_closes_last_user(void)
{
	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	HK_CHECK(must_mount(SD_A)->m_dev == SD_A);
	HK_CHECK(sd_disk.openmask == 2);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(mount_of(SD_A) == NULL);
	/* Nothing else names the device: the driver closes once. */
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
}

static void
test_unmount_keeps_open_descriptor(void)
{
	struct node n;

	boot_machine();
	HK_CHECK(open_node(&n, IFBLK | 0600, SD_A, FREAD) == 0);
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	/*
	 * The descriptor still names the partition, so the driver stays open
	 * and its read succeeds; closei() closes at the descriptor's close.
	 */
	HK_CHECK(sd_disk.nclose == 0);
	HK_CHECK(sd_disk.openmask == 2);
	HK_CHECK(read_block(SD_A) == 0);
	close_node(&n);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
}

static void
test_unmount_with_two_descriptors(void)
{
	struct node n1, n2;

	boot_machine();
	HK_CHECK(open_node(&n1, IFBLK | 0600, SD_A, FREAD) == 0);
	HK_CHECK(open_node(&n2, IFBLK | 0600, SD_A, FREAD) == 0);
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(sd_disk.nclose == 0);
	close_node(&n1);
	HK_CHECK(sd_disk.nclose == 0);
	HK_CHECK(sd_disk.openmask == 2);
	HK_CHECK(read_block(SD_A) == 0);
	close_node(&n2);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
}

static void
test_close_before_unmount(void)
{
	struct node n;

	boot_machine();
	HK_CHECK(open_node(&n, IFBLK | 0600, SD_A, FREAD) == 0);
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	/* closei() leaves a mounted device open; unmount closes it. */
	close_node(&n);
	HK_CHECK(sd_disk.nclose == 0);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
}

static void
test_failed_mount_closes_last_user(void)
{
	boot_machine();
	/* Partition b holds no filesystem. */
	bzero(sd_disk.blocks[31], DEV_BSIZE);
	HK_CHECK(do_mount("/dev/sd0b", "/mnt", 0) == EINVAL);
	HK_CHECK(mount_of(SD_B) == NULL);
	HK_CHECK(sd_disk.nopen == 1);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
}

static void
test_failed_mount_keeps_open_descriptor(void)
{
	struct node n;

	boot_machine();
	bzero(sd_disk.blocks[31], DEV_BSIZE);
	HK_CHECK(open_node(&n, IFBLK | 0600, SD_B, FREAD) == 0);
	HK_CHECK(do_mount("/dev/sd0b", "/mnt", 0) == EINVAL);
	HK_CHECK(sd_disk.nclose == 0);
	HK_CHECK(sd_disk.openmask == 4);
	HK_CHECK(read_block(SD_B) == 0);
	close_node(&n);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
}

/*
 * A card pulled under a read-write mount. The mount's superblock is marked
 * modified (mountfs sets fs_fmod), so ufs_sync() writes it back, the card
 * refuses, and unmount(2) sees an error. When the driver reports the media
 * gone the unmount completes without write-back, closes the driver and
 * leaves no buffer that could reach the next card.
 */
static void
pull_card(void)
{
	sd_disk.replaced = 1;
}

static void
test_media_gone_unmounts(void)
{
	struct mount *mp;
	unsigned i;

	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	mp = must_mount(SD_A);
	HK_CHECK(mp->m_filsys.fs_fmod == 1);
	/* A cached clean block of the old card, not the superblock. */
	HK_CHECK(read_blkno(SD_A, 3) == 0);
	HK_CHECK(valid_buffers(SD_A) != 0);
	pull_card();
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(mount_of(SD_A) == NULL);
	HK_CHECK(mount[1].m_inodp == NULL);
	HK_CHECK(mount[1].m_write_error == 0);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
	HK_CHECK(valid_buffers(SD_A) == 0);
	HK_CHECK(delayed_buffers(SD_A) == 0);

	/* A replacement card, with its own filesystem, mounts and reads. */
	disk_insert(&sd_disk);
	write_super(sd_disk.blocks[1], 4242);
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	mp = must_mount(SD_A);
	HK_CHECK(mp->m_filsys.fs_tfree == 4242);
	HK_CHECK(mp->m_write_error == 0);
	/* No write of the old mount reached the new card. */
	for (i = 0; i < sd_disk.nwrites && i < NLOG; i++)
		HK_CHECK(sd_disk.log[i].generation != sd_disk.generation);
	HK_CHECK(sd_disk.nwrites == 0);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
}

static void
test_media_gone_read_only(void)
{
	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", MNT_RDONLY) == 0);
	pull_card();
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
}

static void
test_media_gone_no_write_after_swap(void)
{
	struct buf *bp;

	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	/* A delayed write queued for the old card, then the card is pulled. */
	bp = getblk(SD_A, 5);
	bzero(bp->b_addr, DEV_BSIZE);
	bdwrite(bp);
	HK_CHECK(delayed_buffers(SD_A) == 1);
	pull_card();
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(delayed_buffers(SD_A) == 0);
	disk_insert(&sd_disk);
	/* Evicting every buffer must not push a stale block to the new card. */
	bflush(SD_A);
	HK_CHECK(sd_disk.nwrites == 0);
}

static void
test_write_error_on_present_media_is_fatal(void)
{
	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	sd_disk.fail_writes = 1;
	HK_CHECK(do_umount("/dev/sd0a") == EIO);
	HK_CHECK(mount_of(SD_A) != NULL);
	HK_CHECK(sd_disk.nclose == 0);
	/* The same error repeats, and clears once the writes succeed. */
	HK_CHECK(do_umount("/dev/sd0a") == EIO);
	sd_disk.fail_writes = 0;
	must_mount(SD_A)->m_write_error = 0;
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(sd_disk.nclose == 1);
}

static void
test_driver_without_query_keeps_failure(void)
{
	enum ioctl_mode modes[2] = { IOCTL_EINVAL, IOCTL_EIO };
	int i;

	for (i = 0; i < 2; i++) {
		boot_machine();
		sd_disk.mode = modes[i];
		HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
		pull_card();
		HK_CHECK(do_umount("/dev/sd0a") == EIO);
		HK_CHECK(mount_of(SD_A) != NULL);
		HK_CHECK(sd_disk.nclose == 0);
	}
}

static void
test_media_gone_with_open_file_is_busy(void)
{
	struct inode *ip;

	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	/* A file of the mounted filesystem held open. */
	ip = new_inode(IFREG | 0644, 0);
	ip->i_dev = SD_A;
	ip->i_fs = &must_mount(SD_A)->m_filsys;
	ip->i_count = 1;
	pull_card();
	HK_CHECK(do_umount("/dev/sd0a") == EBUSY);
	HK_CHECK(mount_of(SD_A) != NULL);
	HK_CHECK(sd_disk.nclose == 0);
	/* Once the file closes the unmount completes. */
	ip->i_count = 0;
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(sd_disk.nclose == 1);
}

static void
test_media_gone_with_open_descriptor_defers_close(void)
{
	struct node n;

	boot_machine();
	HK_CHECK(open_node(&n, IFBLK | 0600, SD_A, FREAD) == 0);
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	pull_card();
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(sd_disk.nclose == 0);
	close_node(&n);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.openmask == 0);
	/* The last close cleared the removal state: a new card opens. */
	disk_insert(&sd_disk);
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
}

/*
 * Securelevel 1 refuses a write open of a device whose storage a mounted
 * filesystem uses. The whole-device nodes and overlapping partitions share
 * sectors with the mounted one even though their device numbers differ.
 */
static int
try_open(dev_t dev, int mode, int flag)
{
	struct node n;
	int error = open_node(&n, mode, dev, flag);

	if (error == 0)
		close_node(&n);
	return error;
}

static void
test_overlap_sd(void)
{
	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	securelevel = 1;
	/* Exact device: refused before and after. */
	HK_CHECK(try_open(RSD_A, IFCHR | 0600, FWRITE) == EBUSY);
	HK_CHECK(try_open(SD_A, IFBLK | 0600, FREAD) == EBUSY);
	/* The whole card covers partition a. */
	HK_CHECK(try_open(RSD_WHOLE, IFCHR | 0600, FWRITE) == EBUSY);
	HK_CHECK(try_open(SD_WHOLE, IFBLK | 0600, FWRITE) == EBUSY);
	/* A read-only raw open writes nothing. */
	HK_CHECK(try_open(RSD_WHOLE, IFCHR | 0600, FREAD) == 0);
	/* Partition c overlaps a, partition b only touches it. */
	HK_CHECK(try_open(RSD_C, IFCHR | 0600, FWRITE) == EBUSY);
	HK_CHECK(try_open(SD_C, IFBLK | 0600, FWRITE) == EBUSY);
	HK_CHECK(try_open(RSD_B, IFCHR | 0600, FWRITE) == 0);
	HK_CHECK(try_open(SD_B, IFBLK | 0600, FWRITE) == 0);
	/* Securelevel 0 admits all of them. */
	securelevel = 0;
	HK_CHECK(try_open(RSD_WHOLE, IFCHR | 0600, FWRITE) == 0);
	HK_CHECK(try_open(RSD_C, IFCHR | 0600, FWRITE) == 0);
}

static void
test_overlap_sd_mbr_entries(void)
{
	boot_machine();
	HK_CHECK(do_mount("/dev/sd0c", "/mnt", 0) == 0);
	securelevel = 1;
	/* c is mounted; a (sectors 2 to 61) and b (62 to 121) both overlap it. */
	HK_CHECK(try_open(RSD_A, IFCHR | 0600, FWRITE) == EBUSY);
	HK_CHECK(try_open(RSD_B, IFCHR | 0600, FWRITE) == EBUSY);
	HK_CHECK(try_open(RSD_WHOLE, IFCHR | 0600, FWRITE) == EBUSY);
	HK_CHECK(try_open(RSD_C, IFCHR | 0600, FWRITE) == EBUSY);
	/* A partition table entry with no sectors holds no storage. */
	HK_CHECK(try_open(makedev(SD_CMAJOR, 4), IFCHR | 0600, FWRITE) ==
	    ENXIO);
}

static void
test_overlap_flash(void)
{
	boot_machine();
	securelevel = 1;
	/* The root fl0a is mounted; fl0 covers it. */
	HK_CHECK(try_open(FL_ROOT, IFBLK | 0600, FREAD) == EBUSY);
	HK_CHECK(try_open(FL_WHOLE, IFBLK | 0600, FREAD) == EBUSY);
	HK_CHECK(try_open(FL_WHOLE, IFBLK | 0600, FWRITE) == EBUSY);
	/* fl1, the raw swap region, is another unit. */
	HK_CHECK(try_open(FL_SWAP, IFBLK | 0600, FWRITE) == 0);
	/* Another partition of the table that does not overlap the root. */
	fldrives[0].part[2].dp_offset = 102;
	fldrives[0].part[2].dp_nsectors = 20;
	HK_CHECK(try_open(makedev(0, 2), IFBLK | 0600, FWRITE) == 0);
	fldrives[0].part[2].dp_offset = 100;
	HK_CHECK(try_open(makedev(0, 2), IFBLK | 0600, FWRITE) == EBUSY);
}

static void
test_overlap_unknown_major_is_exact(void)
{
	boot_machine();
	HK_CHECK(do_mount("/dev/xd1", "/mnt", 0) == 0);
	securelevel = 1;
	HK_CHECK(try_open(makedev(XD_MAJOR, 1), IFBLK | 0600, FWRITE) == EBUSY);
	HK_CHECK(try_open(makedev(XD_MAJOR, 0), IFBLK | 0600, FWRITE) == 0);
	/* The same minor in a known major is a different device. */
	HK_CHECK(try_open(SD_A, IFBLK | 0600, FWRITE) == 0);
}

static void
test_overlap_other_unit_and_majors(void)
{
	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	securelevel = 1;
	/* sd unit 1 does not exist; fl0 and sd0 share no storage. */
	HK_CHECK(try_open(makedev(SD_BMAJOR, 9), IFBLK | 0600, FWRITE) !=
	    0);
	HK_CHECK(blkoverlap(SD_A, FL_ROOT) == 0);
	HK_CHECK(blkoverlap(FL_ROOT, SD_A) == 0);
	HK_CHECK(blkoverlap(SD_A, SD_A) == 1);
	HK_CHECK(blkoverlap(SD_WHOLE, SD_A) == 1);
	HK_CHECK(blkoverlap(SD_A, SD_WHOLE) == 1);
	HK_CHECK(blkoverlap(SD_A, SD_B) == 0);
	HK_CHECK(blkoverlap(SD_B, SD_A) == 0);
	HK_CHECK(blkoverlap(SD_A, SD_C) == 1);
	HK_CHECK(blkoverlap(SD_C, SD_B) == 1);
	HK_CHECK(blkoverlap(makedev(SD_BMAJOR, 1), makedev(SD_BMAJOR, 9)) == 0);
	/* A node of the same unit that the driver cannot describe is refused. */
	HK_CHECK(blkoverlap(SD_A, makedev(SD_BMAJOR, 5)) == 1);
	HK_CHECK(try_open(makedev(SD_BMAJOR, 5), IFBLK | 0600, FWRITE) ==
	    EBUSY);
	/* Equal numbers overlap in every major, known or not. */
	HK_CHECK(blkoverlap(makedev(XD_MAJOR, 3), makedev(XD_MAJOR, 3)) == 1);
	HK_CHECK(blkoverlap(makedev(XD_MAJOR, 3), makedev(XD_MAJOR, 4)) == 0);
	HK_CHECK(blkoverlap(makedev(9, 1), makedev(9, 1)) == 1);
	HK_CHECK(blkoverlap(makedev(9, 1), makedev(9, 0)) == 0);
}

/*
 * The flash driver's DIOCREINIT forgets the table so that the next
 * transfer reads it again. The root filesystem on fl0a is addressed
 * through that table, so the request is refused while any partition other
 * than the whole-device node the caller holds is open.
 */
static void
test_flash_reinit(void)
{
	struct node n;
	unsigned calls;
	int part;

	boot_machine();
	calls = fl_setup_calls;
	/* The mounted root holds fl0a. */
	HK_CHECK(flioctl(FL_WHOLE, DIOCREINIT, NULL, 0) == EBUSY);
	HK_CHECK(fldrives[0].part[RAWPART].dp_nsectors == NDEVBLK * 2);
	HK_CHECK(read_block(FL_ROOT) == 0);
	HK_CHECK(fl_setup_calls == calls);
	HK_CHECK(flioctl(FL_ROOT, DIOCREINIT, NULL, 0) == EBUSY);

	/*
	 * A block descriptor on any one partition holds it, with the root's
	 * own hold released to isolate each.
	 */
	for (part = 1; part <= 4; part++) {
		boot_machine();
		fldrives[0].openpart = 0;
		HK_CHECK(open_node(&n, IFBLK | 0600, makedev(0, part), FREAD) ==
		    0);
		HK_CHECK(flioctl(FL_WHOLE, DIOCREINIT, NULL, 0) == EBUSY);
		close_node(&n);
		/* Partition 1 is the mounted root, which closei() keeps open. */
		if (part != 1)
			HK_CHECK(flioctl(FL_WHOLE, DIOCREINIT, NULL, 0) == 0);
	}
}

static void
test_flash_reinit_whole_device_caller(void)
{
	unsigned calls;

	boot_machine();
	/* No filesystem on fl0: the root slot is released, as when fdisk is run
	   with fl0a unmounted on a board that boots from elsewhere. */
	fldrives[0].openpart = 1U << RAWPART;
	calls = fl_setup_calls;
	HK_CHECK(flioctl(FL_WHOLE, DIOCREINIT, NULL, 0) == 0);
	HK_CHECK(fldrives[0].part[RAWPART].dp_nsectors == 0);
	/* The next transfer reads the table again. */
	fldrives[0].openpart = 0;
	{
		struct buf *bp = bread(FL_WHOLE, 4);

		brelse(bp);
	}
	HK_CHECK(fl_setup_calls == calls + 1);
	HK_CHECK(flioctl(FL_WHOLE, DIOCREINIT, NULL, 0) == 0);
}

int
main(void)
{
	test_unmount_closes_last_user();
	test_unmount_keeps_open_descriptor();
	test_unmount_with_two_descriptors();
	test_close_before_unmount();
	test_failed_mount_closes_last_user();
	test_failed_mount_keeps_open_descriptor();
	test_media_gone_unmounts();
	test_media_gone_read_only();
	test_media_gone_no_write_after_swap();
	test_write_error_on_present_media_is_fatal();
	test_driver_without_query_keeps_failure();
	test_media_gone_with_open_file_is_busy();
	test_media_gone_with_open_descriptor_defers_close();
	test_overlap_sd();
	test_overlap_sd_mbr_entries();
	test_overlap_flash();
	test_overlap_unknown_major_is_exact();
	test_overlap_other_unit_and_majors();
	test_flash_reinit();
	test_flash_reinit_whole_device_caller();
	return hk_verdict("ufs disk lifecycle");
}
