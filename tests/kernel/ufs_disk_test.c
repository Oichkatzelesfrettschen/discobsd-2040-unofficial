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
 * ufs_disk_conf_functions.c holds disk identity and overlap functions from
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
#include "ufs_disk_schedule.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
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
#include <sys/uio.h>

#include <rp2040/dev/sd.h>

_Static_assert(sizeof(void *) == 4, "the target is ILP32");

struct user u;
struct mount mount[NMOUNT];
struct inode inode[NINODE];
struct file file[NFILE];
const struct fileops *const Fops[] = { NULL, &inodeops };
struct buf buf[NBUF];
struct buf bfreelist[BQUEUES];
char bufdata[NBUF * MAXBSIZE];
struct timeval time;
int securelevel;
dev_t rootdev;
static void (*sleep_completion)(caddr_t);
static void (*driver_open_pause)(void), (*driver_close_pause)(void);
static caddr_t scheduler_wait_channels[2];
static unsigned scheduler_wakeups[2];
static int scheduled_open_mode;
static dev_t scheduled_open_device;

/* Kernel services the linked sources call and this file replaces. */
int
suser(void)
{
	if (u.u_uid == 0)
		return 1;
	u.u_error = EPERM;
	return 0;
}

static _Noreturn __attribute__((noinline)) void
jump_checkpoint(label_t *state)
{
	__builtin_longjmp((void *)state, 1);
}

void
wakeup(caddr_t chan)
{
	unsigned index;

	for (index = 0; index < 2; index++)
		if (scheduler_wait_channels[index] != NULL &&
		    scheduler_wait_channels[index] == chan)
			scheduler_wakeups[index]++;
}

void
sleep(caddr_t chan, int pri)
{
	(void)pri;
	if (sleep_completion != NULL) {
		sleep_completion(chan);
		return;
	}
	hk_fail(__FILE__, __LINE__, "kernel code slept");
	hk_escape();
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
	if ((unsigned)fd >= NOFILE || u.u_ofile[fd] == NULL) {
		u.u_error = EBADF;
		return NULL;
	}
	return u.u_ofile[fd];
}

struct inode *
getinode(int fd)
{
	struct file *descriptor = getf(fd);

	if (descriptor == NULL)
		return NULL;
	if (descriptor->f_type != DTYPE_INODE) {
		u.u_error = EINVAL;
		return NULL;
	}
	return (struct inode *)descriptor->f_data;
}

void
psignal(struct proc *process, int signal_number)
{
	(void)process;
	(void)signal_number;
	hk_fail(__FILE__, __LINE__, "unexpected process signal");
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
	int write_error;
	int defer_writes;
	int interrupt_open, interrupt_close;
	enum ioctl_mode mode;
	unsigned generation;	/* counts media inserted */
	unsigned nopen, nclose;
	unsigned nwrites;
	struct write_rec log[NLOG];
};

static struct fakedisk sd_disk, xd_disk, fl_disk;
static struct buf *deferred_write;
static struct inode *completion_holder;
static unsigned completed_transfers;
static int query_interrupt, publish_on_open;
static unsigned inherited_interrupts;
static void publish_queued_failure(void);

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

	if (driver_open_pause != NULL) {
		void (*pause)(void) = driver_open_pause;

		driver_open_pause = NULL;
		pause();
	}
	if (publish_on_open) {
		publish_on_open = 0;
		publish_queued_failure();
	}
	if (d->interrupt_open) {
		u.u_error = EINTR;
		jump_checkpoint(&u.u_qsave);
	}
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
	if (driver_close_pause != NULL) {
		void (*pause)(void) = driver_close_pause;

		driver_close_pause = NULL;
		pause();
	}
	d->openmask &= ~(1U << (minor(dev) & 7));
	d->nclose++;
	if (d->openmask == 0)
		d->replaced = 0;
	if (d->interrupt_close) {
		u.u_error = 0;
		jump_checkpoint(&u.u_qsave);
	}
	return 0;
}

static void
disk_strategy(struct fakedisk *d, struct buf *bp)
{
	int part = minor(bp->b_dev) & 7;
	u_int first = d->part[part].dp_offset / 2 + (u_int)bp->b_blkno;
	int write = (bp->b_flags & B_READ) == 0;

	if (write && d->defer_writes) {
		HK_CHECK(deferred_write == NULL);
		deferred_write = bp;
		return;
	}
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
		bp->b_error = d->write_error != 0 ? d->write_error : EIO;
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

static void
publish_queued_failure(void)
{
	struct buf *pending = deferred_write;

	HK_CHECK(pending != NULL);
	deferred_write = NULL;
	sd_disk.defer_writes = 0;
	inherited_interrupts++;
	disk_strategy(&sd_disk, pending);
}

int
ufs_disk_error_query(dev_t dev)
{
	int result = bdev_write_error(dev);

	if (query_interrupt) {
		query_interrupt = 0;
		if (hk_ipl == 0)
			publish_queued_failure();
		else
			publish_on_open = 1;
	}
	return result;
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

#ifndef UFS_DISK_STM32_INCLUDE
#define UFS_DISK_STM32_INCLUDE "ufs_disk_stm32_unit_functions.c"
#endif
#define blkdev_same_unit stm32_blkdev_same_unit
#include UFS_DISK_STM32_INCLUDE
#undef blkdev_same_unit

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
	label_t checkpoint = u.u_qsave;

	for (fp = file; fp < &file[NFILE]; fp++)
		if (fp->f_count == 0)
			break;
	HK_CHECK(fp < &file[NFILE]);
	n->ip = new_inode(mode, dev);
	n->ip->i_count = 1;
	error = openi(n->ip, flag);
	HK_CHECK(bcmp(&u.u_qsave, &checkpoint, sizeof(checkpoint)) == 0);
	u.u_qsave = checkpoint;
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

static int
close_node(struct node *n)
{
	struct inode *ip = n->ip;
	int error;
	label_t checkpoint = u.u_qsave;

	HK_CHECK(n->fp != NULL);
	error = closef(n->fp);
	HK_CHECK(bcmp(&u.u_qsave, &checkpoint, sizeof(checkpoint)) == 0);
	u.u_qsave = checkpoint;
	n->fp = NULL;
	if (ip->i_count == 0)
		ip->i_number = 0;
	return error;
}

void fsync(void);

static int
sync_node(struct node *node)
{
	const int descriptor = 3;

	u.u_ofile[descriptor] = node->fp;
	u.u_arg[0] = descriptor;
	u.u_error = 0;
	fsync();
	u.u_ofile[descriptor] = NULL;
	return u.u_error;
}

static int
write_cooked_byte(struct node *node, char value)
{
	struct iovec vector = { &value, 1 };
	struct uio transfer;
	int error;

	bzero(&transfer, sizeof(transfer));
	transfer.uio_iov = &vector;
	transfer.uio_iovcnt = 1;
	transfer.uio_offset = 5 * DEV_BSIZE + 1;
	transfer.uio_resid = 1;
	transfer.uio_rw = UIO_WRITE;
	u.u_error = 0;
	error = rwip(node->ip, &transfer, 0);
	HK_CHECK(transfer.uio_resid == 0);
	HK_CHECK(node->ip->i_size == 0);
	return error;
}

static int
write_cooked_block(struct node *node)
{
	char data[DEV_BSIZE];
	struct iovec vector = { data, sizeof(data) };
	struct uio transfer;

	bzero(data, sizeof(data));
	data[0] = 'Q';
	bzero(&transfer, sizeof(transfer));
	transfer.uio_iov = &vector;
	transfer.uio_iovcnt = 1;
	transfer.uio_offset = 5 * DEV_BSIZE;
	transfer.uio_resid = sizeof(data);
	transfer.uio_rw = UIO_WRITE;
	u.u_error = 0;
	return rwip(node->ip, &transfer, 0);
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
	sleep_completion = NULL;
	deferred_write = NULL;
	completion_holder = NULL;
	completed_transfers = 0;
	scheduled_open_mode = IFBLK | 0600;
	scheduled_open_device = SD_A;
	driver_open_pause = driver_close_pause = NULL;
	bzero(scheduler_wait_channels, sizeof(scheduler_wait_channels));
	bzero(scheduler_wakeups, sizeof(scheduler_wakeups));
	query_interrupt = publish_on_open = 0;
	inherited_interrupts = 0;
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
test_cooked_device_explicit_flush_control(void)
{
	struct node node;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	HK_CHECK(write_cooked_byte(&node, 'Q') == 0);
	HK_CHECK(delayed_buffers(SD_A) == 1);
	HK_CHECK(sd_disk.blocks[6][1] == 0);
	HK_CHECK(bflush(SD_A) == 0);
	HK_CHECK(delayed_buffers(SD_A) == 0);
	HK_CHECK(sd_disk.blocks[6][1] == 'Q');
	close_node(&node);
}

static void
test_cooked_device_fsync(void)
{
	struct node node;
	unsigned writes;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	writes = sd_disk.nwrites;
	HK_CHECK(write_cooked_byte(&node, 'Q') == 0);
	HK_CHECK(delayed_buffers(SD_A) == 1);
	HK_CHECK(sd_disk.nwrites == writes);
	HK_CHECK(sync_node(&node) == 0);
	HK_CHECK(delayed_buffers(SD_A) == 0);
	HK_CHECK(sd_disk.nwrites > writes);
	HK_CHECK(sd_disk.blocks[6][1] == 'Q');
	close_node(&node);
}

static void
test_cooked_same_device_fsync_control(void)
{
	struct node node;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, FL_ROOT, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	HK_CHECK(write_cooked_byte(&node, 'Q') == 0);
	HK_CHECK(delayed_buffers(FL_ROOT) == 1);
	HK_CHECK(sync_node(&node) == 0);
	HK_CHECK(delayed_buffers(FL_ROOT) == 0);
	HK_CHECK(fl_disk.blocks[6][1] == 'Q');
	close_node(&node);
}

static void
test_cooked_device_fsync_error(void)
{
	struct node node;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	HK_CHECK(write_cooked_byte(&node, 'Q') == 0);
	sd_disk.fail_writes = 1;
	HK_CHECK(sync_node(&node) == EIO);
	HK_CHECK(sd_disk.nwrites > 0);
	HK_CHECK(sd_disk.blocks[6][1] == 0);
	sd_disk.fail_writes = 0;
	close_node(&node);
}

static void
test_cooked_device_syncip_error_return(void)
{
	struct node node;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	HK_CHECK(write_cooked_byte(&node, 'Q') == 0);
	sd_disk.fail_writes = 1;
	HK_CHECK(syncip(node.ip) == EIO);
	HK_CHECK(u.u_error == EIO);
	sd_disk.fail_writes = 0;
	u.u_error = 0;
	close_node(&node);
}

static void
test_cooked_device_fsync_latched_error(void)
{
	struct node node;

	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	HK_CHECK(write_cooked_byte(&node, 'Q') == 0);
	sd_disk.fail_writes = 1;
	HK_CHECK(bflush(SD_A) == EIO);
	HK_CHECK(must_mount(SD_A)->m_write_error == EIO);
	sd_disk.fail_writes = 0;
	HK_CHECK(sync_node(&node) == EIO);
	close_node(&node);
	must_mount(SD_A)->m_write_error = 0;
	HK_CHECK(do_umount("/dev/sd0a") == 0);
}

static void
test_cooked_unmounted_async_error_lifetime(void)
{
	struct node original, alias, later, unrelated, reopened;

	boot_machine();
	HK_CHECK(open_node(&original, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	HK_CHECK(open_node(&alias, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	HK_CHECK(open_node(&unrelated, IFBLK | 0600, SD_B, FREAD | FWRITE) == 0);
	original.ip->i_number = 4;
	alias.ip->i_number = 5;
	unrelated.ip->i_number = 6;
	sd_disk.fail_writes = 1;
	HK_CHECK(write_cooked_block(&original) == 0);
	HK_CHECK(sd_disk.nwrites == 1);
	HK_CHECK(sd_disk.blocks[6][0] == 0);
	sd_disk.fail_writes = 0;
	HK_CHECK(sync_node(&original) == EIO);
	HK_CHECK(sync_node(&original) == EIO);
	HK_CHECK(sync_node(&alias) == EIO);
	HK_CHECK(sync_node(&unrelated) == 0);
	HK_CHECK(open_node(&later, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	later.ip->i_number = 7;
	HK_CHECK(sync_node(&later) == EIO);
	HK_CHECK(close_node(&original) == EIO);
	HK_CHECK(sync_node(&alias) == EIO);
	HK_CHECK(close_node(&alias) == EIO);
	HK_CHECK(write_cooked_byte(&later, 'R') == 0);
	HK_CHECK(sync_node(&later) == EIO);
	HK_CHECK(sd_disk.blocks[6][1] == 'R');
	HK_CHECK(close_node(&later) == EIO);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(open_node(&reopened, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	reopened.ip->i_number = 8;
	HK_CHECK(sync_node(&reopened) == 0);
	HK_CHECK(close_node(&reopened) == 0);
	HK_CHECK(close_node(&unrelated) == 0);
}

static void
complete_cooked_write(caddr_t channel)
{
	struct buf *pending = deferred_write;

	HK_CHECK(pending != NULL);
	HK_CHECK(channel == (caddr_t)pending);
	HK_CHECK((pending->b_flags & (B_BUSY | B_WANTED)) == (B_BUSY | B_WANTED));
	HK_CHECK(completion_holder->i_count > 0);
	HK_CHECK(sd_disk.nclose == 0);
	deferred_write = NULL;
	sd_disk.defer_writes = 0;
	completed_transfers++;
	disk_strategy(&sd_disk, pending);
}

static void
test_cooked_last_close_with_lookup_reference(void)
{
	struct node node;
	struct inode *held;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	held = node.ip;
	igrab(held);
	IUNLOCK(held);
	sd_disk.fail_writes = 1;
	HK_CHECK(write_cooked_block(&node) == 0);
	sd_disk.fail_writes = 0;
	HK_CHECK(close_node(&node) == EIO);
	HK_CHECK(held->i_count == 1);
	HK_CHECK(held->i_bdev_state == 0 && held->i_bdev_error == 0);
	igrab(held);
	IUNLOCK(held);
	HK_CHECK(openi(held, FREAD | FWRITE) == 0);
	HK_CHECK(syncip(held) == 0);
	HK_CHECK(vn_close(held, FREAD | FWRITE) == 0);
	HK_CHECK(held->i_count == 1);
	irele(held);
}

static void
test_raw_driver_interruption(void)
{
	struct node node;

	boot_machine();
	sd_disk.interrupt_open = 1;
	HK_CHECK(open_node(&node, IFCHR | 0600, RSD_A, FREAD | FWRITE) == EINTR);
	HK_CHECK(node.ip->i_bdev_state == 0);
	HK_CHECK(sd_disk.nopen == 0);
	sd_disk.interrupt_open = 0;
	HK_CHECK(open_node(&node, IFCHR | 0600, RSD_A, FREAD | FWRITE) == 0);
	sd_disk.interrupt_close = 1;
	HK_CHECK(close_node(&node) == EINTR);
	HK_CHECK(node.ip->i_bdev_state == 0);
	HK_CHECK(sd_disk.nclose == 1);
	sd_disk.interrupt_close = 0;
	HK_CHECK(open_node(&node, IFCHR | 0600, RSD_A, FREAD | FWRITE) == 0);
	HK_CHECK(close_node(&node) == 0);
}

static void
test_cooked_deferred_completion(void)
{
	struct node node;
	int closing, failing;

	for (closing = 0; closing <= 1; closing++) {
		for (failing = 0; failing <= 1; failing++) {
			boot_machine();
			HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
			node.ip->i_number = 4;
			sd_disk.defer_writes = 1;
			sd_disk.fail_writes = failing;
			HK_CHECK(write_cooked_block(&node) == 0);
			HK_CHECK(deferred_write != NULL);
			completion_holder = node.ip;
			sleep_completion = complete_cooked_write;
			HK_CHECK((closing ? close_node(&node) : sync_node(&node)) ==
			    (failing ? EIO : 0));
			HK_CHECK(completed_transfers == 1);
			HK_CHECK(deferred_write == NULL);
			HK_CHECK(sd_disk.blocks[6][0] == (failing ? 0 : 'Q'));
			/* Complete a transfer even when a mutant skips the wait. */
			if (deferred_write != NULL)
				complete_cooked_write((caddr_t)deferred_write);
			sleep_completion = NULL;
			sd_disk.fail_writes = 0;
			if (!closing)
				HK_CHECK(close_node(&node) == (failing ? EIO : 0));
			HK_CHECK(sd_disk.nclose == 1);
		}
	}
}

static void
test_cooked_first_error_and_alias_state(void)
{
	struct node node;
	struct inode *reused;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	HK_CHECK((node.ip->i_bdev_state & IBDEV_REFS) == 1);
	sd_disk.fail_writes = 1;
	sd_disk.write_error = ENOSPC;
	HK_CHECK(write_cooked_block(&node) == 0);
	HK_CHECK(node.ip->i_bdev_error == ENOSPC);
	sd_disk.write_error = EIO;
	HK_CHECK(write_cooked_block(&node) == 0);
	HK_CHECK(node.ip->i_bdev_error == ENOSPC);
	sd_disk.fail_writes = 0;
	HK_CHECK(sync_node(&node) == ENOSPC);
	reused = node.ip;
	HK_CHECK(close_node(&node) == ENOSPC);
	HK_CHECK(reused->i_bdev_state == 0);
	HK_CHECK(reused->i_bdev_error == 0);
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	HK_CHECK(node.ip == reused);
	HK_CHECK(node.ip->i_bdev_error == 0);
	HK_CHECK(close_node(&node) == 0);
}

static void
test_cooked_mount_inherits_error(void)
{
	struct node node;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	sd_disk.fail_writes = 1;
	sd_disk.write_error = ENOSPC;
	HK_CHECK(write_cooked_block(&node) == 0);
	sd_disk.fail_writes = 0;
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	HK_CHECK(must_mount(SD_A)->m_write_error == ENOSPC);
	HK_CHECK(close_node(&node) == ENOSPC);
	HK_CHECK(sd_disk.nclose == 0);
	HK_CHECK(do_umount("/dev/sd0a") == ENOSPC);
	/* Exercise the existing explicit inspection reset for present media. */
	must_mount(SD_A)->m_write_error = 0;
	HK_CHECK(do_umount("/dev/sd0a") == 0);
}

static struct user scheduled_users[3];
static int scheduled_priorities[3];
static struct node scheduled_node;
static int scheduled_close_result, scheduled_mount_result, mount_waited;

static void
switch_user_state(int previous, int destination)
{
	scheduled_users[previous < 0 ? 2 : previous] = u;
	scheduled_priorities[previous < 0 ? 2 : previous] = hk_ipl;
	u = scheduled_users[destination < 0 ? 2 : destination];
	hk_ipl = scheduled_priorities[destination < 0 ? 2 : destination];
}

static void
scheduled_close(void)
{
	scheduled_close_result = close_node(&scheduled_node);
}

static void
scheduled_mount(void)
{
	scheduled_mount_result = do_mount("/dev/sd0a", "/mnt", 0);
}

static void
mount_during_close_sleep(caddr_t channel)
{
	int current = disk_schedule_current();

	scheduler_wait_channels[current] = channel;
	if (current == 0) {
		HK_CHECK(channel == (caddr_t)deferred_write);
		disk_schedule_yield();
		complete_cooked_write(channel);
	} else {
		HK_CHECK(channel == (caddr_t)scheduled_node.ip);
		HK_CHECK(mount_of(SD_A) == NULL);
		HK_CHECK(sd_disk.nopen == 1);
		mount_waited++;
		disk_schedule_yield();
	}
	HK_CHECK(scheduler_wakeups[current] > 0);
	scheduler_wait_channels[current] = NULL;
}

static void
test_cooked_mount_waits_for_last_close(void)
{
	int schedule_result;

	boot_machine();
	HK_CHECK(open_node(&scheduled_node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	scheduled_node.ip->i_number = 4;
	sd_disk.defer_writes = 1;
	HK_CHECK(write_cooked_block(&scheduled_node) == 0);
	completion_holder = scheduled_node.ip;
	sleep_completion = mount_during_close_sleep;
	scheduled_users[0] = scheduled_users[1] = u;
	scheduled_priorities[0] = scheduled_priorities[1] = 0;
	mount_waited = 0;
	schedule_result = disk_schedule_run(scheduled_close, scheduled_mount, switch_user_state);
	HK_CHECK(schedule_result == 0);
	sleep_completion = NULL;
	if (schedule_result != 0) {
		boot_machine();
		return;
	}
	HK_CHECK(scheduled_close_result == 0);
	HK_CHECK(scheduled_mount_result == 0);
	HK_CHECK(mount_waited == 1);
	HK_CHECK(completed_transfers == 1);
	if (deferred_write != NULL)
		complete_cooked_write((caddr_t)deferred_write);
	HK_CHECK(sd_disk.nopen == 2);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK((sd_disk.openmask & (1U << 1)) != 0);
	HK_CHECK(mount_of(SD_A) != NULL);
	HK_CHECK(read_block(SD_A) == 0);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(sd_disk.nclose == 2);
	HK_CHECK(hk_ipl == 0);
}

static void
pause_device_driver(void)
{
	disk_schedule_yield();
}

static void
close_during_mount_sleep(caddr_t channel)
{
	struct inode *owner = (struct inode *)channel;

	HK_CHECK(disk_schedule_current() == 1);
	HK_CHECK(owner->i_rdev == SD_A);
	HK_CHECK((owner->i_bdev_state & IBDEV_LOCKED) != 0);
	HK_CHECK(mount_of(SD_A) == NULL);
	HK_CHECK(sd_disk.nopen == 1 && sd_disk.nclose == 0);
	scheduler_wait_channels[1] = channel;
	mount_waited++;
	disk_schedule_yield();
	HK_CHECK(scheduler_wakeups[1] > 0);
	scheduler_wait_channels[1] = NULL;
}

static void
test_cooked_close_waits_for_mount_publication(void)
{
	int schedule_result;

	boot_machine();
	HK_CHECK(open_node(&scheduled_node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	scheduled_node.ip->i_number = 4;
	scheduled_users[0] = scheduled_users[1] = u;
	scheduled_priorities[0] = scheduled_priorities[1] = 0;
	driver_open_pause = pause_device_driver;
	sleep_completion = close_during_mount_sleep;
	mount_waited = 0;
	schedule_result = disk_schedule_run(scheduled_mount, scheduled_close, switch_user_state);
	HK_CHECK(schedule_result == 0);
	sleep_completion = NULL;
	if (schedule_result != 0) {
		boot_machine();
		return;
	}
	HK_CHECK(scheduled_mount_result == 0 && scheduled_close_result == 0);
	HK_CHECK(mount_waited == 1);
	HK_CHECK(sd_disk.nopen == 2 && sd_disk.nclose == 0);
	HK_CHECK(mount_of(SD_A) != NULL);
	HK_CHECK(read_block(SD_A) == 0);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(hk_ipl == 0);
}

static int scheduled_open_result, scheduled_unmount_result;

static void
scheduled_unmount(void)
{
	scheduled_unmount_result = do_umount("/dev/sd0a");
}

static void
scheduled_open(void)
{
	scheduled_open_result = open_node(&scheduled_node, scheduled_open_mode, scheduled_open_device,
	    FREAD | FWRITE);
	if (scheduled_open_result == 0)
		scheduled_node.ip->i_number = 4;
}

static void
open_during_unmount_sleep(caddr_t channel)
{
	struct mount *owner = (struct mount *)channel;

	HK_CHECK(disk_schedule_current() == 1);
	HK_CHECK(owner == &mount[1]);
	HK_CHECK(owner->m_dev == SD_A && owner->m_device_lock == 1);
	HK_CHECK(owner->m_inodp == NULL);
	HK_CHECK(sd_disk.nopen == 1 && sd_disk.nclose == 0);
	scheduler_wait_channels[1] = channel;
	mount_waited++;
	disk_schedule_yield();
	HK_CHECK(scheduler_wakeups[1] > 0);
	scheduler_wait_channels[1] = NULL;
}

static void
test_cooked_open_waits_for_unmount_close(void)
{
	int schedule_result;

	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	scheduled_users[0] = scheduled_users[1] = u;
	scheduled_priorities[0] = scheduled_priorities[1] = 0;
	driver_close_pause = pause_device_driver;
	sleep_completion = open_during_unmount_sleep;
	mount_waited = 0;
	schedule_result = disk_schedule_run(scheduled_unmount, scheduled_open, switch_user_state);
	HK_CHECK(schedule_result == 0);
	sleep_completion = NULL;
	if (schedule_result != 0) {
		boot_machine();
		return;
	}
	HK_CHECK(scheduled_unmount_result == 0 && scheduled_open_result == 0);
	HK_CHECK(mount_waited == 1);
	HK_CHECK(sd_disk.nopen == 2 && sd_disk.nclose == 1);
	HK_CHECK(mount_of(SD_A) == NULL);
	HK_CHECK((sd_disk.openmask & (1U << 1)) != 0);
	HK_CHECK(read_block(SD_A) == 0);
	HK_CHECK(close_node(&scheduled_node) == 0);
	HK_CHECK(sd_disk.nclose == 2);
	HK_CHECK(hk_ipl == 0);
}

static int admission_securelevel;

static void
open_during_mount_sleep(caddr_t channel)
{
	struct inode *owner = (struct inode *)channel;

	HK_CHECK(disk_schedule_current() == 1);
	HK_CHECK(owner->i_rdev == SD_A);
	HK_CHECK((owner->i_bdev_state & IBDEV_LOCKED) != 0);
	HK_CHECK(mount_of(SD_A) == NULL);
	HK_CHECK(sd_disk.nopen == 0);
	scheduler_wait_channels[1] = channel;
	mount_waited++;
	disk_schedule_yield();
	HK_CHECK(scheduler_wakeups[1] > 0);
	scheduler_wait_channels[1] = NULL;
	securelevel = admission_securelevel;
}

static void
test_disk_open_rechecks_mount_admission(void)
{
	int schedule_result;
	unsigned index;
	const struct {
		const char *name;
		int mode;
		dev_t device;
		int expected, level;
	} cases[] = {
		{ "cooked partition", IFBLK, SD_A, EBUSY, 1 },
		{ "cooked whole device", IFBLK, SD_WHOLE, EBUSY, 1 },
		{ "raw partition", IFCHR, RSD_A, EBUSY, 1 },
		{ "raw whole device", IFCHR, RSD_WHOLE, EBUSY, 1 },
		{ "cooked adjacent partition", IFBLK, SD_B, 0, 1 },
		{ "cooked adjacent partition after policy raise", IFBLK, SD_B, EPERM, 2 },
		{ "raw adjacent partition after policy raise", IFCHR,
		    makedev(SD_CMAJOR, 2), EPERM, 2 },
	};

	for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		hk_note("mounted-device admission: %s", cases[index].name);
		boot_machine();
		securelevel = 1;
		admission_securelevel = cases[index].level;
		scheduled_open_mode = cases[index].mode | 0600;
		scheduled_open_device = cases[index].device;
		scheduled_users[0] = scheduled_users[1] = u;
		scheduled_priorities[0] = scheduled_priorities[1] = 0;
		driver_open_pause = pause_device_driver;
		sleep_completion = open_during_mount_sleep;
		mount_waited = 0;
		schedule_result = disk_schedule_run(scheduled_mount, scheduled_open, switch_user_state);
		HK_CHECK(schedule_result == 0);
		sleep_completion = NULL;
		if (schedule_result != 0) {
			boot_machine();
			return;
		}
		HK_CHECK(scheduled_mount_result == 0);
		HK_CHECK(scheduled_open_result == cases[index].expected);
		HK_CHECK(mount_waited == 1);
		HK_CHECK(sd_disk.nopen == (cases[index].expected == 0 ? 2U : 1U));
		HK_CHECK(do_umount("/dev/sd0a") == 0);
		if (scheduled_open_result == 0)
			close_node(&scheduled_node);
	}
}

static void
test_nested_checkpoint_control(void)
{
	label_t outer;
	volatile int stage = 0;

	if (setjmp(&u.u_qsave)) {
		HK_CHECK(stage == 2);
		return;
	}
	outer = u.u_qsave;
	if (setjmp(&u.u_qsave)) {
		HK_CHECK(stage == 1);
		stage = 2;
		u.u_qsave = outer;
		jump_checkpoint(&u.u_qsave);
	}
	stage = 1;
	jump_checkpoint(&u.u_qsave);
}

static void
test_cooked_interrupted_open(void)
{
	struct node original, interrupted, later;
	volatile int stage = 0;

	boot_machine();
	if (setjmp(&u.u_qsave)) {
		HK_CHECK(stage == 1);
		return;
	}
	HK_CHECK(open_node(&original, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	original.ip->i_number = 4;
	sd_disk.fail_writes = 1;
	HK_CHECK(write_cooked_block(&original) == 0);
	sd_disk.fail_writes = 0;
	sd_disk.interrupt_open = 1;
	HK_CHECK(open_node(&interrupted, IFBLK | 0600, SD_A, FREAD | FWRITE) == EINTR);
	HK_CHECK(sd_disk.nopen == 1);
	HK_CHECK((original.ip->i_bdev_state & IBDEV_REFS) == 1);
	HK_CHECK((interrupted.ip->i_bdev_state & (IBDEV_LOCKED | IBDEV_REFS)) == 0);
	HK_CHECK(original.ip->i_bdev_error == EIO);
	sd_disk.interrupt_open = 0;
	HK_CHECK(open_node(&later, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	later.ip->i_number = 5;
	HK_CHECK(sync_node(&later) == EIO);
	HK_CHECK(close_node(&original) == EIO);
	HK_CHECK(close_node(&later) == EIO);
	stage = 1;
	jump_checkpoint(&u.u_qsave);
}

static void
test_cooked_interrupted_close(void)
{
	struct node node;
	int failing;

	for (failing = 0; failing <= 1; failing++) {
		boot_machine();
		HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
		node.ip->i_number = 4;
		if (failing) {
			sd_disk.fail_writes = 1;
			HK_CHECK(write_cooked_block(&node) == 0);
			sd_disk.fail_writes = 0;
		}
		sd_disk.interrupt_close = 1;
		HK_CHECK(close_node(&node) == (failing ? EIO : EINTR));
		HK_CHECK(node.ip->i_count == 0);
		HK_CHECK(node.ip->i_bdev_state == 0);
		HK_CHECK(sd_disk.nclose == 1);
		sd_disk.interrupt_close = 0;
		HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
		HK_CHECK(close_node(&node) == 0);
	}
}

static void
test_cooked_same_inode_completion_during_open(void)
{
	struct node node;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	sd_disk.defer_writes = 1;
	sd_disk.fail_writes = 1;
	HK_CHECK(write_cooked_block(&node) == 0);
	igrab(node.ip);
	IUNLOCK(node.ip);
	query_interrupt = 1;
	HK_CHECK(openi(node.ip, FREAD | FWRITE) == 0);
	HK_CHECK(inherited_interrupts == 1);
	HK_CHECK(node.ip->i_bdev_error == EIO);
	if (deferred_write != NULL) {
		query_interrupt = publish_on_open = 0;
		publish_queued_failure();
	}
	HK_CHECK((node.ip->i_bdev_state & IBDEV_REFS) == 2);
	sd_disk.fail_writes = 0;
	HK_CHECK(sync_node(&node) == EIO);
	HK_CHECK(vn_close(node.ip, FREAD | FWRITE) == EIO);
	HK_CHECK(node.ip->i_count == 1);
	HK_CHECK(close_node(&node) == EIO);
}

void ufs_disk_dup(void);
void ufs_disk_close(void);

static void
test_cooked_duplicated_descriptor(void)
{
	struct node node;
	int duplicate_descriptor;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	node.ip->i_number = 4;
	u.u_ofile[3] = node.fp;
	u.u_arg[0] = 3;
	u.u_error = 0;
	ufs_disk_dup();
	duplicate_descriptor = u.u_rval;
	HK_CHECK(u.u_error == 0);
	HK_CHECK(duplicate_descriptor == 0);
	HK_CHECK(node.fp->f_count == 2);
	HK_CHECK(node.ip->i_count == 1);
	HK_CHECK((node.ip->i_bdev_state & IBDEV_REFS) == 1);
	sd_disk.fail_writes = 1;
	HK_CHECK(write_cooked_block(&node) == 0);
	sd_disk.fail_writes = 0;
	u.u_arg[0] = 3;
	ufs_disk_close();
	HK_CHECK(u.u_error == 0);
	HK_CHECK(node.fp->f_count == 1);
	HK_CHECK(node.ip->i_count == 1);
	HK_CHECK(sd_disk.nclose == 0);
	HK_CHECK(sync_node(&node) == EIO);
	u.u_arg[0] = duplicate_descriptor;
	u.u_error = 0;
	ufs_disk_close();
	HK_CHECK(u.u_error == EIO);
	HK_CHECK(node.fp->f_count == 0);
	HK_CHECK(node.ip->i_count == 0);
	HK_CHECK(sd_disk.nclose == 1);
	node.fp = NULL;
}

static void
test_cooked_open_admission_preserves_count(void)
{
	struct node node, failed;
	unsigned opens;

	boot_machine();
	HK_CHECK(open_node(&node, IFBLK | 0600, SD_A, FREAD | FWRITE) == 0);
	HK_CHECK(open_node(&failed, IFBLK | 0600, makedev(SD_BMAJOR, 5), FREAD) == ENXIO);
	HK_CHECK(failed.ip->i_bdev_state == 0);
	HK_CHECK((node.ip->i_bdev_state & IBDEV_REFS) == 1);
	opens = sd_disk.nopen;
	node.ip->i_bdev_state = IBDEV_REFS;
	HK_CHECK(openi(node.ip, FREAD) == ENFILE);
	HK_CHECK(sd_disk.nopen == opens);
	HK_CHECK(node.ip->i_bdev_state == IBDEV_REFS);
	node.ip->i_bdev_state = 1;
	HK_CHECK(close_node(&node) == 0);
}

static void
test_unmount_device_node_inside_its_filesystem(void)
{
	struct inode *device_node;
	int result;
	unsigned writes;

	boot_machine();
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	device_node = new_inode(IFBLK | 0600, SD_A);
	device_node->i_dev = SD_A;
	device_node->i_fs = &must_mount(SD_A)->m_filsys;
	device_node->i_number = 4;
	device_node->i_flag = IUPD;
	add_path("/mnt/device", device_node);
	writes = sd_disk.nwrites;
	result = do_umount("/mnt/device");
	HK_CHECK(result == 0);
	HK_CHECK(device_node->i_count == 0);
	HK_CHECK(mount_of(SD_A) == NULL);
	HK_CHECK(sd_disk.nclose == 1);
	HK_CHECK(sd_disk.nwrites > writes);
	if (result != 0)
		HK_CHECK(do_umount("/dev/sd0a") == 0);
}

static unsigned scheduler_visits[2], scheduler_caller_returns;

static void
scheduler_state_observer(int previous, int destination)
{
	(void)previous;
	if (destination < 0)
		scheduler_caller_returns++;
}

static void
scheduler_endless(void)
{
	for (;;)
		disk_schedule_yield();
}

static void
scheduler_first(void)
{
	scheduler_visits[0]++;
	disk_schedule_yield();
	scheduler_visits[0]++;
}

static void
scheduler_second(void)
{
	scheduler_visits[1]++;
	disk_schedule_yield();
	scheduler_visits[1]++;
}

static void
test_disk_scheduler_budget(void)
{
	scheduler_caller_returns = 0;
	HK_CHECK(disk_schedule_run(scheduler_endless, scheduler_endless,
	    scheduler_state_observer) == 1);
	HK_CHECK(disk_schedule_current() == -1);
	HK_CHECK(scheduler_caller_returns == 1);
	HK_CHECK(disk_schedule_run(scheduler_endless, scheduler_first,
	    scheduler_state_observer) == 1);
	HK_CHECK(scheduler_caller_returns == 2);
	scheduler_visits[0] = scheduler_visits[1] = 0;
	HK_CHECK(disk_schedule_run(scheduler_first, scheduler_second,
	    scheduler_state_observer) == 0);
	HK_CHECK(scheduler_visits[0] == 2 && scheduler_visits[1] == 2);
	HK_CHECK(scheduler_caller_returns == 3);
}

static unsigned
device_operation_locks(dev_t dev)
{
	struct inode *alias;
	struct mount *device_mount;
	unsigned locks = 0;

	for (alias = inode; alias < &inode[NINODE]; alias++)
		if (alias->i_count != 0 && (alias->i_mode & IFMT) == IFBLK &&
		    alias->i_rdev == dev && (alias->i_bdev_state & IBDEV_LOCKED))
			locks++;
	for (device_mount = mount; device_mount < &mount[NMOUNT]; device_mount++)
		if (device_mount->m_dev == dev && device_mount->m_device_lock)
			locks++;
	return locks;
}

static void
test_mount_device_lookup_errors(void)
{
	struct inode *invalid;

	boot_machine();
	u.u_uid = 1;
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == EPERM);
	HK_CHECK(do_umount("/dev/sd0a") == EPERM);
	u.u_uid = 0;
	HK_CHECK(do_mount("/dev/missing", "/mnt", 0) == ENODEV);
	HK_CHECK(do_umount("/dev/missing") == ENODEV);
	HK_CHECK(do_mount("/mnt", "/mnt", 0) == ENOTBLK);
	HK_CHECK(do_umount("/mnt") == ENOTBLK);
	invalid = new_inode(IFBLK | 0600, makedev(nblkdev, 0));
	add_path("/dev/invalid", invalid);
	HK_CHECK(do_mount("/dev/invalid", "/mnt", 0) == ENXIO);
	HK_CHECK(do_umount("/dev/invalid") == ENXIO);
	HK_CHECK(invalid->i_count == 0);
	HK_CHECK(device_operation_locks(SD_A) == 0);
	HK_CHECK(sd_disk.nopen == 0 && sd_disk.nclose == 0);
	HK_CHECK(mount_of(SD_A) == NULL);
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
}

static void
test_cooked_interrupted_mount_and_unmount(void)
{
	label_t checkpoint;

	boot_machine();
	checkpoint = u.u_qsave;
	sd_disk.interrupt_open = 1;
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == EINTR);
	HK_CHECK(bcmp(&u.u_qsave, &checkpoint, sizeof(checkpoint)) == 0);
	u.u_qsave = checkpoint;
	HK_CHECK(device_operation_locks(SD_A) == 0);
	HK_CHECK(mntdir->i_count == 0);
	HK_CHECK(mount_of(SD_A) == NULL);
	HK_CHECK(sd_disk.nopen == 0);
	sd_disk.interrupt_open = 0;
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	sd_disk.interrupt_close = 1;
	HK_CHECK(do_umount("/dev/sd0a") == EINTR);
	HK_CHECK(bcmp(&u.u_qsave, &checkpoint, sizeof(checkpoint)) == 0);
	u.u_qsave = checkpoint;
	HK_CHECK(device_operation_locks(SD_A) == 0);
	HK_CHECK(mount_of(SD_A) == NULL);
	HK_CHECK(sd_disk.openmask == 0);
	sd_disk.interrupt_close = 0;
	HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
	HK_CHECK(do_umount("/dev/sd0a") == 0);
	boot_machine();
	bzero(sd_disk.blocks[31], DEV_BSIZE);
	checkpoint = u.u_qsave;
	sd_disk.interrupt_close = 1;
	HK_CHECK(do_mount("/dev/sd0b", "/mnt", 0) == EINVAL);
	HK_CHECK(bcmp(&u.u_qsave, &checkpoint, sizeof(checkpoint)) == 0);
	u.u_qsave = checkpoint;
	HK_CHECK(device_operation_locks(SD_B) == 0);
	HK_CHECK(mntdir->i_count == 0);
	HK_CHECK(mount_of(SD_B) == NULL);
	HK_CHECK(sd_disk.nclose == 1);
	sd_disk.interrupt_close = 0;
	write_super(sd_disk.blocks[31], 1002);
	HK_CHECK(do_mount("/dev/sd0b", "/mnt", 0) == 0);
	HK_CHECK(do_umount("/dev/sd0b") == 0);
}

static void
complete_unmount_write(caddr_t channel)
{
	struct buf *pending = deferred_write;

	HK_CHECK(pending != NULL && channel == (caddr_t)pending);
	HK_CHECK((pending->b_flags & (B_BUSY | B_WANTED)) == (B_BUSY | B_WANTED));
	HK_CHECK(mount_of(SD_A) != NULL);
	HK_CHECK(mount[1].m_device_lock == 1);
	HK_CHECK(sd_disk.nclose == 0);
	deferred_write = NULL;
	completed_transfers++;
	disk_strategy(&sd_disk, pending);
}

static void
test_unmount_drains_busy_write(void)
{
	struct buf *pending;
	int failing, result;

	for (failing = 0; failing <= 1; failing++) {
		boot_machine();
		HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
		pending = getblk(SD_A, 5);
		bzero(pending->b_addr, DEV_BSIZE);
		pending->b_addr[0] = 'Q';
		sd_disk.defer_writes = 1;
		bawrite(pending);
		HK_CHECK(deferred_write == pending);
		sd_disk.defer_writes = 0;
		sd_disk.fail_writes = failing;
		sleep_completion = complete_unmount_write;
		result = do_umount("/dev/sd0a");
		sleep_completion = NULL;
		HK_CHECK(result == (failing ? EIO : 0));
		HK_CHECK(completed_transfers == 1);
		HK_CHECK(deferred_write == NULL);
		HK_CHECK(sd_disk.blocks[6][0] == (failing ? 0 : 'Q'));
		if (deferred_write != NULL)
			complete_unmount_write((caddr_t)deferred_write);
		if (failing) {
			HK_CHECK(mount_of(SD_A) != NULL);
			HK_CHECK(sd_disk.nclose == 0);
			sd_disk.fail_writes = 0;
			must_mount(SD_A)->m_write_error = 0;
			HK_CHECK(do_umount("/dev/sd0a") == 0);
		}
		HK_CHECK(sd_disk.nclose == 1);
	}
}

static void
test_regular_file_fsync_drains_busy_write(void)
{
	struct node node;
	char data[DEV_BSIZE];
	struct iovec vector;
	struct uio transfer;
	int large, failing;

	for (large = 0; large <= 1; large++) {
		for (failing = 0; failing <= 1; failing++) {
			boot_machine();
			HK_CHECK(do_mount("/dev/sd0a", "/mnt", 0) == 0);
			HK_CHECK(open_node(&node, IFREG | 0600, 0, FREAD | FWRITE) == 0);
			node.ip->i_dev = SD_A;
			node.ip->i_fs = &must_mount(SD_A)->m_filsys;
			node.ip->i_number = 4;
			node.ip->i_size = (large ? NBUF / 2 + 1 : 1) * DEV_BSIZE;
			node.ip->i_db[0] = 5;
			u.u_rlimit[RLIMIT_FSIZE].rlim_cur = RLIM_INFINITY;
			u.u_rlimit[RLIMIT_FSIZE].rlim_max = RLIM_INFINITY;
			bzero(data, sizeof(data));
			data[0] = 'Q';
			vector.iov_base = data;
			vector.iov_len = sizeof(data);
			bzero(&transfer, sizeof(transfer));
			transfer.uio_iov = &vector;
			transfer.uio_iovcnt = 1;
			transfer.uio_resid = sizeof(data);
			transfer.uio_rw = UIO_WRITE;
			u.u_error = 0;
			sd_disk.defer_writes = 1;
			HK_CHECK(rwip(node.ip, &transfer, 0) == 0);
			HK_CHECK(transfer.uio_resid == 0);
			HK_CHECK(deferred_write != NULL);
			sd_disk.defer_writes = 0;
			sd_disk.fail_writes = failing;
			completion_holder = node.ip;
			sleep_completion = complete_cooked_write;
			HK_CHECK(sync_node(&node) == (failing ? EIO : 0));
			sleep_completion = NULL;
			HK_CHECK(completed_transfers == 1);
			HK_CHECK(deferred_write == NULL);
			HK_CHECK(sd_disk.blocks[6][0] == (failing ? 0 : 'Q'));
			if (deferred_write != NULL)
				complete_cooked_write((caddr_t)deferred_write);
			sd_disk.fail_writes = 0;
			HK_CHECK(close_node(&node) == 0);
			must_mount(SD_A)->m_write_error = 0;
			HK_CHECK(do_umount("/dev/sd0a") == 0);
		}
	}
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
	HK_CHECK(stm32_blkdev_same_unit(makedev(0, 1), makedev(0, 2)) == 1);
	HK_CHECK(stm32_blkdev_same_unit(makedev(0, 1), makedev(0, 17)) == 1);
	HK_CHECK(stm32_blkdev_same_unit(makedev(0, 1), makedev(0, 9)) == 0);
	HK_CHECK(stm32_blkdev_same_unit(makedev(0, 1), makedev(4, 1)) == 0);
	HK_CHECK(stm32_blkdev_same_unit(makedev(4, 1), makedev(4, 2)) == 0);
	HK_CHECK(stm32_blkdev_same_unit(makedev(4, 1), makedev(4, 1)) == 1);
	HK_CHECK(blkdev_same_unit(SD_A, SD_B) == 1);
	HK_CHECK(blkdev_same_unit(SD_A, SD_WHOLE) == 1);
	HK_CHECK(blkdev_same_unit(SD_A, FL_ROOT) == 0);
	HK_CHECK(blkdev_same_unit(SD_A, makedev(SD_BMAJOR, 9)) == 0);
	HK_CHECK(blkdev_same_unit(makedev(XD_MAJOR, 3), makedev(XD_MAJOR, 4)) == 0);
	HK_CHECK(blkdev_same_unit(makedev(XD_MAJOR, 3), makedev(XD_MAJOR, 3)) == 1);
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
	test_nested_checkpoint_control();
	test_disk_scheduler_budget();
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
	test_cooked_device_explicit_flush_control();
	test_cooked_device_fsync();
	test_cooked_same_device_fsync_control();
	test_cooked_device_fsync_error();
	test_cooked_device_syncip_error_return();
	test_cooked_device_fsync_latched_error();
	test_cooked_unmounted_async_error_lifetime();
	test_cooked_last_close_with_lookup_reference();
	test_raw_driver_interruption();
	test_cooked_deferred_completion();
	test_cooked_first_error_and_alias_state();
	test_cooked_mount_inherits_error();
	test_cooked_mount_waits_for_last_close();
	test_cooked_close_waits_for_mount_publication();
	test_cooked_open_waits_for_unmount_close();
	test_disk_open_rechecks_mount_admission();
	test_cooked_interrupted_open();
	test_cooked_interrupted_close();
	test_cooked_same_inode_completion_during_open();
	test_cooked_duplicated_descriptor();
	test_cooked_open_admission_preserves_count();
	test_unmount_device_node_inside_its_filesystem();
	test_mount_device_lookup_errors();
	test_cooked_interrupted_mount_and_unmount();
	test_unmount_drains_busy_write();
	test_regular_file_fsync_drains_busy_write();
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
