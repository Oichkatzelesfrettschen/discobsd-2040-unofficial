/*
 * v7fs: read and write a Seventh Edition (V7) file system held in an image
 * file or on a raw device, through the NetBSD v7fs core in sys/fs/v7fs and
 * the descriptor backend in v7fs_io_fd.c.
 *
 *	v7fs [-p] newfs image sectors [files]
 *	v7fs [-p] ls image [dir]
 *	v7fs [-p] cat image path
 *	v7fs [-p] get image path local
 *	v7fs [-p] put image local path
 *	v7fs [-p] mkdir image path
 *	v7fs [-p] rm image path
 *	v7fs [-p] df image
 *
 * -p selects the PDP-11 byte order of disks written by V7 itself; the
 * default is little-endian. Paths are absolute or relative to the root of
 * the image; each component holds at most 14 bytes, the V7 limit. newfs
 * sizes the inode list for FILES inodes, or 4 percent of the volume when
 * FILES is absent or 0. rm removes a file or an empty directory.
 *
 * Exit status: 0 on success, 1 when an operation fails, 2 on a usage error.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "v7fs.h"
#include "v7fs_impl.h"
#include "v7fs_endian.h"
#include "v7fs_superblock.h"
#include "v7fs_inode.h"
#include "v7fs_datablock.h"
#include "v7fs_dirent.h"
#include "v7fs_file.h"
#include "newfs_v7fs.h"
#include "v7fs_io_fd.h"

/* sbin/newfs_v7fs/main.c reports through these; this tool stays silent. */
int v7fs_newfs_verbose = 0;

void
progress(const struct progress_arg *arg)
{
	(void)arg;
}

void
progress_done(void)
{
}

static int byte_order = LITTLE_ENDIAN;
static const char *program = "v7fs";

static int
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-p] newfs image sectors [files]\n"
	    "       %s [-p] ls image [dir]\n"
	    "       %s [-p] cat image path\n"
	    "       %s [-p] get image path local\n"
	    "       %s [-p] put image local path\n"
	    "       %s [-p] mkdir image path\n"
	    "       %s [-p] rm image path\n"
	    "       %s [-p] df image\n",
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

/* The open image: its descriptor and the mounted core state. */
struct image {
	int fd;
	struct v7fs_self *fs;
};

static int
image_open(struct image *im, const char *path, bool writing)
{
	struct v7fs_mount_device device;
	struct stat st;
	int error;

	if ((im->fd = open(path, writing ? O_RDWR : O_RDONLY)) < 0)
		return errno;
	if (fstat(im->fd, &st) != 0) {
		error = errno;
		close(im->fd);
		return error;
	}
	memset(&device, 0, sizeof(device));
	device.device.fd = im->fd;
	device.endian = byte_order;
	/* A regular file states its size; a device is bounded by v7fs. */
	device.sectors = S_ISREG(st.st_mode) ? (daddr_t)(st.st_size / V7FS_BSIZE) :
	    (daddr_t)V7FS_DADDR_MAX + 1;
	if ((error = v7fs_io_init(&im->fs, &device, V7FS_BSIZE)) != 0) {
		close(im->fd);
		return error;
	}
	if ((error = v7fs_superblock_load(im->fs)) != 0) {
		v7fs_io_fini(im->fs);
		close(im->fd);
		return error;
	}
	return 0;
}

static int
image_close(struct image *im, bool writing)
{
	int error = 0;

	if (writing) {
		error = v7fs_superblock_writeback(im->fs);
		if (fsync(im->fd) != 0 && error == 0)
			error = errno;
	}
	v7fs_io_fini(im->fs);
	if (close(im->fd) != 0 && error == 0)
		error = errno;
	return error;
}

/*
 * Walk PATH from the root. On success *dir holds the inode number of the
 * directory that contains the last component, which LEAF (V7FS_NAME_MAX
 * bytes and a NUL) receives; an empty path or "/" names the root itself,
 * with LEAF empty.
 */
static int
walk(struct v7fs_self *fs, const char *path, v7fs_ino_t *dir, char *leaf)
{
	struct v7fs_inode inode;
	const char *p = path, *end;
	v7fs_ino_t ino = V7FS_ROOT_INODE;
	size_t len;
	int error;

	*dir = V7FS_ROOT_INODE;
	leaf[0] = '\0';
	for (;;) {
		while (*p == '/')
			p++;
		if (*p == '\0')
			return 0;
		end = strchr(p, '/');
		len = end != NULL ? (size_t)(end - p) : strlen(p);
		if (len > V7FS_NAME_MAX)
			return ENAMETOOLONG;
		if (leaf[0] != '\0') {
			/* The previous leaf is an intermediate directory. */
			if ((error = v7fs_inode_load(fs, &inode, *dir)) != 0 ||
			    (error = v7fs_file_lookup_by_name(fs, &inode, leaf,
			    strlen(leaf), &ino)) != 0)
				return error;
			if ((error = v7fs_inode_load(fs, &inode, ino)) != 0)
				return error;
			if (!v7fs_inode_isdir(&inode))
				return ENOTDIR;
			*dir = ino;
		}
		memcpy(leaf, p, len);
		leaf[len] = '\0';
		p += len;
	}
}

/* Resolve PATH to an inode; the root when the path names no component. */
static int
lookup(struct v7fs_self *fs, const char *path, struct v7fs_inode *out)
{
	char leaf[V7FS_NAME_MAX + 1];
	struct v7fs_inode parent;
	v7fs_ino_t dir, ino;
	int error;

	if ((error = walk(fs, path, &dir, leaf)) != 0)
		return error;
	if (leaf[0] == '\0')
		return v7fs_inode_load(fs, out, V7FS_ROOT_INODE);
	if ((error = v7fs_inode_load(fs, &parent, dir)) != 0 ||
	    (error = v7fs_file_lookup_by_name(fs, &parent, leaf, strlen(leaf),
	    &ino)) != 0)
		return error;
	return v7fs_inode_load(fs, out, ino);
}

/* Data copy between a file's blocks and a descriptor. */
struct copy {
	int fd;
	bool to_image;
};

static int
copy_block(struct v7fs_self *fs, void *arg, v7fs_daddr_t blk, size_t size)
{
	const struct copy *c = arg;
	uint8_t buf[V7FS_BSIZE];
	size_t done = 0;
	ssize_t n;

	if (c->to_image)
		memset(buf, 0, sizeof(buf));
	else if (!fs->io.read(fs->io.cookie, buf, blk))
		return V7FS_ITERATOR_ERROR;
	while (done < size) {
		n = c->to_image ? read(c->fd, buf + done, size - done) :
		    write(c->fd, buf + done, size - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return V7FS_ITERATOR_ERROR;
		done += (size_t)n;
	}
	if (c->to_image && !fs->io.write(fs->io.cookie, buf, blk))
		return V7FS_ITERATOR_ERROR;
	return 0;
}

static int
copy_out(struct v7fs_self *fs, struct v7fs_inode *inode, int fd)
{
	struct copy c = { fd, false };

	if (!v7fs_inode_isfile(inode))
		return EISDIR;
	if (v7fs_inode_filesize(inode) == 0)
		return 0;
	return v7fs_datablock_foreach(fs, inode, copy_block, &c) ==
	    V7FS_ITERATOR_END ? 0 : EIO;
}

/* ls: one line per entry, "inode mode links size name". */
static int
list_block(struct v7fs_self *fs, void *arg, v7fs_daddr_t blk, size_t size)
{
	struct v7fs_dirent entries[V7FS_BSIZE / sizeof(struct v7fs_dirent)];
	struct v7fs_inode inode;
	char name[V7FS_NAME_MAX + 1];
	size_t i, n = size / sizeof(struct v7fs_dirent);
	v7fs_ino_t ino;

	(void)arg;
	if (!fs->io.read(fs->io.cookie, (uint8_t *)entries, blk))
		return V7FS_ITERATOR_ERROR;
	for (i = 0; i < n; i++) {
		ino = V7FS_VAL16(fs, entries[i].inode_number);
		if (ino == 0)
			continue;
		memcpy(name, entries[i].name, V7FS_NAME_MAX);
		name[V7FS_NAME_MAX] = '\0';
		if (v7fs_inode_load(fs, &inode, ino) != 0)
			return V7FS_ITERATOR_ERROR;
		printf("%5u %06o %3d %8lu %s%s\n", (unsigned)ino,
		    (unsigned)(inode.mode & 0177777), (int)inode.nlink,
		    (unsigned long)v7fs_inode_filesize(&inode), name,
		    v7fs_inode_isdir(&inode) && strcmp(name, ".") != 0 &&
		    strcmp(name, "..") != 0 ? "/" : "");
	}
	return 0;
}

static int
cmd_newfs(const char *path, const char *sectors_arg, const char *files_arg)
{
	struct v7fs_mount_device device;
	char *end;
	long sectors, files = 0, ilist;
	struct stat st;
	int fd, error;

	errno = 0;
	sectors = strtol(sectors_arg, &end, 10);
	/* v7fs_superblock_sanity() refuses a volume below 128 sectors. */
	if (errno != 0 || *end != '\0' || sectors < 128 ||
	    sectors > V7FS_DADDR_MAX + 1L)
		return fail("sectors", EINVAL);
	if ((uint64_t)sectors > V7FS_FD_MAX_SECTORS)
		return fail("sectors", EFBIG);
	if (files_arg != NULL) {
		files = strtol(files_arg, &end, 10);
		if (errno != 0 || *end != '\0' || files < 0 || files > 65535)
			return fail("files", EINVAL);
	}
	/*
	 * The inode list as determine_ilist_size() in sbin/newfs_v7fs/main.c
	 * sizes it; the data area after it must hold the 50-entry free-block
	 * cache newfs fills, so a refused geometry leaves the target untouched.
	 */
	ilist = files != 0 ? (files + (long)V7FS_INODE_PER_BLOCK - 1) /
	    (long)V7FS_INODE_PER_BLOCK : sectors / 25;
	if (ilist > (long)V7FS_ILISTBLK_MAX)
		ilist = (long)V7FS_ILISTBLK_MAX;
	if (ilist < 1 || sectors - V7FS_ILIST_SECTOR - ilist <= V7FS_MAX_FREEBLOCK)
		return fail("newfs", ENOSPC);
	if ((fd = open(path, O_RDWR | O_CREAT, 0644)) < 0)
		return fail(path, errno);
	/* An image file grows to the requested size; a device is used as is. */
	if (fstat(fd, &st) != 0 || (S_ISREG(st.st_mode) &&
	    st.st_size < (off_t)sectors * V7FS_BSIZE &&
	    ftruncate(fd, (off_t)sectors * V7FS_BSIZE) != 0)) {
		error = errno;
		close(fd);
		return fail(path, error);
	}
	memset(&device, 0, sizeof(device));
	device.device.fd = fd;
	device.sectors = (daddr_t)sectors;
	device.endian = byte_order;
	error = v7fs_newfs(&device, (int32_t)files);
	if (error == 0 && fsync(fd) != 0)
		error = errno;
	if (close(fd) != 0 && error == 0)
		error = errno;
	return error != 0 ? fail("newfs", error) : 0;
}

/*
 * Data blocks plus index blocks a file of SIZE bytes occupies: ten direct
 * addresses, then single, double and triple indirect blocks of
 * V7FS_DADDR_PER_BLOCK addresses each, as v7fs_datablock_expand() allocates
 * them.
 */
static uint64_t
blocks_for(uint64_t size)
{
	const uint64_t per = V7FS_DADDR_PER_BLOCK;
	uint64_t n = (size + V7FS_BSIZE - 1) / V7FS_BSIZE, total = n, level;

	if (n <= V7FS_NADDR_DIRECT)
		return total;
	n -= V7FS_NADDR_DIRECT;
	total += 1;				/* single indirect */
	if (n <= per)
		return total;
	n -= per;
	level = n < per * per ? n : per * per;
	total += 1 + (level + per - 1) / per;	/* double indirect */
	n -= level;
	if (n == 0)
		return total;
	total += 1 + (n + per * per - 1) / (per * per) + (n + per - 1) / per;
	return total;
}

static int cmd_rm(struct image *, const char *);

/* Check the expected capacity before starting a put or mkdir operation. */
static int
cmd_put(struct image *im, const char *local, const char *path, bool directory)
{
	char leaf[V7FS_NAME_MAX + 1];
	struct v7fs_fileattr attr;
	struct v7fs_inode parent, inode, original_inode;
	struct copy c = { -1, true };
	struct stat st;
	uint64_t needed;
	v7fs_ino_t dir, ino;
	int error, cleanup_error;

	if ((error = walk(im->fs, path, &dir, leaf)) != 0)
		return error;
	if (leaf[0] == '\0')
		return EEXIST;
	st.st_size = 0;
	memset(&attr, 0, sizeof(attr));
	attr.mode = directory ? V7FS_IFDIR | 0755 : V7FS_IFREG | 0644;
	attr.ctime = attr.mtime = attr.atime = (v7fs_time_t)time(NULL);
	if (!directory) {
		if ((c.fd = open(local, O_RDONLY)) < 0)
			return errno;
		if (fstat(c.fd, &st) != 0) {
			error = errno;
			close(c.fd);
			return error;
		}
		if (!S_ISREG(st.st_mode)) {
			close(c.fd);
			return EINVAL;
		}
		/* A v7fs file size is 32 bits (v7fs_off_t). */
		if ((uint64_t)st.st_size > UINT32_MAX) {
			close(c.fd);
			return EFBIG;
		}
	}
	if ((error = v7fs_inode_load(im->fs, &parent, dir)) != 0)
		goto out;
	needed = directory ? 1 : blocks_for((uint64_t)st.st_size);
	if (v7fs_inode_filesize(&parent) % V7FS_BSIZE == 0)
		needed++;
	if (needed > im->fs->superblock.total_freeblock) {
		error = ENOSPC;
		goto out;
	}
	if ((error = v7fs_file_allocate(im->fs, &parent, leaf, strlen(leaf),
	    &attr, &ino)) != 0)
		goto out;
	if (directory || st.st_size == 0)
		goto out;
	if ((error = v7fs_inode_load(im->fs, &inode, ino)) != 0)
		goto remove_created;
	original_inode = inode;
	if ((error = v7fs_datablock_expand(im->fs, &inode,
	    (size_t)st.st_size)) != 0) {
		if (!v7fs_inode_data_equal(&inode, &original_inode))
			goto out;
		goto remove_created;
	}
	if (v7fs_datablock_foreach(im->fs, &inode, copy_block, &c) !=
	    V7FS_ITERATOR_END)
		error = EIO;
	else
		error = v7fs_inode_writeback(im->fs, &inode);
	if (error == 0)
		goto out;
remove_created:
	cleanup_error = cmd_rm(im, path);
	if (cleanup_error != 0)
		error = cleanup_error;
out:
	if (c.fd >= 0 && close(c.fd) != 0 && error == 0)
		error = errno;
	return error;
}

/*
 * rm: v7fs_file_deallocate() removes the entry and drops the link count;
 * releasing an inode left with no links and its data blocks is the
 * host's step, which NetBSD's vnode layer performs at reclaim
 * (v7fs_reclaim() in v7fs_vnops.c). This process holds no open files, so the
 * release follows at once.
 */
/*
 * get: the source must be a regular file, and the destination is truncated
 * only once it is known not to be the image itself, under its own name, a
 * hard link, or another node for the same device.
 */
static int
cmd_get(struct image *im, const char *path, const char *local)
{
	struct v7fs_inode inode;
	struct stat src, dst;
	int out, error;

	if ((error = lookup(im->fs, path, &inode)) != 0)
		return error;
	if (!v7fs_inode_isfile(&inode))
		return EISDIR;
	if ((out = open(local, O_WRONLY | O_CREAT, 0644)) < 0)
		return errno;
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
		error = copy_out(im->fs, &inode, out);
	}
	if (close(out) != 0 && error == 0)
		error = errno;
	return error;
}

static int
cmd_rm(struct image *im, const char *path)
{
	char leaf[V7FS_NAME_MAX + 1];
	struct v7fs_inode parent, inode, original_inode, unlinked_inode;
	v7fs_ino_t dir, ino;
	int error, restore_error;
	bool retryable;

	if ((error = walk(im->fs, path, &dir, leaf)) != 0)
		return error;
	if (leaf[0] == '\0' || strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0)
		return EINVAL;
	if ((error = v7fs_inode_load(im->fs, &parent, dir)) != 0 ||
	    (error = v7fs_file_lookup_by_name(im->fs, &parent, leaf,
	    strlen(leaf), &ino)) != 0 ||
	    (error = v7fs_inode_load(im->fs, &inode, ino)) != 0)
		return error;
	original_inode = inode;
	if ((error = v7fs_file_deallocate(im->fs, &parent, leaf,
	    strlen(leaf))) != 0)
		return error;
	if (v7fs_inode_isdir(&inode))
		inode.nlink = 0;
	else
		inode.nlink--;
	if (v7fs_inode_nlink(&inode) != 0)
		return 0;
	unlinked_inode = inode;
	if ((error = v7fs_datablock_contract_status(im->fs, &inode,
	    inode.filesize, &retryable)) != 0) {
		if (!v7fs_inode_data_equal(&inode, &unlinked_inode)) {
			if (!retryable)
				return error;
			restore_error = v7fs_datablock_contract_status(im->fs, &inode,
			    inode.filesize, &retryable);
			if (restore_error != 0)
				return restore_error;
			restore_error = v7fs_inode_deallocate(im->fs, ino);
			if (restore_error != 0)
				return restore_error;
			return error;
		}
		restore_error = v7fs_inode_writeback(im->fs, &original_inode);
		if (restore_error != 0)
			return restore_error;
		restore_error = v7fs_directory_add_entry(im->fs, &parent, ino,
		    leaf, strlen(leaf));
		return restore_error != 0 ? restore_error : error;
	}
	error = v7fs_inode_deallocate(im->fs, ino);
	if (error != 0) {
		restore_error = v7fs_inode_deallocate(im->fs, ino);
		if (restore_error != 0)
			return restore_error;
		return error;
	}
	return 0;
}

static int
cmd_df(struct image *im)
{
	const struct v7fs_superblock *sb = &im->fs->superblock;

	printf("blocks %lu data %lu free %lu inodes %lu free %lu\n",
	    (unsigned long)sb->volume_size,
	    (unsigned long)(sb->volume_size - sb->datablock_start_sector),
	    (unsigned long)sb->total_freeblock,
	    (unsigned long)V7FS_MAX_INODE(sb),
	    (unsigned long)sb->total_freeinode);
	return 0;
}

int
main(int argc, char **argv)
{
	struct image im;
	struct v7fs_inode inode;
	const char *cmd;
	bool writing;
	int error;

	if (argc > 0 && argv[0][0] != '\0')
		program = argv[0];
	argv++;
	argc--;
	if (argc > 0 && strcmp(argv[0], "-p") == 0) {
		byte_order = PDP_ENDIAN;
		argv++;
		argc--;
	}
	if (argc < 2)
		return usage();
	cmd = argv[0];

	if (strcmp(cmd, "newfs") == 0) {
		if (argc != 3 && argc != 4)
			return usage();
		return cmd_newfs(argv[1], argv[2], argc == 4 ? argv[3] : NULL);
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

	if (strcmp(cmd, "ls") == 0) {
		error = lookup(im.fs, argc == 3 ? argv[2] : "/", &inode);
		if (error == 0 && !v7fs_inode_isdir(&inode))
			error = ENOTDIR;
		if (error == 0 && v7fs_datablock_foreach(im.fs, &inode,
		    list_block, NULL) != V7FS_ITERATOR_END)
			error = EIO;
	} else if (strcmp(cmd, "cat") == 0) {
		if ((error = lookup(im.fs, argv[2], &inode)) == 0)
			error = copy_out(im.fs, &inode, STDOUT_FILENO);
	} else if (strcmp(cmd, "get") == 0) {
		error = cmd_get(&im, argv[2], argv[3]);
	} else if (strcmp(cmd, "put") == 0) {
		error = cmd_put(&im, argv[2], argv[3], false);
	} else if (strcmp(cmd, "mkdir") == 0) {
		error = cmd_put(&im, NULL, argv[2], true);
	} else if (strcmp(cmd, "rm") == 0) {
		error = cmd_rm(&im, argv[2]);
	} else {
		error = cmd_df(&im);
	}

	if (error != 0) {
		/*
		 * Persist the in-core free lists after an operation error.
		 * Failed compensation can still require image repair.
		 */
		image_close(&im, writing);
		return fail(cmd, error);
	}
	if ((error = image_close(&im, writing)) != 0)
		return fail(argv[1], error);
	if (fflush(stdout) != 0)
		return fail("stdout", errno);
	return 0;
}
