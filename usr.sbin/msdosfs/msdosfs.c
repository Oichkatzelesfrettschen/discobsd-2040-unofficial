/*
 * msdosfs: read and write a FAT12, FAT16 or FAT32 file system held in an
 * image file, through the NetBSD msdosfs core in sys/fs/msdosfs, the makefs
 * forks in usr.sbin/makefs/msdos, sbin/newfs_msdos/mkfs_msdos.c and the
 * fixed buffer pool in msdosfs_buf.c.
 *
 *	msdosfs newfs image sectors [fat]
 *	msdosfs ls image [dir]
 *	msdosfs cat image path
 *	msdosfs get image path local
 *	msdosfs put image local path
 *	msdosfs mkdir image path
 *	msdosfs rm image path
 *	msdosfs df image
 *
 * newfs creates IMAGE with SECTORS 512-byte sectors and FAT type FAT (12,
 * 16 or 32; mkfs_msdos picks one from the size when FAT is absent). Paths
 * are absolute or relative to the root; lookups match long names as the
 * NetBSD kernel does and create long-name entries for names that are not
 * plain 8.3. rm removes a file or an empty directory. When the environment
 * holds SOURCE_DATE_EPOCH, every timestamp written and the cluster
 * allocator's starting point derive from it, so equal inputs produce equal
 * images.
 *
 * The buffer pool bounds the geometry: a volume whose sector is not 512
 * bytes, or whose cluster or FAT block exceeds MSDOSFS_BUFSIZE, is refused.
 *
 * Exit status: 0 on success, 1 when an operation fails, 2 on a usage error.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "makefs.h"
#include "ffs/buf.h"
#include <fs/msdosfs/bpb.h>
#include <fs/msdosfs/direntry.h>
#include <fs/msdosfs/denode.h>
#include <fs/msdosfs/msdosfsmount.h>
#include <fs/msdosfs/fat.h>
#include "msdos.h"
#include "mkfs_msdos.h"

static const char *program = "msdosfs";

static int
usage(void)
{
	fprintf(stderr,
	    "usage: %s newfs image sectors [fat]\n"
	    "       %s ls image [dir]\n"
	    "       %s cat image path\n"
	    "       %s get image path local\n"
	    "       %s put image local path\n"
	    "       %s mkdir image path\n"
	    "       %s rm image path\n"
	    "       %s df image\n",
	    program, program, program, program, program, program, program,
	    program);
	return 2;
}

static int
fail(const char *what, int error)
{
	fprintf(stderr, "%s: %s: %s\n", program, what, strerror(error));
	return 1;
}

/*
 * SOURCE_DATE_EPOCH as a time, or -1 when it is absent, malformed or past
 * what the host's time_t and unsigned long hold. DiscoBSD's libc has
 * strtoul and no strtoll.
 */
static long long
source_date_epoch(void)
{
	const char *text = getenv("SOURCE_DATE_EPOCH");
	char *end;
	unsigned long value;
	time_t as_time;

	if (text == NULL || *text < '0' || *text > '9')
		return -1;
	errno = 0;
	value = strtoul(text, &end, 10);
	/*
	 * The value as a time_t, which holds every unsigned long where
	 * time_t is wider and turns negative or changes where it is not.
	 */
	as_time = (time_t)value;
	if (errno != 0 || *end != '\0' || as_time < 0 ||
	    (unsigned long)as_time != value)
		return -1;
	return (long long)value;
}

/* The open image and its mounted volume. */
struct image {
	int fd;
	fsinfo_t fs;
	struct vnode devvp;
	struct vnode rootvp;
	struct msdos_options options;
	struct msdosfsmount *pmp;
	struct denode *root;
};

static int
image_open(struct image *im, const char *path, bool writing)
{
	struct msdosfsmount *pmp;
	int error;

	memset(im, 0, sizeof(*im));
	if ((im->fd = open(path, writing ? O_RDWR : O_RDONLY)) < 0)
		return errno;
	im->fs.fd = im->fd;
	im->fs.sectorsize = DEV_BSIZE;
	im->fs.fs_specific = &im->options;
	im->devvp.fs = &im->fs;
	if ((pmp = msdosfs_mount(&im->devvp, 0)) == NULL) {
		error = errno != 0 ? errno : EINVAL;
		close(im->fd);
		return error;
	}
	im->pmp = pmp;
	if (pmp->pm_BytesPerSec != DEV_BSIZE ||
	    pmp->pm_bpcluster > MSDOSFS_BUFSIZE ||
	    pmp->pm_fatblocksize > MSDOSFS_BUFSIZE) {
		free(pmp->pm_inusemap);
		free(pmp);
		close(im->fd);
		return EFBIG;
	}
	if (msdosfs_root(pmp, &im->rootvp) != 0) {
		error = errno;
		free(pmp->pm_inusemap);
		free(pmp);
		close(im->fd);
		return error;
	}
	im->root = im->rootvp.v_data;
	return 0;
}

static int
image_close(struct image *im, bool writing)
{
	int error = 0;

	/* A FAT write updatefats dropped still fails the command. */
	if (writing && (error = buf_write_error()) == 0 && fsync(im->fd) != 0)
		error = errno;
	free(im->root);
	free(im->pmp->pm_inusemap);
	free(im->pmp);
	if (close(im->fd) != 0 && error == 0)
		error = errno;
	return error;
}

/*
 * One pass over the slots of directory DP, in the order the kernel's
 * msdosfs_lookup reads them: FN sees each slot before the first that has
 * never been used, with its byte offset in the directory and the cluster
 * holding it (MSDOSFSROOT for the FAT12 and FAT16 root region). FN returns
 * 0 to continue, SCAN_STOP to end the pass with status 0, or an error.
 */
#define SCAN_STOP (-1)

typedef int (*slot_fn)(struct direntry *, u_long, u_long, void *);

static int
scan(struct denode *dp, slot_fn fn, void *arg)
{
	struct msdosfsmount *pmp = dp->de_pmp;
	struct buf *bp;
	u_long cluster, diroff = 0, frcn;
	daddr_t bn;
	int blkoff, blsize, error, status;

	for (frcn = 0; diroff < dp->de_FileSize; frcn++) {
		error = msdosfs_pcbmap(dp, frcn, &bn, &cluster, &blsize);
		if (error == E2BIG)
			return 0;
		if (error != 0)
			return error;
		if ((error = bread(pmp->pm_devvp, de_bn2kb(pmp, bn), blsize, 0,
		    &bp)) != 0)
			return error;
		for (blkoff = 0; blkoff < blsize && diroff < dp->de_FileSize;
		    blkoff += sizeof(struct direntry),
		    diroff += sizeof(struct direntry)) {
			struct direntry *dep =
			    (struct direntry *)((char *)bp->b_data + blkoff);

			if (dep->deName[0] == SLOT_EMPTY) {
				brelse(bp, 0);
				return 0;
			}
			if ((status = fn(dep, diroff, cluster, arg)) != 0) {
				brelse(bp, 0);
				return status == SCAN_STOP ? 0 : status;
			}
		}
		brelse(bp, 0);
	}
	return 0;
}

/* Where a lookup found its entry, in the terms msdosfs_deget takes. */
struct found {
	u_long diroff;		/* the short entry's offset in the directory */
	u_long dirclust;	/* the cluster that holds it */
	u_long clustoff;	/* its offset in that cluster (root: diroff) */
	u_int8_t attributes;
	u_long start;		/* first cluster, high half included */
};

struct lookup {
	struct msdosfsmount *pmp;
	bool root_region;
	const char *name;
	int namelen;
	u_char dosname[12];
	bool olddos;
	int chksum;
	bool found;
	struct found where;
};

/*
 * The kernel's match rule, as usr.sbin/makefs/msdos's msdosfs_findslot
 * applies it: a long-name chain whose checksum matches the short entry,
 * or a short entry equal to the name's 8.3 form.
 */
static int
lookup_slot(struct direntry *dep, u_long diroff, u_long cluster, void *arg)
{
	struct lookup *l = arg;
	struct msdosfsmount *pmp = l->pmp;

	if (dep->deName[0] == SLOT_DELETED) {
		l->chksum = -1;
		return 0;
	}
	if (dep->deAttributes == ATTR_WIN95) {
		if (!(pmp->pm_flags & MSDOSFSMNT_SHORTNAME))
			l->chksum = msdosfs_winChkName(
			    (const u_char *)l->name, l->namelen,
			    (struct winentry *)dep, l->chksum,
			    pmp->pm_flags & MSDOSFSMNT_UTF8);
		return 0;
	}
	if (dep->deAttributes & ATTR_VOLUME) {
		l->chksum = -1;
		return 0;
	}
	if (l->chksum != msdosfs_winChksum(dep->deName) &&
	    (!l->olddos || memcmp(l->dosname, dep->deName, 11) != 0)) {
		l->chksum = -1;
		return 0;
	}
	l->found = true;
	l->where.diroff = diroff;
	l->where.dirclust = cluster;
	l->where.clustoff = l->root_region ? diroff : diroff & pmp->pm_crbomask;
	l->where.attributes = dep->deAttributes;
	l->where.start = getushort(dep->deStartCluster);
	if (FAT32(pmp))
		l->where.start |= (u_long)getushort(dep->deHighClust) << 16;
	return SCAN_STOP;
}

static int
find(struct denode *dp, const char *name, size_t namelen, struct found *out)
{
	struct lookup l;

	if (!(dp->de_Attributes & ATTR_DIRECTORY))
		return ENOTDIR;
	if (namelen == 0 || namelen > WIN_MAXLEN)
		return ENAMETOOLONG;
	memset(&l, 0, sizeof(l));
	l.pmp = dp->de_pmp;
	l.root_region = dp->de_StartCluster == MSDOSFSROOT;
	l.name = name;
	l.namelen = (int)namelen;
	l.chksum = -1;
	l.olddos = true;
	switch (msdosfs_unix2dosfn((const u_char *)name, l.dosname,
	    (int)namelen, 0)) {
	case 0:
		return EINVAL;
	case 3:
		l.olddos = false;
		break;
	default:
		break;
	}
	{
		int error = scan(dp, lookup_slot, &l);

		if (error != 0)
			return error;
	}
	if (!l.found)
		return ENOENT;
	*out = l.where;
	return 0;
}

/*
 * The denode for an entry FIND returned. A directory entry whose first
 * cluster is 0 names the root (the ".." of a top-level directory), which
 * NetBSD's deget would read as an empty directory, so the root's own
 * denode serves it.
 */
static int
get_node(struct image *im, const struct found *f, struct denode **out)
{
	int error;

	if ((f->attributes & ATTR_DIRECTORY) && (f->start == 0 ||
	    (FAT32(im->pmp) && f->start == im->pmp->pm_rootdirblk))) {
		*out = im->root;
		return 0;
	}
	if ((error = msdosfs_deget(im->pmp, f->dirclust, f->clustoff, out)) != 0)
		return error;
	return 0;
}

static void
put_node(struct image *im, struct denode *dep)
{
	if (dep != im->root)
		free(dep);
}

/*
 * Walk PATH from the root. On success *dir holds the directory that
 * contains the last component, which LEAF (WIN_MAXLEN bytes and a NUL)
 * receives; an empty path or "/" names the root itself, with LEAF empty.
 * The caller releases *dir with put_node.
 */
static int
walk(struct image *im, const char *path, struct denode **dir, char *leaf)
{
	struct denode *cur = im->root, *next;
	struct found f;
	const char *p = path, *end;
	size_t len;
	int error;

	leaf[0] = '\0';
	for (;;) {
		while (*p == '/')
			p++;
		if (*p == '\0')
			break;
		for (end = p; *end != '\0' && *end != '/'; end++)
			;
		len = (size_t)(end - p);
		if (len > WIN_MAXLEN) {
			put_node(im, cur);
			return ENAMETOOLONG;
		}
		while (*end == '/')
			end++;
		if (*end == '\0') {
			memcpy(leaf, p, len);
			leaf[len] = '\0';
			break;
		}
		if ((error = find(cur, p, len, &f)) != 0) {
			put_node(im, cur);
			return error;
		}
		if (!(f.attributes & ATTR_DIRECTORY)) {
			put_node(im, cur);
			return ENOTDIR;
		}
		error = get_node(im, &f, &next);
		put_node(im, cur);
		if (error != 0)
			return error;
		cur = next;
		p = end;
	}
	*dir = cur;
	return 0;
}

/* The node PATH names; the caller releases it with put_node. */
static int
lookup(struct image *im, const char *path, struct denode **out)
{
	char leaf[WIN_MAXLEN + 1];
	struct denode *dir;
	struct found f;
	int error;

	if ((error = walk(im, path, &dir, leaf)) != 0)
		return error;
	if (leaf[0] == '\0') {
		*out = dir;
		return 0;
	}
	error = find(dir, leaf, strlen(leaf), &f);
	if (error == 0)
		error = get_node(im, &f, out);
	put_node(im, dir);
	return error;
}

/* Copy file DEP's FileSize bytes to FD, cluster by cluster. */
static int
copy_out(struct denode *dep, int fd)
{
	struct msdosfsmount *pmp = dep->de_pmp;
	struct buf *bp;
	u_long left = dep->de_FileSize, frcn;
	daddr_t bn;
	int blsize, error;
	size_t chunk;

	if (dep->de_Attributes & ATTR_DIRECTORY)
		return EISDIR;
	for (frcn = 0; left > 0; frcn++) {
		if ((error = msdosfs_pcbmap(dep, frcn, &bn, NULL, &blsize)) != 0)
			return error == E2BIG ? EIO : error;
		if ((error = bread(pmp->pm_devvp, de_bn2kb(pmp, bn), blsize, 0,
		    &bp)) != 0)
			return error;
		chunk = left < (u_long)blsize ? (size_t)left : (size_t)blsize;
		if (write(fd, bp->b_data, chunk) != (ssize_t)chunk) {
			error = errno != 0 ? errno : EIO;
			brelse(bp, 0);
			return error;
		}
		brelse(bp, 0);
		left -= chunk;
	}
	return 0;
}

struct listing {
	struct msdosfsmount *pmp;
	struct dirent name;
	uint16_t namelen;
	int chksum;
};

/* ls: one line per entry, "size name", with "/" after a directory. */
static int
list_slot(struct direntry *dep, u_long diroff, u_long cluster, void *arg)
{
	struct listing *l = arg;
	struct msdosfsmount *pmp = l->pmp;
	u_char shortname[11];
	const char *suffix;

	(void)diroff;
	(void)cluster;
	if (dep->deName[0] == SLOT_DELETED) {
		l->chksum = -1;
		return 0;
	}
	if (dep->deAttributes == ATTR_WIN95) {
		if (!(pmp->pm_flags & MSDOSFSMNT_SHORTNAME))
			l->chksum = msdosfs_win2unixfn((struct winentry *)dep,
			    &l->name, l->chksum, &l->namelen,
			    pmp->pm_flags & MSDOSFSMNT_UTF8);
		return 0;
	}
	if (dep->deAttributes & ATTR_VOLUME) {
		l->chksum = -1;
		return 0;
	}
	if (l->chksum != msdosfs_winChksum(dep->deName)) {
		memcpy(shortname, dep->deName, 8);
		memcpy(shortname + 8, dep->deExtension, 3);
		l->namelen = (uint16_t)msdosfs_dos2unixfn(shortname,
		    (u_char *)l->name.d_name,
		    pmp->pm_flags & MSDOSFSMNT_SHORTNAME);
	}
	l->name.d_name[l->namelen] = '\0';
	l->chksum = -1;
	suffix = (dep->deAttributes & ATTR_DIRECTORY) &&
	    strcmp(l->name.d_name, ".") != 0 &&
	    strcmp(l->name.d_name, "..") != 0 ? "/" : "";
	printf("%10lu %s%s\n", (unsigned long)getulong(dep->deFileSize),
	    l->name.d_name, suffix);
	return 0;
}

static int
cmd_ls(struct image *im, const char *path)
{
	struct listing l;
	struct denode *dir;
	int error;

	if ((error = lookup(im, path, &dir)) != 0)
		return error;
	if (!(dir->de_Attributes & ATTR_DIRECTORY)) {
		put_node(im, dir);
		return ENOTDIR;
	}
	memset(&l, 0, sizeof(l));
	l.pmp = im->pmp;
	l.chksum = -1;
	error = scan(dir, list_slot, &l);
	put_node(im, dir);
	return error;
}

static int
cmd_cat(struct image *im, const char *path)
{
	struct denode *dep;
	int error;

	if ((error = lookup(im, path, &dep)) != 0)
		return error;
	error = copy_out(dep, STDOUT_FILENO);
	put_node(im, dep);
	return error;
}

/*
 * get: the destination is opened without truncation and truncated only
 * once it is known not to be the image itself, under its own name, a
 * hard link, or another node for the same device.
 */
static int
cmd_get(struct image *im, const char *path, const char *local)
{
	struct denode *dep;
	struct stat src, dst;
	int out, error;

	if ((error = lookup(im, path, &dep)) != 0)
		return error;
	if (dep->de_Attributes & ATTR_DIRECTORY) {
		put_node(im, dep);
		return EISDIR;
	}
	if ((out = open(local, O_WRONLY | O_CREAT, 0644)) < 0) {
		error = errno;
		put_node(im, dep);
		return error;
	}
	if (fstat(im->fd, &src) != 0 || fstat(out, &dst) != 0) {
		error = errno;
	} else if ((src.st_dev == dst.st_dev && src.st_ino == dst.st_ino) ||
	    (((S_ISBLK(src.st_mode) && S_ISBLK(dst.st_mode)) ||
	    (S_ISCHR(src.st_mode) && S_ISCHR(dst.st_mode))) &&
	    src.st_rdev == dst.st_rdev)) {
		error = EINVAL;
	} else if (ftruncate(out, 0) != 0) {
		error = errno;
	} else {
		error = copy_out(dep, out);
	}
	if (close(out) != 0 && error == 0)
		error = errno;
	put_node(im, dep);
	return error;
}

/*
 * Remove the entry F of directory DIR and free its clusters: the order the
 * kernel follows (msdosfs_remove, then msdosfs_inactive's truncation).
 */
static int
remove_entry(struct image *im, struct denode *dir, const struct found *f)
{
	struct msdosfs_lookup_results mlr;
	struct denode *dep;
	int error;

	if ((error = msdosfs_deget(im->pmp, f->dirclust, f->clustoff,
	    &dep)) != 0)
		return error;
	if ((dep->de_Attributes & ATTR_DIRECTORY) &&
	    !msdosfs_dosdirempty(dep)) {
		free(dep);
		return ENOTEMPTY;
	}
	memset(&mlr, 0, sizeof(mlr));
	mlr.mlr_fndoffset = f->diroff;
	if ((error = msdosfs_removede(dir, dep, &mlr)) == 0)
		error = msdosfs_detrunc(dep, 0, 0, NOCRED);
	free(dep);
	return error;
}

/*
 * put and mkdir: the file or directory LOCAL describes becomes PATH. A put
 * that cannot fit is refused before any cluster or slot is allocated; a
 * put that fails after its entry exists removes the entry again.
 */
static int
cmd_put(struct image *im, const char *local, const char *path, bool directory)
{
	char leaf[WIN_MAXLEN + 1];
	struct denode *dir, *dep;
	struct found f;
	fsinode inode;
	fsnode node;
	u_long clusters;
	int error;

	memset(&inode, 0, sizeof(inode));
	if (directory) {
		inode.st.st_mode = S_IFDIR | 0755;
		inode.st.st_atime = inode.st.st_mtime = time(NULL);
	} else {
		if (stat(local, &inode.st) != 0)
			return errno;
		if (!S_ISREG(inode.st.st_mode))
			return EINVAL;
		if ((unsigned long long)inode.st.st_size > MSDOSFS_FILESIZE_MAX)
			return EFBIG;
	}
	if ((error = walk(im, path, &dir, leaf)) != 0)
		return error;
	if (leaf[0] == '\0' || strcmp(leaf, ".") == 0 ||
	    strcmp(leaf, "..") == 0) {
		put_node(im, dir);
		return EINVAL;
	}
	if ((error = find(dir, leaf, strlen(leaf), &f)) != ENOENT) {
		put_node(im, dir);
		return error == 0 ? EEXIST : error;
	}
	clusters = directory ? 1 : (u_long)((inode.st.st_size +
	    im->pmp->pm_bpcluster - 1) / im->pmp->pm_bpcluster);
	if (clusters > im->pmp->pm_freeclustercount) {
		put_node(im, dir);
		return ENOSPC;
	}
	node.inode = &inode;
	node.name = leaf;
	errno = 0;
	dep = directory ? msdosfs_mkdire(local, dir, &node) :
	    msdosfs_mkfile(local, dir, &node);
	if (dep == NULL) {
		error = errno != 0 ? errno : EIO;
		/* An entry created before the failure is removed again. */
		if (find(dir, leaf, strlen(leaf), &f) == 0)
			(void)remove_entry(im, dir, &f);
		put_node(im, dir);
		return error;
	}
	free(dep);
	put_node(im, dir);
	return 0;
}

static int
cmd_rm(struct image *im, const char *path)
{
	char leaf[WIN_MAXLEN + 1];
	struct denode *dir;
	struct found f;
	int error;

	if ((error = walk(im, path, &dir, leaf)) != 0)
		return error;
	if (leaf[0] == '\0' || strcmp(leaf, ".") == 0 ||
	    strcmp(leaf, "..") == 0) {
		put_node(im, dir);
		return EINVAL;
	}
	if ((error = find(dir, leaf, strlen(leaf), &f)) == 0)
		error = remove_entry(im, dir, &f);
	put_node(im, dir);
	return error;
}

static int
cmd_df(struct image *im)
{
	const struct msdosfsmount *pmp = im->pmp;

	printf("fat %d cluster %lu clusters %lu free %lu\n",
	    FAT12(pmp) ? 12 : FAT16(pmp) ? 16 : 32,
	    (unsigned long)pmp->pm_bpcluster,
	    (unsigned long)pmp->pm_nmbrofclusters,
	    (unsigned long)pmp->pm_freeclustercount);
	return 0;
}

static int
cmd_newfs(const char *path, const char *sectors_arg, const char *fat_arg)
{
	struct msdos_options options;
	long long epoch = source_date_epoch();
	unsigned long sectors, fat = 0;
	char *end;

	errno = 0;
	sectors = strtoul(sectors_arg, &end, 10);
	if (errno != 0 || *end != '\0' || sectors == 0 ||
	    sectors > (unsigned long)INT32_MAX / DEV_BSIZE)
		return EINVAL;
	if (fat_arg != NULL) {
		fat = strtoul(fat_arg, &end, 10);
		if (*end != '\0' || (fat != 12 && fat != 16 && fat != 32))
			return EINVAL;
	}
	memset(&options, 0, sizeof(options));
	options.create_size = (off_t)sectors * DEV_BSIZE;
	options.fat_type = (uint8_t)fat;
	options.bytes_per_sector = DEV_BSIZE;
	if (epoch >= 0) {
		options.timestamp_set = 1;
		options.timestamp = (time_t)epoch;
	}
	return mkfs_msdos(path, NULL, &options) == 0 ? 0 : EINVAL;
}

int
main(int argc, char **argv)
{
	struct image im;
	const char *cmd;
	long long epoch;
	bool writing;
	int error;

	if (argc > 0 && argv[0][0] != '\0')
		program = argv[0];
	argv++;
	argc--;
	if (argc < 2)
		return usage();
	cmd = argv[0];
	if ((epoch = source_date_epoch()) >= 0) {
		stampst.st_ino = 1;
		stampst.st_atime = stampst.st_mtime = (time_t)epoch;
	}

	if (strcmp(cmd, "newfs") == 0) {
		if (argc != 3 && argc != 4)
			return usage();
		if ((error = cmd_newfs(argv[1], argv[2],
		    argc == 4 ? argv[3] : NULL)) != 0)
			return fail(cmd, error);
		return 0;
	}

	writing = strcmp(cmd, "put") == 0 || strcmp(cmd, "mkdir") == 0 ||
	    strcmp(cmd, "rm") == 0;
	if ((strcmp(cmd, "ls") == 0 && argc != 2 && argc != 3) ||
	    ((strcmp(cmd, "cat") == 0 || strcmp(cmd, "mkdir") == 0 ||
	    strcmp(cmd, "rm") == 0) && argc != 3) ||
	    ((strcmp(cmd, "get") == 0 || strcmp(cmd, "put") == 0) &&
	    argc != 4) || (strcmp(cmd, "df") == 0 && argc != 2))
		return usage();
	if (strcmp(cmd, "ls") != 0 && strcmp(cmd, "cat") != 0 &&
	    strcmp(cmd, "get") != 0 && strcmp(cmd, "df") != 0 && !writing)
		return usage();

	if ((error = image_open(&im, argv[1], writing)) != 0)
		return fail(argv[1], error);

	if (strcmp(cmd, "ls") == 0)
		error = cmd_ls(&im, argc == 3 ? argv[2] : "/");
	else if (strcmp(cmd, "cat") == 0)
		error = cmd_cat(&im, argv[2]);
	else if (strcmp(cmd, "get") == 0)
		error = cmd_get(&im, argv[2], argv[3]);
	else if (strcmp(cmd, "put") == 0)
		error = cmd_put(&im, argv[2], argv[3], false);
	else if (strcmp(cmd, "mkdir") == 0)
		error = cmd_put(&im, argv[2], argv[2], true);
	else if (strcmp(cmd, "rm") == 0)
		error = cmd_rm(&im, argv[2]);
	else
		error = cmd_df(&im);

	if (error != 0) {
		image_close(&im, writing);
		return fail(cmd, error);
	}
	if ((error = image_close(&im, writing)) != 0)
		return fail(argv[1], error);
	if (fflush(stdout) != 0)
		return fail("stdout", errno);
	if (getenv("MSDOSFS_REPORT_BUFFERS") != NULL)
		fprintf(stderr, "%s: buffers held at most %d of %d\n", program,
		    buf_high_water(), MSDOSFS_NBUF);
	return 0;
}
