/*
 * Host oracle for the NetBSD v7fs core imported under sys/fs/v7fs.
 *
 * The program formats an image with NetBSD's own v7fs_newfs()
 * (sbin/newfs_v7fs/main.c), writes a small file, a file that reaches the
 * single-indirect block, and a subdirectory through the core, then checks
 * the image twice:
 *
 *   core     the core mounts the image again and reads every file back;
 *   reader   an independent parser reads the raw bytes with the Seventh
 *            Edition layouts (filsys.h superblock, ino.h inode, dir.h
 *            entry, fblk free-list block) and accounts for every data
 *            block: each block below the volume size is either reachable
 *            from an inode or on the free list, never both.
 *
 * The second argument selects the byte order the core writes (the core is
 * built with V7FS_EI). "pdp" is the PDP-11 order of Seventh Edition disks:
 * 16-bit fields low byte first, 32-bit fields high word first, and 24-bit
 * inode addresses as l3tol() in V7's iget.c unpacks them (high byte, then
 * low, then middle). "little" is the order an ARM build would write
 * natively; there the reader shares one convention with the core that no V7
 * header states, the low-byte-first 24-bit address of
 * val24_normal_order_write() in v7fs_endian.c. A core that reads back its
 * own consistent but wrong layout passes the core check and fails the
 * reader, which the swapped-address mutant in check.sh demonstrates.
 *
 * "v7fs_core_test IMAGE little regress" runs the regression scenarios for
 * the local patches under tools/netbsd-patches.
 *
 * Two further modes let check.sh drive usr.sbin/v7fs through the same
 * checks: "v7fs_core_test DIR - patterns" writes the three files (hello, big,
 * inner) into DIR, "v7fs_core_test IMAGE ORDER verify" skips formatting and
 * writing and checks an image that already holds /hello, /big and /sub/inner,
 * and "v7fs_core_test IMAGE ORDER parent BLOCKS" creates a root directory at
 * an indirect-address boundary for the command-line capacity checks.
 *
 * Exit status: 0 when every check holds, 1 when a check fails (each
 * failure prints "FAIL <label>: ..."), 2 when the environment fails.
 */

#include <sys/types.h>
#include <sys/param.h>	/* BYTE_ORDER, LITTLE_ENDIAN, PDP_ENDIAN */
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "v7fs.h"
#include "v7fs_impl.h"
#include "v7fs_endian.h"
#include "v7fs_superblock.h"
#include "v7fs_inode.h"
#include "v7fs_datablock.h"
#include "v7fs_file.h"
#include "newfs_v7fs.h"

/* Pin the current host and Cortex-M0+ free-inode cache layout. */
_Static_assert((offsetof(struct v7fs_self, superblock) +
    offsetof(struct v7fs_superblock, freeinode)) % _Alignof(v7fs_ino_t) == 0,
    "superblock freeinode cache is misaligned for v7fs_ino_t");

#define	SECTORS		2048	/* 1 MiB image */
#define	MAXFILE		64
#define	STAMP		1000000000
#define	BIG_SIZE	40000	/* 79 blocks: 10 direct, 69 through index1 */
#define	INNER_SIZE	600
/* Data blocks the files occupy: root 1, sub 1, hello 1, big 79 plus its
 * index1 block, inner 2. */
#define	FILE_BLOCKS	85

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

static int failures;
static int byte_order;	/* LITTLE_ENDIAN or PDP_ENDIAN */

static void
fail(const char *label, const char *detail)
{
	printf("FAIL %s: %s\n", label, detail);
	failures++;
}

static void
check(int ok, const char *label, const char *detail)
{
	if (!ok)
		fail(label, detail);
}

static bool
inode_equal(const struct v7fs_inode *left, const struct v7fs_inode *right)
{
	return left->inode_number == right->inode_number &&
	    left->mode == right->mode && left->nlink == right->nlink &&
	    left->uid == right->uid && left->gid == right->gid &&
	    left->atime == right->atime && left->mtime == right->mtime &&
	    left->ctime == right->ctime && left->append_mode == right->append_mode &&
	    left->device == right->device && v7fs_inode_data_equal(left, right);
}

static uint8_t big[BIG_SIZE];
static uint8_t inner[INNER_SIZE];
static const uint8_t hello[] = "hello, v7fs\n";

static void
fill_patterns(void)
{
	size_t i;

	for (i = 0; i < sizeof(big); i++)
		big[i] = (uint8_t)((i * 7 + 3) % 251);
	for (i = 0; i < sizeof(inner); i++)
		inner[i] = (uint8_t)(255 - i % 256);
}

/* Core side. */

struct copy {
	uint8_t *data;
	size_t offset;
	int write;
};

static int
copy_block(struct v7fs_self *fs, void *arg, v7fs_daddr_t blk, size_t size)
{
	struct copy *c = arg;
	uint8_t buf[V7FS_BSIZE];

	if (c->write) {
		memset(buf, 0, sizeof(buf));
		memcpy(buf, c->data + c->offset, size);
		if (!fs->io.write(fs->io.cookie, buf, blk))
			return V7FS_ITERATOR_ERROR;
	} else {
		if (!fs->io.read(fs->io.cookie, buf, blk))
			return V7FS_ITERATOR_ERROR;
		memcpy(c->data + c->offset, buf, size);
	}
	c->offset += size;
	return 0;
}

static int
make_node(struct v7fs_self *fs, v7fs_ino_t parent_ino, const char *name,
    v7fs_mode_t mode, const uint8_t *data, size_t size, v7fs_ino_t *ino)
{
	struct v7fs_inode parent, inode;
	struct v7fs_fileattr attr;
	struct copy c;
	int error;

	memset(&attr, 0, sizeof(attr));
	attr.mode = mode;
	attr.ctime = attr.mtime = attr.atime = STAMP;
	if ((error = v7fs_inode_load(fs, &parent, parent_ino)) != 0)
		return error;
	if ((error = v7fs_file_allocate(fs, &parent, name, strlen(name),
	    &attr, ino)) != 0)
		return error;
	if (size == 0)
		return 0;
	if ((error = v7fs_inode_load(fs, &inode, *ino)) != 0)
		return error;
	if ((error = v7fs_datablock_expand(fs, &inode, size)) != 0)
		return error;
	c.data = (uint8_t *)(uintptr_t)data;
	c.offset = 0;
	c.write = 1;
	if (v7fs_datablock_foreach(fs, &inode, copy_block, &c) !=
	    V7FS_ITERATOR_END)
		return EIO;
	return v7fs_inode_writeback(fs, &inode);
}

static struct v7fs_self *
mount_image(int fd)
{
	struct v7fs_mount_device device;
	struct v7fs_self *fs;

	memset(&device, 0, sizeof(device));
	device.device.fd = fd;
	device.sectors = SECTORS;
	device.endian = byte_order;
	if (v7fs_io_init(&fs, &device, V7FS_BSIZE) != 0)
		return NULL;
	fs->endian = byte_order;
	v7fs_endian_init(fs);
	if (v7fs_superblock_load(fs) != 0) {
		v7fs_io_fini(fs);
		return NULL;
	}
	return fs;
}

static int
populate(int fd, v7fs_ino_t inos[4])
{
	struct v7fs_mount_device device;
	struct v7fs_self *fs;
	int error;

	memset(&device, 0, sizeof(device));
	device.device.fd = fd;
	device.sectors = SECTORS;
	device.endian = byte_order;
	if ((error = v7fs_newfs(&device, MAXFILE)) != 0)
		return error;
	if ((fs = mount_image(fd)) == NULL)
		return EIO;
	if ((error = make_node(fs, V7FS_ROOT_INODE, "hello",
	    V7FS_IFREG | 0644, hello, sizeof(hello) - 1, &inos[0])) != 0 ||
	    (error = make_node(fs, V7FS_ROOT_INODE, "big",
	    V7FS_IFREG | 0600, big, sizeof(big), &inos[1])) != 0 ||
	    (error = make_node(fs, V7FS_ROOT_INODE, "sub",
	    V7FS_IFDIR | 0755, NULL, 0, &inos[2])) != 0 ||
	    (error = make_node(fs, inos[2], "inner",
	    V7FS_IFREG | 0644, inner, sizeof(inner), &inos[3])) != 0)
		return error;
	error = v7fs_superblock_writeback(fs);
	v7fs_io_fini(fs);
	return error;
}

static void
core_read(struct v7fs_self *fs, v7fs_ino_t dir, const char *name,
    const uint8_t *want, size_t size, const char *label)
{
	struct v7fs_inode parent, inode;
	v7fs_ino_t ino;
	struct copy c;
	uint8_t *got;

	if (v7fs_inode_load(fs, &parent, dir) != 0 ||
	    v7fs_file_lookup_by_name(fs, &parent, name, strlen(name), &ino)
	    != 0 || v7fs_inode_load(fs, &inode, ino) != 0) {
		fail(label, "lookup through the core failed");
		return;
	}
	if (v7fs_inode_filesize(&inode) != size) {
		fail(label, "core reports the wrong file size");
		return;
	}
	if ((got = calloc(1, size)) == NULL) {
		perror("calloc");
		exit(2);
	}
	c.data = got;
	c.offset = 0;
	c.write = 0;
	if (v7fs_datablock_foreach(fs, &inode, copy_block, &c) !=
	    V7FS_ITERATOR_END)
		fail(label, "core read failed");
	else
		check(memcmp(got, want, size) == 0, label,
		    "core read differs from the written bytes");
	free(got);
}

/* Independent reader: V7 layouts over the raw image. */

static uint8_t image[SECTORS * 512];
static uint8_t owner[SECTORS];	/* 0 unused, 1 file data, 2 free list */

static unsigned
le16(const uint8_t *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t
le32(const uint8_t *p)
{
	if (byte_order == PDP_ENDIAN)
		return (uint32_t)le16(p) << 16 | le16(p + 2);
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static uint32_t
le24(const uint8_t *p)
{
	if (byte_order == PDP_ENDIAN)
		return (uint32_t)p[0] << 16 | (uint32_t)p[1] | (uint32_t)p[2] << 8;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
}

/* filsys.h: s_isize, s_fsize, s_nfree, s_free[50], s_ninode, s_inode[100],
 * four lock and flag bytes, s_time, s_tfree, s_tinode. */
#define	SB		(image + 512)
#define	SB_ISIZE	le16(SB + 0)
#define	SB_FSIZE	le32(SB + 2)
#define	SB_NFREE	le16(SB + 6)
#define	SB_FREE(i)	le32(SB + 8 + 4 * (i))
#define	SB_NINODE	le16(SB + 208)
#define	SB_TFREE	le32(SB + 418)
#define	SB_TINODE	le16(SB + 422)

/* ino.h: di_mode, di_nlink, di_uid, di_gid, di_size, di_addr[40], times. */
static const uint8_t *
dinode(unsigned ino)
{
	return image + 2 * 512 + (size_t)(ino - 1) * 64;
}

static int
block_ok(uint32_t blk)
{
	return blk >= SB_ISIZE && blk < SB_FSIZE;
}

static int
claim(uint32_t blk, uint8_t kind, const char *label)
{
	if (!block_ok(blk)) {
		fail(label, "block number outside the data area");
		return 0;
	}
	if (owner[blk] != 0) {
		fail(label, "block claimed twice");
		return 0;
	}
	owner[blk] = kind;
	return 1;
}

/* Read the file of inode INO into OUT (SIZE bytes), claiming its blocks. */
static int
reader_file(unsigned ino, uint8_t *out, size_t size, const char *label)
{
	const uint8_t *di = dinode(ino);
	size_t nblocks = (size + 511) / 512, i;
	uint32_t blk, index1 = 0;

	if (le32(di + 8) != size) {
		fail(label, "reader sees the wrong di_size");
		return 0;
	}
	if (nblocks > 10 + 128) {
		fail(label, "test file exceeds the single-indirect range");
		return 0;
	}
	for (i = 0; i < nblocks; i++) {
		if (i < 10) {
			blk = le24(di + 12 + 3 * i);
		} else {
			if (index1 == 0) {
				index1 = le24(di + 12 + 3 * 10);
				if (!claim(index1, 1, label))
					return 0;
			}
			blk = le32(image + (size_t)index1 * 512 + 4 * (i - 10));
		}
		if (!claim(blk, 1, label))
			return 0;
		memcpy(out + i * 512, image + (size_t)blk * 512,
		    size - i * 512 < 512 ? size - i * 512 : 512);
	}
	return 1;
}

/* dir.h: d_ino (16 bits) and d_name[14], 16 bytes per entry. */
static unsigned
reader_lookup(const uint8_t *dir, size_t size, const char *name)
{
	size_t off;
	char entry[15];

	for (off = 0; off + 16 <= size; off += 16) {
		memcpy(entry, dir + off + 2, 14);
		entry[14] = '\0';
		if (le16(dir + off) != 0 && strcmp(entry, name) == 0)
			return le16(dir + off);
	}
	return 0;
}

static void
reader_expect_file(const uint8_t *dir, size_t dirsize, const char *name,
    const uint8_t *want, size_t size, const char *label)
{
	unsigned ino = reader_lookup(dir, dirsize, name);
	uint8_t *got;

	if (ino == 0) {
		fail(label, "reader finds no directory entry");
		return;
	}
	check((le16(dinode(ino)) & 0170000) == 0100000, label,
	    "reader sees a non-regular mode");
	if ((got = calloc(1, size)) == NULL) {
		perror("calloc");
		exit(2);
	}
	if (reader_file(ino, got, size, label))
		check(memcmp(got, want, size) == 0, label,
		    "reader bytes differ from the written bytes");
	free(got);
}

static void
reader(void)
{
	uint8_t rootdir[512], subdir[512];
	uint32_t blk, link, freecount = 0, data, used = 0;
	unsigned ino, sub, n, i, allocated = 0, ninodes;
	size_t rootsize, subsize;
	int before = failures;	/* the core checks have already run */

	check(SB_FSIZE == SECTORS, "superblock", "s_fsize differs from the image");
	check(SB_ISIZE > 2 && SB_ISIZE < SECTORS, "superblock",
	    "s_isize outside the image");
	check(SB_NFREE <= 50 && SB_NINODE <= 100, "superblock",
	    "free caches exceed their V7 bounds");
	if (failures != before)
		return;

	rootsize = le32(dinode(2) + 8);
	check((le16(dinode(2)) & 0170000) == 0040000, "root", "inode 2 is not a directory");
	if (rootsize == 0 || rootsize > sizeof(rootdir) || rootsize % 16 != 0) {
		fail("root", "root directory size is not 1..32 entries");
		return;
	}
	if (!reader_file(2, rootdir, rootsize, "root"))
		return;
	check(reader_lookup(rootdir, rootsize, ".") == 2, "root", "'.' is not 2");
	check(reader_lookup(rootdir, rootsize, "..") == 2, "root", "'..' is not 2");
	reader_expect_file(rootdir, rootsize, "hello", hello, sizeof(hello) - 1,
	    "reader-hello");
	reader_expect_file(rootdir, rootsize, "big", big, sizeof(big), "reader-big");

	if ((sub = reader_lookup(rootdir, rootsize, "sub")) == 0) {
		fail("reader-sub", "no entry");
	} else {
		subsize = le32(dinode(sub) + 8);
		if (subsize == 0 || subsize > sizeof(subdir) || subsize % 16 != 0)
			fail("reader-sub", "directory size is not 1..32 entries");
		else if (reader_file(sub, subdir, subsize, "reader-sub")) {
			check(reader_lookup(subdir, subsize, ".") == sub,
			    "reader-sub", "'.' is not the directory itself");
			check(reader_lookup(subdir, subsize, "..") == 2,
			    "reader-sub", "'..' is not the root");
			reader_expect_file(subdir, subsize, "inner", inner,
			    sizeof(inner), "reader-inner");
		}
	}

	/* Free list: the cache in the superblock, then each chained fblk
	 * (df_nfree, df_free[50]); entry 0 links the next block, 0 ends. */
	n = SB_NFREE;
	link = SB_FREE(0);
	for (i = 1; i < n; i++)
		if (claim(SB_FREE(i), 2, "freelist"))
			freecount++;
	while (link != 0) {
		if (!claim(link, 2, "freelist"))
			break;
		freecount++;
		blk = link;
		n = le16(image + (size_t)blk * 512);
		if (n > 50) {
			fail("freelist", "df_nfree exceeds 50");
			break;
		}
		link = n > 0 ? le32(image + (size_t)blk * 512 + 2) : 0;
		for (i = 1; i < n; i++)
			if (claim(le32(image + (size_t)blk * 512 + 2 + 4 * i), 2,
			    "freelist"))
				freecount++;
	}
	check(freecount == SB_TFREE, "freelist", "s_tfree differs from the list length");

	data = SB_FSIZE - SB_ISIZE;
	for (blk = SB_ISIZE; blk < SB_FSIZE; blk++)
		used += owner[blk] == 1;
	check(used == FILE_BLOCKS, "accounting",
	    "the files occupy a different number of data blocks");
	check(used + freecount == data, "accounting",
	    "a data block is neither in a file nor on the free list");

	ninodes = (SB_ISIZE - 2) * 8;
	for (ino = 1; ino <= ninodes; ino++)
		allocated += le16(dinode(ino)) != 0;
	check(SB_TINODE + allocated == ninodes, "inodes",
	    "s_tinode and the allocated inodes do not cover the ilist");
	printf("reader: data blocks %u, in files %u, free %u; inodes %u, allocated %u, free %u\n",
	    (unsigned)data, (unsigned)used, (unsigned)freecount, ninodes, allocated,
	    SB_TINODE);
}

static void
reader_index_ownership(uint32_t block, unsigned level, const char *label)
{
	unsigned index;
	uint32_t child;

	if (!claim(block, 1, label))
		return;
	for (index = 0; index < 128; index++) {
		child = le32(image + (size_t)block * 512 + index * 4);
		if (child == 0)
			continue;
		if (level == 1)
			(void)claim(child, 1, label);
		else
			reader_index_ownership(child, level - 1, label);
	}
}

static void
reader_inode_ownership(unsigned ino, const char *label)
{
	const uint8_t *inode = dinode(ino);
	uint32_t block;
	unsigned index;

	for (index = 0; index < 10; index++) {
		block = le24(inode + 12 + index * 3);
		if (block != 0)
			(void)claim(block, 1, label);
	}
	for (index = 0; index < 3; index++) {
		block = le24(inode + 12 + (10 + index) * 3);
		if (block != 0)
			reader_index_ownership(block, index + 1, label);
	}
}

static void
reader_selected_ownership_set(const char *label, unsigned first_inode,
    unsigned second_inode, unsigned third_inode)
{
	uint32_t block, link, free_count = 0;
	unsigned index, count;

	memset(owner, 0, sizeof(owner));
	reader_inode_ownership(V7FS_ROOT_INODE, label);
	if (first_inode != 0 && first_inode != V7FS_ROOT_INODE)
		reader_inode_ownership(first_inode, label);
	if (second_inode != 0 && second_inode != V7FS_ROOT_INODE &&
	    second_inode != first_inode)
		reader_inode_ownership(second_inode, label);
	if (third_inode != 0 && third_inode != V7FS_ROOT_INODE &&
	    third_inode != first_inode && third_inode != second_inode)
		reader_inode_ownership(third_inode, label);
	count = SB_NFREE;
	if (count > 50) {
		fail(label, "free cache exceeds its bounds");
		return;
	}
	link = count == 0 ? 0 : SB_FREE(0);
	for (index = 1; index < count; index++)
		if (claim(SB_FREE(index), 2, label))
			free_count++;
	while (link != 0) {
		if (!claim(link, 2, label))
			break;
		free_count++;
		block = link;
		count = le16(image + (size_t)block * 512);
		if (count > 50) {
			fail(label, "free-list block exceeds its bounds");
			return;
		}
		link = count == 0 ? 0 : le32(image + (size_t)block * 512 + 2);
		for (index = 1; index < count; index++)
			if (claim(le32(image + (size_t)block * 512 + 2 + index * 4),
			    2, label))
				free_count++;
	}
	check(free_count == SB_TFREE, label, "free count differs from free list");
	for (block = SB_ISIZE; block < SB_FSIZE; block++)
		if (owner[block] == 0) {
			fail(label, "data block has no owner");
			break;
		}
}

/* Account selected inode pointers and every raw free-list entry. */
static void
reader_selected_ownership(const char *label, unsigned extra_inode)
{
	reader_selected_ownership_set(label, extra_inode, 0, 0);
}

static void
reader_root_ownership(const char *label)
{
	reader_selected_ownership(label, 0);
}

/*
 * Regression scenarios for the local patches under tools/netbsd-patches,
 * each reported under its own label; check.sh calibrates selected scenarios
 * against reversed patches and targeted mutants.
 */
static void
put16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static int
format_image(int fd, int32_t files, uint8_t fill)
{
	struct v7fs_mount_device device;
	size_t i;

	memset(image, fill, sizeof(image));
	if (lseek(fd, 0, SEEK_SET) != 0)
		return EIO;
	for (i = 0; i < sizeof(image); i += 512)
		if (write(fd, image + i, 512) != 512)
			return EIO;
	memset(&device, 0, sizeof(device));
	device.device.fd = fd;
	device.sectors = SECTORS;
	device.endian = byte_order;
	return v7fs_newfs(&device, files);
}

struct directory_fill {
	unsigned slot;
	v7fs_ino_t target;
};

static int
fill_directory_block(struct v7fs_self *fs, void *context,
    v7fs_daddr_t block, size_t length)
{
	struct directory_fill *fill = context;
	uint8_t buffer[V7FS_BSIZE];
	uint16_t disk_inode;
	unsigned slot;
	char *name;

	if (length != sizeof(buffer))
		return V7FS_ITERATOR_ERROR;
	memset(buffer, 0, sizeof(buffer));
	for (slot = 0; slot < V7FS_BSIZE / sizeof(struct v7fs_dirent);
	    slot++, fill->slot++) {
		disk_inode = V7FS_VAL16(fs, fill->slot < 2 ?
		    V7FS_ROOT_INODE : fill->target);
		memcpy(buffer + slot * sizeof(struct v7fs_dirent), &disk_inode,
		    sizeof(disk_inode));
		name = (char *)(buffer + slot * sizeof(struct v7fs_dirent) +
		    sizeof(disk_inode));
		if (fill->slot < 2)
			strcpy(name, fill->slot == 0 ? "." : "..");
		else if (fill->slot == 2)
			strcpy(name, "seed");
		else
			(void)snprintf(name, V7FS_NAME_MAX, "f%06u", fill->slot);
	}
	return fs->io.write(fs->io.cookie, buffer, block) ? 0 :
	    V7FS_ITERATOR_ERROR;
}

static int
write_parent_fixture(int fd, unsigned blocks)
{
	struct v7fs_self *fs;
	struct v7fs_inode parent, target;
	struct directory_fill fill;
	v7fs_ino_t target_number;
	int error;

	if (blocks == 0 || blocks > 138)
		return EINVAL;
	if ((error = format_image(fd, 64, 0)) != 0)
		return error;
	if ((fs = mount_image(fd)) == NULL)
		return EIO;
	error = make_node(fs, V7FS_ROOT_INODE, "seed", V7FS_IFREG | 0644,
	    NULL, 0, &target_number);
	if (error == 0)
		error = v7fs_inode_load(fs, &parent, V7FS_ROOT_INODE);
	if (error == 0)
		error = v7fs_inode_load(fs, &target, target_number);
	if (error == 0)
		error = v7fs_datablock_expand(fs, &parent,
		    (size_t)blocks * V7FS_BSIZE - parent.filesize);
	fill.slot = 0;
	fill.target = target_number;
	if (error == 0 && v7fs_datablock_foreach(fs, &parent,
	    fill_directory_block, &fill) != V7FS_ITERATOR_END)
		error = EIO;
	if (error == 0) {
		target.nlink = (int16_t)(fill.slot - 2);
		error = v7fs_inode_writeback(fs, &target);
	}
	if (error == 0)
		error = v7fs_inode_writeback(fs, &parent);
	if (error == 0)
		error = v7fs_superblock_writeback(fs);
	v7fs_io_fini(fs);
	return error;
}

static int
load_raw(int fd)
{
	return lseek(fd, 0, SEEK_SET) == 0 &&
	    read(fd, image, sizeof(image)) == (ssize_t)sizeof(image) ? 0 : EIO;
}

static bool
fail_sector_read(void *cookie, uint8_t *buffer, daddr_t sector)
{
	(void)cookie;
	(void)buffer;
	(void)sector;
	return false;
}

static bool
fail_sector_write(void *cookie, uint8_t *buffer, daddr_t sector)
{
	(void)cookie;
	(void)buffer;
	(void)sector;
	return false;
}

static bool (*saved_sector_read)(void *, uint8_t *, daddr_t);
static unsigned sector_reads, failed_read_number;
static daddr_t failed_read_sector;
static bool (*saved_sector_write)(void *, uint8_t *, daddr_t);
static bool fail_inode_write;
static bool fail_after_write;
static unsigned sector_writes, failed_write_number;
static unsigned uncertain_writes;
static unsigned inode_writes, failed_inode_write_number;
static daddr_t failed_write_sector;
static bool fail_sector_once;

static int
accept_inode(struct v7fs_self *fs, void *context, struct v7fs_inode *inode,
    v7fs_ino_t number)
{
	(void)fs;
	(void)context;
	(void)inode;
	(void)number;
	return 0;
}

static bool
fail_selected_read(void *cookie, uint8_t *buffer, daddr_t sector)
{
	if (++sector_reads == failed_read_number) {
		failed_read_sector = sector;
		return false;
	}
	return saved_sector_read(cookie, buffer, sector);
}

static bool
fail_selected_inode_write(void *cookie, uint8_t *buffer, daddr_t sector)
{
	if (fail_inode_write && sector == 2) {
		fail_inode_write = false;
		return false;
	}
	return saved_sector_write(cookie, buffer, sector);
}

static bool
fail_selected_write(void *cookie, uint8_t *buffer, daddr_t sector)
{
	if (++sector_writes == failed_write_number) {
		failed_write_sector = sector;
		if (fail_after_write)
			(void)saved_sector_write(cookie, buffer, sector);
		return false;
	}
	return saved_sector_write(cookie, buffer, sector);
}

static bool
fail_nth_inode_write(void *cookie, uint8_t *buffer, daddr_t sector)
{
	if (sector == 2 && ++inode_writes == failed_inode_write_number)
		return false;
	return saved_sector_write(cookie, buffer, sector);
}

static bool
fail_selected_sector_write(void *cookie, uint8_t *buffer, daddr_t sector)
{
	if (fail_sector_once && sector == failed_write_sector) {
		fail_sector_once = false;
		(void)saved_sector_write(cookie, buffer, sector);
		return false;
	}
	return saved_sector_write(cookie, buffer, sector);
}

static bool
fail_publication_and_restore(void *cookie, uint8_t *buffer, daddr_t sector)
{
	uncertain_writes++;
	if (uncertain_writes == 5) {
		(void)saved_sector_write(cookie, buffer, sector);
		return false;
	}
	if (uncertain_writes == 6)
		return false;
	return saved_sector_write(cookie, buffer, sector);
}

static void
regress_uncertain_publication(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode;
	uint32_t free_before;
	uint8_t buffer[V7FS_BSIZE];
	v7fs_daddr_t root;
	int error, scratch_before;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-uncertain-publication", "format or mount failed");
		return;
	}
	if (v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE) != 0 ||
	    v7fs_datablock_expand(fs, &inode, 10 * V7FS_BSIZE - inode.filesize) != 0) {
		fail("regress-uncertain-publication", "fixture expansion failed");
		v7fs_io_fini(fs);
		return;
	}
	free_before = fs->superblock.total_freeblock;
	scratch_before = scratch_remain(fs);
	saved_sector_write = fs->io.write;
	uncertain_writes = 0;
	fs->io.write = fail_publication_and_restore;
	error = v7fs_datablock_expand(fs, &inode, 2 * V7FS_BSIZE);
	fs->io.write = saved_sector_write;
	root = inode.addr[V7FS_NADDR_INDEX1];
	check(error == EIO && uncertain_writes == 6 &&
	    fs->superblock.total_freeblock == free_before - 3 &&
	    scratch_remain(fs) == scratch_before &&
	    datablock_number_sanity(fs, root), "regress-uncertain-publication",
	    "uncertain publication must stop outer rollback");
	if (datablock_number_sanity(fs, root) &&
	    fs->io.read(fs->io.cookie, buffer, root))
		check(le32(buffer) != 0 && le32(buffer + 4) != 0,
		    "regress-uncertain-publication", "possible children must remain owned");
	else
		fail("regress-uncertain-publication", "index read failed");
	v7fs_io_fini(fs);
}

static void
regress_shrink_regrow(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode;
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-shrink-regrow", "format or mount failed");
		return;
	}
	error = v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE);
	if (error == 0)
		error = v7fs_datablock_expand(fs, &inode, 2 * V7FS_BSIZE - inode.filesize);
	if (error == 0)
		error = v7fs_datablock_contract(fs, &inode, V7FS_BSIZE);
	check(error == 0 && inode.addr[1] == 0, "regress-shrink-regrow",
	    "contraction retained a freed direct pointer");
	if (error == 0)
		error = v7fs_datablock_expand(fs, &inode, V7FS_BSIZE);
	check(error == 0 && inode.filesize == 2 * V7FS_BSIZE,
	    "regress-shrink-regrow", "growth after direct contraction failed");
	check(v7fs_superblock_writeback(fs) == 0, "regress-shrink-regrow",
	    "superblock writeback failed");
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0)
		fail("regress-shrink-regrow", "image reload failed");
	else
		reader_root_ownership("regress-shrink-regrow");
}

static void
regress_shrink_retry(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode;
	uint32_t free_before;
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-shrink-retry", "format or mount failed");
		return;
	}
	if (v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE) != 0 ||
	    v7fs_datablock_expand(fs, &inode, 2 * V7FS_BSIZE - inode.filesize) != 0) {
		fail("regress-shrink-retry", "fixture expansion failed");
		v7fs_io_fini(fs);
		return;
	}
	free_before = fs->superblock.total_freeblock;
	saved_sector_write = fs->io.write;
	sector_writes = 0;
	failed_write_number = 2;
	fail_after_write = false;
	fs->io.write = fail_selected_write;
	error = v7fs_datablock_contract(fs, &inode, V7FS_BSIZE);
	fs->io.write = saved_sector_write;
	check(error == EIO && inode.filesize == 2 * V7FS_BSIZE &&
	    fs->superblock.total_freeblock == free_before, "regress-shrink-retry",
	    "failed detach must restore the prior size and ownership");
	error = v7fs_datablock_contract(fs, &inode, V7FS_BSIZE);
	if (error == 0)
		error = v7fs_datablock_expand(fs, &inode, V7FS_BSIZE);
	check(error == 0 && inode.filesize == 2 * V7FS_BSIZE,
	    "regress-shrink-retry", "shrink retry and regrowth failed");
	check(v7fs_superblock_writeback(fs) == 0, "regress-shrink-retry",
	    "superblock writeback failed");
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0)
		fail("regress-shrink-retry", "image reload failed");
	else
		reader_root_ownership("regress-shrink-retry");
}

static void
regress_shrink_root_retry(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode;
	uint32_t free_before;
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-shrink-root-retry", "format or mount failed");
		return;
	}
	free_before = fs->superblock.total_freeblock;
	if (v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE) != 0 ||
	    v7fs_datablock_expand(fs, &inode, 11 * V7FS_BSIZE - inode.filesize) != 0) {
		fail("regress-shrink-root-retry", "fixture expansion failed");
		v7fs_io_fini(fs);
		return;
	}
	saved_sector_write = fs->io.write;
	inode_writes = 0;
	failed_inode_write_number = 2;
	fs->io.write = fail_nth_inode_write;
	error = v7fs_datablock_contract(fs, &inode, V7FS_BSIZE);
	fs->io.write = saved_sector_write;
	check(error == EIO && inode_writes >= 2,
	    "regress-shrink-root-retry", "root detachment failure was not reported");
	error = v7fs_datablock_contract(fs, &inode, inode.filesize);
	check(error == 0 && inode.filesize == 0 &&
	    fs->superblock.total_freeblock == free_before + 1,
	    "regress-shrink-root-retry", "retry leaked an indirect root");
	check(v7fs_superblock_writeback(fs) == 0, "regress-shrink-root-retry",
	    "superblock writeback failed");
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0)
		fail("regress-shrink-root-retry", "image reload failed");
	else
		reader_root_ownership("regress-shrink-root-retry");
}

static void
regress_shrink_transition(int fd, size_t initial_blocks, size_t final_blocks,
    const char *label)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode;
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail(label, "format or mount failed");
		return;
	}
	error = v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE);
	if (error == 0)
		error = v7fs_datablock_expand(fs, &inode,
		    initial_blocks * V7FS_BSIZE - inode.filesize);
	if (error == 0)
		error = v7fs_datablock_contract(fs, &inode,
		    (initial_blocks - final_blocks) * V7FS_BSIZE);
	if (error == 0)
		error = v7fs_datablock_expand(fs, &inode,
		    (initial_blocks - final_blocks) * V7FS_BSIZE);
	check(error == 0 && inode.filesize == initial_blocks * V7FS_BSIZE,
	    label, "indirect shrink and regrow failed");
	check(v7fs_superblock_writeback(fs) == 0, label, "superblock writeback failed");
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0)
		fail(label, "image reload failed");
	else
		reader_root_ownership(label);
}

static void
regress_shrink_write_failure(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode;
	uint32_t free_before;
	int error, scratch_before;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-shrink-write", "format or mount failed");
		return;
	}
	if (v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE) != 0 ||
	    v7fs_datablock_expand(fs, &inode, 11 * V7FS_BSIZE - inode.filesize) != 0) {
		fail("regress-shrink-write", "fixture expansion failed");
		v7fs_io_fini(fs);
		return;
	}
	free_before = fs->superblock.total_freeblock;
	scratch_before = scratch_remain(fs);
	saved_sector_write = fs->io.write;
	sector_writes = 0;
	failed_write_number = 2;
	fail_after_write = true;
	fs->io.write = fail_selected_write;
	error = v7fs_datablock_contract(fs, &inode, V7FS_BSIZE);
	fs->io.write = saved_sector_write;
	check(error == EIO && sector_writes >= 3 &&
	    fs->superblock.total_freeblock == free_before &&
	    inode.filesize == 11 * V7FS_BSIZE &&
	    scratch_remain(fs) == scratch_before, "regress-shrink-write",
	    "failed unlink must retain the allocated blocks");
	error = v7fs_datablock_contract(fs, &inode, V7FS_BSIZE);
	if (error == 0)
		error = v7fs_datablock_expand(fs, &inode, V7FS_BSIZE);
	check(error == 0 && inode.filesize == 11 * V7FS_BSIZE,
	    "regress-shrink-write", "retry and regrowth failed");
	check(v7fs_superblock_writeback(fs) == 0, "regress-shrink-write",
	    "superblock writeback failed");
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0)
		fail("regress-shrink-write", "image reload failed");
	else
		reader_root_ownership("regress-shrink-write");
}

static void
regress_lookup_full_name(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode root;
	v7fs_ino_t ino;
	char entry[16], result[V7FS_NAME_MAX + 1];
	unsigned index;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-lookup-full-name", "format or mount failed");
		return;
	}
	for (index = 0; index < 29; index++) {
		snprintf(entry, sizeof(entry), "f%02u", index);
		if (make_node(fs, V7FS_ROOT_INODE, entry, V7FS_IFREG | 0644,
		    NULL, 0, &ino) != 0) {
			fail("regress-lookup-full-name", "fixture creation failed");
			v7fs_io_fini(fs);
			return;
		}
	}
	if (make_node(fs, V7FS_ROOT_INODE, "abcdefghijklmn", V7FS_IFREG | 0644,
	    NULL, 0, &ino) != 0 ||
	    v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
		fail("regress-lookup-full-name", "last directory entry creation failed");
	} else {
		check(v7fs_file_lookup_by_number(fs, &root, ino, result) == 0 &&
		    strcmp(result, "abcdefghijklmn") == 0, "regress-lookup-full-name",
		    "lookup must preserve all 14 name bytes");
	}
	v7fs_io_fini(fs);
}

static void
regress_rename_failures(int fd)
{
	unsigned scenario;

	for (scenario = 0; scenario < 4; scenario++) {
		struct v7fs_self *fs;
		struct v7fs_inode source_parent, target_parent;
		v7fs_ino_t source_ino, target_ino, found;
		int error;

		if (format_image(fd, 64, 0) != 0 ||
		    (fs = mount_image(fd)) == NULL) {
			fail("regress-rename", "format or mount failed");
			return;
		}
		if (make_node(fs, V7FS_ROOT_INODE, "src", V7FS_IFDIR | 0755,
		    NULL, 0, &source_ino) != 0 ||
		    make_node(fs, V7FS_ROOT_INODE, "dst", V7FS_IFDIR | 0755,
		    NULL, 0, &target_ino) != 0 ||
		    v7fs_inode_load(fs, &source_parent, V7FS_ROOT_INODE) != 0 ||
		    v7fs_inode_load(fs, &target_parent, target_ino) != 0) {
			fail("regress-rename", "fixture creation failed");
			v7fs_io_fini(fs);
			return;
		}
		saved_sector_read = fs->io.read;
		sector_reads = 0;
		failed_read_number = scenario == 1 ? 2 : scenario == 2 ? 4 : 6;
		if (scenario != 0)
			fs->io.read = fail_selected_read;
		error = v7fs_file_rename(fs, &source_parent, "src", 3,
		    &target_parent, "new", 3);
		fs->io.read = saved_sector_read;
		if (scenario == 0) {
			check(error == 0,
			    "regress-rename", "control rename failed");
		} else {
			check(error == EIO, "regress-rename",
			    "injected rename failure must return EIO");
		}
		if (scenario != 0) {
			check(sector_reads == failed_read_number, "regress-rename",
			    "injected read was not reached exactly once");
			check(scenario == 1 || failed_read_sector ==
			    (daddr_t)target_parent.addr[0], "regress-rename",
			    "target-directory read was not reached");
			check(v7fs_file_lookup_by_name(fs, &source_parent, "src", 3,
			    &found) == 0 && found == source_ino, "regress-rename",
			    "failed lookup removed its source");
			check(v7fs_file_lookup_by_name(fs, &target_parent, "new", 3,
			    &found) == ENOENT, "regress-rename",
			    "failed lookup published its target");
		} else {
			check(v7fs_file_lookup_by_name(fs, &source_parent, "src", 3,
			    &found) == ENOENT &&
			    v7fs_file_lookup_by_name(fs, &target_parent, "new", 3,
			    &found) == 0 && found == source_ino,
			    "regress-rename", "published rename entries differ");
		}
		v7fs_io_fini(fs);
	}
}

static bool
rename_state(struct v7fs_self *fs, v7fs_ino_t source_ino,
    v7fs_ino_t target_parent_ino, int16_t root_nlink,
    int16_t target_nlink, int16_t source_nlink, bool renamed)
{
	struct v7fs_inode root, source, target_parent;
	v7fs_ino_t found, parent;

	if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0 ||
	    v7fs_inode_load(fs, &source, source_ino) != 0 ||
	    v7fs_inode_load(fs, &target_parent, target_parent_ino) != 0)
		return false;
	if (source.nlink != source_nlink || root.nlink !=
	    root_nlink - (renamed ? 1 : 0) || target_parent.nlink !=
	    target_nlink + (renamed ? 1 : 0))
		return false;
	if (renamed)
		return v7fs_file_lookup_by_name(fs, &root, "src", 3, &found) ==
		    ENOENT && v7fs_file_lookup_by_name(fs, &target_parent, "new", 3,
		    &found) == 0 && found == source_ino &&
		    v7fs_file_lookup_by_name(fs, &source, "..", 2, &parent) == 0 &&
		    parent == target_parent_ino;
	return v7fs_file_lookup_by_name(fs, &root, "src", 3, &found) == 0 &&
	    found == source_ino && v7fs_file_lookup_by_name(fs, &target_parent,
	    "new", 3, &found) == ENOENT && v7fs_file_lookup_by_name(fs,
	    &source, "..", 2, &parent) == 0 && parent == V7FS_ROOT_INODE;
}

static void
regress_rename_write_compensation(int fd)
{
	const char *label = "regress-rename-write-compensation";
	struct v7fs_self *fs = NULL;
	struct v7fs_inode source_parent, target_parent;
	v7fs_ino_t source_ino, target_parent_ino;
	int16_t root_nlink, target_nlink, source_nlink;
	unsigned control_writes, failure_write;
	char detail[96];
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL ||
	    make_node(fs, V7FS_ROOT_INODE, "src", V7FS_IFDIR | 0755,
	    NULL, 0, &source_ino) != 0 || make_node(fs, V7FS_ROOT_INODE, "dst",
	    V7FS_IFDIR | 0755, NULL, 0, &target_parent_ino) != 0 ||
	    v7fs_inode_load(fs, &source_parent, V7FS_ROOT_INODE) != 0 ||
	    v7fs_inode_load(fs, &target_parent, target_parent_ino) != 0 ||
	    v7fs_superblock_writeback(fs) != 0) {
		fail(label, "control fixture creation failed");
		if (fs != NULL)
			v7fs_io_fini(fs);
		return;
	}
	root_nlink = source_parent.nlink;
	target_nlink = target_parent.nlink;
	if (v7fs_inode_load(fs, &source_parent, source_ino) != 0) {
		fail(label, "control source load failed");
		v7fs_io_fini(fs);
		return;
	}
	source_nlink = source_parent.nlink;
	if (v7fs_inode_load(fs, &source_parent, V7FS_ROOT_INODE) != 0) {
		fail(label, "control parent reload failed");
		v7fs_io_fini(fs);
		return;
	}
	saved_sector_write = fs->io.write;
	sector_writes = 0;
	failed_write_number = UINT_MAX;
	fail_after_write = false;
	fs->io.write = fail_selected_write;
	error = v7fs_file_rename(fs, &source_parent, "src", 3, &target_parent,
	    "new", 3);
	fs->io.write = saved_sector_write;
	control_writes = sector_writes;
	check(error == 0 && control_writes != 0 && rename_state(fs, source_ino,
	    target_parent_ino, root_nlink, target_nlink, source_nlink, true),
	    label, "control rename failed");
	v7fs_io_fini(fs);

	for (failure_write = 1; failure_write <= control_writes; failure_write++) {
		fs = NULL;
		if (format_image(fd, 64, 0) != 0 ||
		    (fs = mount_image(fd)) == NULL || make_node(fs,
		    V7FS_ROOT_INODE, "src", V7FS_IFDIR | 0755, NULL, 0,
		    &source_ino) != 0 || make_node(fs, V7FS_ROOT_INODE, "dst",
		    V7FS_IFDIR | 0755, NULL, 0, &target_parent_ino) != 0 ||
		    v7fs_inode_load(fs, &source_parent, V7FS_ROOT_INODE) != 0 ||
		    v7fs_inode_load(fs, &target_parent, target_parent_ino) != 0 ||
		    v7fs_superblock_writeback(fs) != 0) {
			fail(label, "failure fixture creation failed");
			if (fs != NULL)
				v7fs_io_fini(fs);
			return;
		}
		root_nlink = source_parent.nlink;
		target_nlink = target_parent.nlink;
		if (v7fs_inode_load(fs, &source_parent, source_ino) != 0) {
			fail(label, "failure source load failed");
			v7fs_io_fini(fs);
			return;
		}
		source_nlink = source_parent.nlink;
		if (v7fs_inode_load(fs, &source_parent, V7FS_ROOT_INODE) != 0) {
			fail(label, "failure parent reload failed");
			v7fs_io_fini(fs);
			return;
		}
		saved_sector_write = fs->io.write;
		sector_writes = 0;
		failed_write_number = failure_write;
		fail_after_write = false;
		fs->io.write = fail_selected_write;
		error = v7fs_file_rename(fs, &source_parent, "src", 3,
		    &target_parent, "new", 3);
		fs->io.write = saved_sector_write;
		snprintf(detail, sizeof(detail),
		    "write %u was not reported and activated", failure_write);
		check(error == EIO && sector_writes >= failure_write, label, detail);
		v7fs_io_fini(fs);
		if (load_raw(fd) != 0)
			fail(label, "recovered image read failed");
		else
			reader_selected_ownership_set(label, source_ino,
			    target_parent_ino, 0);

		fs = mount_image(fd);
		if (fs == NULL || !rename_state(fs, source_ino, target_parent_ino,
		    root_nlink, target_nlink, source_nlink, false) ||
		    v7fs_inode_load(fs, &source_parent,
		    V7FS_ROOT_INODE) != 0 || v7fs_inode_load(fs, &target_parent,
		    target_parent_ino) != 0) {
			snprintf(detail, sizeof(detail),
			    "write %u did not recover the old namespace", failure_write);
			fail(label, detail);
			if (fs != NULL)
				v7fs_io_fini(fs);
			return;
		}
		error = v7fs_file_rename(fs, &source_parent, "src", 3,
		    &target_parent, "new", 3);
		check(error == 0 && rename_state(fs, source_ino, target_parent_ino,
		    root_nlink, target_nlink, source_nlink, true), label,
		    "recovered image did not accept a retry");
		v7fs_io_fini(fs);
	}
}

static bool
rename_replacement_state(struct v7fs_self *fs, v7fs_ino_t source_ino,
    v7fs_ino_t target_parent_ino, v7fs_ino_t replaced_ino,
    int16_t root_nlink, int16_t target_parent_nlink,
    int16_t source_nlink, int16_t replaced_nlink, bool renamed)
{
	struct v7fs_inode root, source, target_parent, replaced;
	v7fs_ino_t found, parent;

	if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0 ||
	    v7fs_inode_load(fs, &source, source_ino) != 0 ||
	    v7fs_inode_load(fs, &target_parent, target_parent_ino) != 0 ||
	    v7fs_inode_load(fs, &replaced, replaced_ino) != 0 ||
	    root.nlink != root_nlink - (renamed ? 1 : 0) ||
	    target_parent.nlink != target_parent_nlink ||
	    source.nlink != source_nlink ||
	    replaced.nlink != (renamed ? 0 : replaced_nlink))
		return false;
	if (renamed)
		return v7fs_file_lookup_by_name(fs, &root, "src", 3, &found) ==
		    ENOENT && v7fs_file_lookup_by_name(fs, &target_parent, "new", 3,
		    &found) == 0 && found == source_ino &&
		    v7fs_file_lookup_by_name(fs, &source, "..", 2, &parent) == 0 &&
		    parent == target_parent_ino;
	return v7fs_file_lookup_by_name(fs, &root, "src", 3, &found) == 0 &&
	    found == source_ino && v7fs_file_lookup_by_name(fs, &target_parent,
	    "new", 3, &found) == 0 && found == replaced_ino &&
	    v7fs_file_lookup_by_name(fs, &source, "..", 2, &parent) == 0 &&
	    parent == V7FS_ROOT_INODE && v7fs_file_lookup_by_name(fs, &replaced,
	    "..", 2, &parent) == 0 && parent == target_parent_ino;
}

static void
regress_rename_replacement_compensation(int fd)
{
	const char *label = "regress-rename-replacement-compensation";
	struct v7fs_self *fs = NULL;
	struct v7fs_inode source_parent, target_parent, inode;
	v7fs_ino_t source_ino, target_parent_ino, replaced_ino;
	int16_t root_nlink, target_parent_nlink, source_nlink, replaced_nlink;
	unsigned control_writes, failure_write;
	char detail[96];
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL ||
	    make_node(fs, V7FS_ROOT_INODE, "src", V7FS_IFDIR | 0755,
	    NULL, 0, &source_ino) != 0 || make_node(fs, V7FS_ROOT_INODE, "dst",
	    V7FS_IFDIR | 0755, NULL, 0, &target_parent_ino) != 0 ||
	    make_node(fs, target_parent_ino, "new", V7FS_IFDIR | 0755,
	    NULL, 0, &replaced_ino) != 0 || v7fs_inode_load(fs, &source_parent,
	    V7FS_ROOT_INODE) != 0 || v7fs_inode_load(fs, &target_parent,
	    target_parent_ino) != 0 || v7fs_superblock_writeback(fs) != 0) {
		fail(label, "control fixture creation failed");
		if (fs != NULL)
			v7fs_io_fini(fs);
		return;
	}
	root_nlink = source_parent.nlink;
	target_parent_nlink = target_parent.nlink;
	if (v7fs_inode_load(fs, &inode, source_ino) != 0) {
		fail(label, "control source load failed");
		v7fs_io_fini(fs);
		return;
	}
	source_nlink = inode.nlink;
	if (v7fs_inode_load(fs, &inode, replaced_ino) != 0) {
		fail(label, "control target load failed");
		v7fs_io_fini(fs);
		return;
	}
	replaced_nlink = inode.nlink;
	saved_sector_write = fs->io.write;
	sector_writes = 0;
	failed_write_number = UINT_MAX;
	fail_after_write = false;
	fs->io.write = fail_selected_write;
	error = v7fs_file_rename(fs, &source_parent, "src", 3, &target_parent,
	    "new", 3);
	fs->io.write = saved_sector_write;
	control_writes = sector_writes;
	check(error == 0 && control_writes != 0 && rename_replacement_state(fs,
	    source_ino, target_parent_ino, replaced_ino, root_nlink,
	    target_parent_nlink, source_nlink, replaced_nlink, true), label,
	    "control replacement failed");
	v7fs_io_fini(fs);

	for (failure_write = 1; failure_write <= control_writes; failure_write++) {
		fs = NULL;
		if (format_image(fd, 64, 0) != 0 ||
		    (fs = mount_image(fd)) == NULL || make_node(fs,
		    V7FS_ROOT_INODE, "src", V7FS_IFDIR | 0755, NULL, 0,
		    &source_ino) != 0 || make_node(fs, V7FS_ROOT_INODE, "dst",
		    V7FS_IFDIR | 0755, NULL, 0, &target_parent_ino) != 0 ||
		    make_node(fs, target_parent_ino, "new", V7FS_IFDIR | 0755,
		    NULL, 0, &replaced_ino) != 0 || v7fs_inode_load(fs,
		    &source_parent, V7FS_ROOT_INODE) != 0 || v7fs_inode_load(fs,
		    &target_parent, target_parent_ino) != 0 ||
		    v7fs_superblock_writeback(fs) != 0 || v7fs_inode_load(fs,
		    &inode, source_ino) != 0) {
			fail(label, "failure fixture creation failed");
			if (fs != NULL)
				v7fs_io_fini(fs);
			return;
		}
		root_nlink = source_parent.nlink;
		target_parent_nlink = target_parent.nlink;
		source_nlink = inode.nlink;
		if (v7fs_inode_load(fs, &inode, replaced_ino) != 0) {
			fail(label, "failure target load failed");
			v7fs_io_fini(fs);
			return;
		}
		replaced_nlink = inode.nlink;
		saved_sector_write = fs->io.write;
		sector_writes = 0;
		failed_write_number = failure_write;
		fail_after_write = false;
		fs->io.write = fail_selected_write;
		error = v7fs_file_rename(fs, &source_parent, "src", 3,
		    &target_parent, "new", 3);
		fs->io.write = saved_sector_write;
		snprintf(detail, sizeof(detail),
		    "write %u was not reported and activated", failure_write);
		check(error == EIO && sector_writes >= failure_write, label, detail);
		v7fs_io_fini(fs);
		if (load_raw(fd) != 0)
			fail(label, "recovered image read failed");
		else
			reader_selected_ownership_set(label, source_ino,
			    target_parent_ino, replaced_ino);
		fs = mount_image(fd);
		if (fs == NULL || !rename_replacement_state(fs, source_ino,
		    target_parent_ino, replaced_ino, root_nlink,
		    target_parent_nlink, source_nlink, replaced_nlink, false) ||
		    v7fs_inode_load(fs, &source_parent, V7FS_ROOT_INODE) != 0 ||
		    v7fs_inode_load(fs, &target_parent, target_parent_ino) != 0) {
			snprintf(detail, sizeof(detail),
			    "write %u did not restore the displaced target", failure_write);
			fail(label, detail);
			if (fs != NULL)
				v7fs_io_fini(fs);
			return;
		}
		error = v7fs_file_rename(fs, &source_parent, "src", 3,
		    &target_parent, "new", 3);
		check(error == 0 && rename_replacement_state(fs, source_ino,
		    target_parent_ino, replaced_ino, root_nlink,
		    target_parent_nlink, source_nlink, replaced_nlink, true), label,
		    "recovered replacement did not accept a retry");
		v7fs_io_fini(fs);
	}
}

static void
regress_rename_hierarchy(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode source_parent, target_parent, victim_before, victim_after;
	v7fs_ino_t source, descendant, victim, found;
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-rename-hierarchy", "format or mount failed");
		return;
	}
	if (make_node(fs, V7FS_ROOT_INODE, "source", V7FS_IFDIR | 0755,
	    NULL, 0, &source) != 0 ||
	    make_node(fs, source, "descendant", V7FS_IFDIR | 0755,
	    NULL, 0, &descendant) != 0 ||
	    make_node(fs, descendant, "victim", V7FS_IFDIR | 0755,
	    NULL, 0, &victim) != 0 ||
	    v7fs_inode_load(fs, &source_parent, V7FS_ROOT_INODE) != 0 ||
	    v7fs_inode_load(fs, &target_parent, descendant) != 0 ||
	    v7fs_inode_load(fs, &victim_before, victim) != 0) {
		fail("regress-rename-hierarchy", "fixture creation failed");
		v7fs_io_fini(fs);
		return;
	}
	error = v7fs_file_rename(fs, &source_parent, "source", 6,
	    &target_parent, "victim", 6);
	check(error == EINVAL, "regress-rename-hierarchy",
	    "descendant move must fail with EINVAL");
	check(v7fs_file_lookup_by_name(fs, &source_parent, "source", 6,
	    &found) == 0 && found == source, "regress-rename-hierarchy",
	    "rejected move removed its source");
	check(v7fs_file_lookup_by_name(fs, &target_parent, "victim", 6,
	    &found) == 0 && found == victim &&
	    v7fs_inode_load(fs, &victim_after, victim) == 0 &&
	    inode_equal(&victim_after, &victim_before),
	    "regress-rename-hierarchy", "rejected move changed its target");
	check(v7fs_superblock_writeback(fs) == 0, "regress-rename-hierarchy",
	    "superblock writeback failed");
	v7fs_io_fini(fs);
	fs = mount_image(fd);
	if (fs == NULL) {
		fail("regress-rename-hierarchy", "fresh mount failed");
	} else if (v7fs_inode_load(fs, &source_parent, V7FS_ROOT_INODE) != 0 ||
	    v7fs_inode_load(fs, &target_parent, descendant) != 0) {
		fail("regress-rename-hierarchy", "fresh inode load failed");
		v7fs_io_fini(fs);
	} else {
		check(v7fs_file_lookup_by_name(fs, &source_parent, "source", 6,
		    &found) == 0 && found == source, "regress-rename-hierarchy",
		    "fresh mount lost the source");
		check(v7fs_file_lookup_by_name(fs, &target_parent, "victim", 6,
		    &found) == 0 && found == victim, "regress-rename-hierarchy",
		    "fresh mount lost the target");
		v7fs_io_fini(fs);
	}
}

static void
regress_create_failure(int fd, bool directory, bool full_parent,
    unsigned failure_write)
{
	struct v7fs_self *fs;
	struct v7fs_inode parent, original;
	struct v7fs_fileattr attr;
	v7fs_ino_t ino;
	uint32_t free_blocks, free_inodes;
	char label[48];
	int error, scratch_before;

	snprintf(label, sizeof(label), "regress-create-%s%s-%u",
	    directory ? "directory" : "file", full_parent ? "-full" : "",
	    failure_write);
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail(label, "format or mount failed");
		return;
	}
	if (v7fs_inode_load(fs, &parent, V7FS_ROOT_INODE) != 0) {
		fail(label, "root inode load failed");
		v7fs_io_fini(fs);
		return;
	}
	if (full_parent && v7fs_datablock_expand(fs, &parent,
	    V7FS_BSIZE - parent.filesize) != 0) {
		fail(label, "parent fixture expansion failed");
		v7fs_io_fini(fs);
		return;
	}
	original = parent;
	free_blocks = fs->superblock.total_freeblock;
	free_inodes = fs->superblock.total_freeinode;
	scratch_before = scratch_remain(fs);
	memset(&attr, 0, sizeof(attr));
	attr.mode = (directory ? V7FS_IFDIR : V7FS_IFREG) | 0755;
	saved_sector_write = fs->io.write;
	sector_writes = 0;
	failed_write_number = failure_write;
	fail_after_write = true;
	fs->io.write = fail_selected_write;
	error = v7fs_file_allocate(fs, &parent, "new", 3, &attr, &ino);
	fs->io.write = saved_sector_write;
	check(error == EIO && sector_writes >= failure_write &&
	    fs->superblock.total_freeblock == free_blocks &&
	    fs->superblock.total_freeinode == free_inodes &&
	    scratch_remain(fs) == scratch_before &&
	    inode_equal(&parent, &original), label,
	    "failed creation must restore inode, blocks, and parent");
	check(v7fs_superblock_writeback(fs) == 0, label,
	    "superblock writeback failed");
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0)
		fail(label, "image reload failed");
	else
		reader_root_ownership(label);
}

static void
regress_symlink_writeback(int fd)
{
	static const char target[] = "destination";
	struct v7fs_self *fs;
	struct v7fs_inode inode;
	v7fs_ino_t ino;
	void *buffer;
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-symlink-writeback", "format or mount failed");
		return;
	}
	if (make_node(fs, V7FS_ROOT_INODE, "link", V7FSBSD_IFLNK | 0777,
	    NULL, 0, &ino) != 0 || v7fs_inode_load(fs, &inode, ino) != 0) {
		fail("regress-symlink-writeback", "fixture creation failed");
		v7fs_io_fini(fs);
		return;
	}
	saved_sector_write = fs->io.write;
	inode_writes = 0;
	failed_inode_write_number = 2;
	fs->io.write = fail_nth_inode_write;
	error = v7fs_file_symlink(fs, &inode, target);
	fs->io.write = saved_sector_write;
	check(error == EIO && inode_writes == failed_inode_write_number,
	    "regress-symlink-writeback",
	    "final inode write failure must be reported");
	check(v7fs_superblock_writeback(fs) == 0,
	    "regress-symlink-writeback", "superblock writeback failed");
	v7fs_io_fini(fs);

	fs = mount_image(fd);
	if (fs == NULL || v7fs_inode_load(fs, &inode, ino) != 0 ||
	    (buffer = scratch_read(fs, inode.addr[0])) == NULL) {
		fail("regress-symlink-writeback", "fresh symlink load failed");
		if (fs != NULL)
			v7fs_io_fini(fs);
		return;
	}
	check(inode.filesize == sizeof(target) && strcmp(buffer, target) == 0,
	    "regress-symlink-writeback",
	    "reported write failure damaged the persisted symlink");
	scratch_free(fs, buffer);
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0)
		fail("regress-symlink-writeback", "image reload failed");
	else
		reader_selected_ownership("regress-symlink-writeback", ino);
}

static void
regress_inode_address_width(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode, original, loaded;
	uint8_t before[V7FS_BSIZE], after[V7FS_BSIZE];
	off_t sector_offset = (off_t)V7FS_ILIST_SECTOR * V7FS_BSIZE;
	unsigned slot;
	char detail[96];
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-inode-address-width", "format or mount failed");
		return;
	}
	if (v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE) != 0) {
		fail("regress-inode-address-width", "root inode load failed");
		v7fs_io_fini(fs);
		return;
	}
	original = inode;
	for (slot = 0; slot < V7FS_NADDR; slot++) {
		inode = original;
		inode.addr[slot] = V7FS_DADDR_MAX;
		error = v7fs_inode_writeback(fs, &inode);
		if (error == 0)
			error = v7fs_inode_load(fs, &loaded, V7FS_ROOT_INODE);
		snprintf(detail, sizeof(detail),
		    "address slot %u did not round-trip the 24-bit maximum", slot);
		check(error == 0 && loaded.addr[slot] == V7FS_DADDR_MAX,
		    "regress-inode-address-width", detail);
		check(v7fs_inode_writeback(fs, &original) == 0,
		    "regress-inode-address-width", "fixture restoration failed");

		if (pread(fd, before, sizeof(before), sector_offset) !=
		    (ssize_t)sizeof(before)) {
			fail("regress-inode-address-width", "inode sector read failed");
			break;
		}
		inode = original;
		inode.addr[slot] = V7FS_DADDR_MAX + 1u;
		error = v7fs_inode_writeback(fs, &inode);
		if (pread(fd, after, sizeof(after), sector_offset) !=
		    (ssize_t)sizeof(after)) {
			fail("regress-inode-address-width", "inode sector reread failed");
			break;
		}
		snprintf(detail, sizeof(detail),
		    "address slot %u truncated outside the 24-bit domain", slot);
		check(error == EFBIG && memcmp(before, after, sizeof(before)) == 0,
		    "regress-inode-address-width", detail);
	}
	v7fs_io_fini(fs);
}

static void
regress_remove_failure(int fd, unsigned failure_kind)
{
	struct v7fs_self *fs;
	struct v7fs_inode parent, original;
	v7fs_ino_t first, second, found;
	uint32_t free_blocks, free_inodes;
	int error;
	const char *label = failure_kind == 0 ? "regress-remove-read" :
	    failure_kind == 1 ? "regress-remove-write" : "regress-remove-contract";

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail(label, "format or mount failed");
		return;
	}
	if (make_node(fs, V7FS_ROOT_INODE, "one", V7FS_IFREG | 0644,
	    NULL, 0, &first) != 0 || make_node(fs, V7FS_ROOT_INODE, "two",
	    V7FS_IFREG | 0644, NULL, 0, &second) != 0 ||
	    v7fs_inode_load(fs, &parent, V7FS_ROOT_INODE) != 0) {
		fail(label, "fixture creation failed");
		v7fs_io_fini(fs);
		return;
	}
	original = parent;
	free_blocks = fs->superblock.total_freeblock;
	free_inodes = fs->superblock.total_freeinode;
	if (failure_kind != 0) {
		saved_sector_write = fs->io.write;
		sector_writes = 0;
		failed_write_number = failure_kind;
		fail_after_write = true;
		fs->io.write = fail_selected_write;
	} else {
		saved_sector_read = fs->io.read;
		sector_reads = 0;
		failed_read_number = 4;
		fs->io.read = fail_selected_read;
	}
	error = v7fs_file_deallocate(fs, &parent, "one", 3);
	if (failure_kind != 0)
		fs->io.write = saved_sector_write;
	else
		fs->io.read = saved_sector_read;
	check(error == EIO && fs->superblock.total_freeblock == free_blocks &&
	    fs->superblock.total_freeinode == free_inodes &&
	    inode_equal(&parent, &original), label,
	    "failed removal must report EIO and retain ownership");
	check(v7fs_file_lookup_by_name(fs, &parent, "one", 3, &found) == 0 &&
	    found == first && v7fs_file_lookup_by_name(fs, &parent, "two", 3,
	    &found) == 0 && found == second, label,
	    "failed removal must preserve both entries");
	v7fs_io_fini(fs);
}

static void
regress_remove_last_block_read(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode parent, original;
	v7fs_ino_t ino, found;
	uint32_t free_before;
	int error;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-remove-last-read", "format or mount failed");
		return;
	}
	if (v7fs_inode_load(fs, &parent, V7FS_ROOT_INODE) != 0 ||
	    v7fs_datablock_expand(fs, &parent, 10 * V7FS_BSIZE - parent.filesize) != 0 ||
	    make_node(fs, V7FS_ROOT_INODE, "entry", V7FS_IFREG | 0644,
	    NULL, 0, &ino) != 0 ||
	    v7fs_inode_load(fs, &parent, V7FS_ROOT_INODE) != 0) {
		fail("regress-remove-last-read", "fixture creation failed");
		v7fs_io_fini(fs);
		return;
	}
	original = parent;
	free_before = fs->superblock.total_freeblock;
	saved_sector_read = fs->io.read;
	sector_reads = 0;
	failed_read_number = 14;
	failed_read_sector = -1;
	fs->io.read = fail_selected_read;
	error = v7fs_directory_remove_entry(fs, &parent, "entry", 5);
	fs->io.read = saved_sector_read;
	check(error == EIO && failed_read_sector == (daddr_t)parent.addr[V7FS_NADDR_INDEX1] &&
	    fs->superblock.total_freeblock == free_before &&
	    inode_equal(&parent, &original),
	    "regress-remove-last-read", "last-block lookup failure changed the directory");
	check(v7fs_file_lookup_by_name(fs, &parent, "entry", 5, &found) == 0 &&
	    found == ino, "regress-remove-last-read", "failed lookup lost the entry");
	v7fs_io_fini(fs);
}

static void
regress_remove_retry(int fd, bool directory, unsigned failure_kind)
{
	struct v7fs_self *fs;
	struct v7fs_inode parent, inode;
	v7fs_ino_t ino, found;
	uint32_t free_blocks, free_inodes;
	int parent_links;
	int error;
	const char *label = failure_kind == 2 ? "regress-rmdir-inode-write" :
	    failure_kind == 1 ? "regress-rmdir-parent-link" :
	    directory ? "regress-rmdir-retry" :
	    "regress-unlink-inode-write";

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail(label, "format or mount failed");
		return;
	}
	if (make_node(fs, V7FS_ROOT_INODE, "entry",
	    (directory ? V7FS_IFDIR : V7FS_IFREG) | 0755, NULL, 0, &ino) != 0 ||
	    v7fs_inode_load(fs, &parent, V7FS_ROOT_INODE) != 0) {
		fail(label, "fixture creation failed");
		v7fs_io_fini(fs);
		return;
	}
	free_blocks = fs->superblock.total_freeblock;
	free_inodes = fs->superblock.total_freeinode;
	parent_links = parent.nlink;
	saved_sector_write = fs->io.write;
	if (directory && failure_kind == 0) {
		failed_write_sector = parent.addr[0];
		fail_sector_once = true;
		fs->io.write = fail_selected_sector_write;
	} else {
		inode_writes = 0;
		failed_inode_write_number = failure_kind == 2 ? 3 : 2;
		fs->io.write = fail_nth_inode_write;
	}
	error = v7fs_file_deallocate(fs, &parent, "entry", 5);
	fs->io.write = saved_sector_write;
	check(error == EIO && fs->superblock.total_freeblock == free_blocks &&
	    fs->superblock.total_freeinode == free_inodes &&
	    parent.nlink == parent_links &&
	    v7fs_file_lookup_by_name(fs, &parent, "entry", 5, &found) == 0 &&
	    found == ino && v7fs_inode_load(fs, &inode, ino) == 0 &&
	    (directory ? inode.filesize == 2 * sizeof(struct v7fs_dirent) :
	    inode.nlink == 1), label, "failed removal must remain retryable");
	check(v7fs_file_deallocate(fs, &parent, "entry", 5) == 0, label,
	    "removal retry failed");
	v7fs_io_fini(fs);
}

static void
regress_indirect_rollback(int fd, unsigned failure_kind)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode, original;
	v7fs_ino_t inos[4];
	uint32_t free_before;
	int error, scratch_before;
	const char *label = failure_kind == 0 ? "regress-indirect-read" :
	    failure_kind == 1 ? "regress-indirect-writeback" :
	    failure_kind == 2 ? "regress-indirect-pointer-write" :
	    "regress-indirect-written-error";

	if (format_image(fd, 64, 0) != 0 || populate(fd, inos) != 0 ||
	    (fs = mount_image(fd)) == NULL) {
		fail(label, "fixture creation or mount failed");
		return;
	}
	if (v7fs_inode_load(fs, &inode, inos[1]) != 0) {
		fail(label, "fixture inode load failed");
		v7fs_io_fini(fs);
		return;
	}
	original = inode;
	free_before = fs->superblock.total_freeblock;
	scratch_before = scratch_remain(fs);
	if (failure_kind == 1) {
		saved_sector_write = fs->io.write;
		fail_inode_write = true;
		fs->io.write = fail_selected_inode_write;
	} else if (failure_kind == 0) {
		saved_sector_read = fs->io.read;
		sector_reads = 0;
		failed_read_number = 5;
		fs->io.read = fail_selected_read;
	} else {
		saved_sector_write = fs->io.write;
		sector_writes = 0;
		failed_write_number = 2;
		fail_after_write = failure_kind == 3;
		fs->io.write = fail_selected_write;
	}
	error = v7fs_datablock_expand(fs, &inode, 2 * V7FS_BSIZE);
	if (failure_kind == 0)
		fs->io.read = saved_sector_read;
	else
		fs->io.write = saved_sector_write;
	check(error == EIO && (failure_kind == 0 ?
	    sector_reads >= failed_read_number : failure_kind == 1 ?
	    !fail_inode_write : sector_writes >= failed_write_number) &&
	    fs->superblock.total_freeblock == free_before &&
	    scratch_remain(fs) == scratch_before &&
	    inode_equal(&inode, &original), label,
	    "failed indirect growth must restore ownership");
	check(v7fs_superblock_writeback(fs) == 0, label,
	    "rollback superblock writeback failed");
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0) {
		fail(label, "image reload failed");
		return;
	}
	memset(owner, 0, sizeof(owner));
	reader();
}

static void
regress_indirect_transition(int fd, size_t original_blocks,
    unsigned failure_read, const char *label)
{
	struct v7fs_self *fs;
	struct v7fs_inode inode, original;
	uint32_t free_before;
	int error, scratch_before;

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail(label, "fixture creation or mount failed");
		return;
	}
	if (v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE) != 0 ||
	    v7fs_datablock_expand(fs, &inode,
	    original_blocks * V7FS_BSIZE - inode.filesize) != 0 ||
	    v7fs_superblock_writeback(fs) != 0) {
		fail(label, "fixture expansion failed");
		v7fs_io_fini(fs);
		return;
	}
	original = inode;
	free_before = fs->superblock.total_freeblock;
	scratch_before = scratch_remain(fs);
	saved_sector_read = fs->io.read;
	sector_reads = 0;
	failed_read_number = failure_read;
	fs->io.read = fail_selected_read;
	error = v7fs_datablock_expand(fs, &inode, 3 * V7FS_BSIZE);
	fs->io.read = saved_sector_read;
	check(error == EIO && sector_reads >= failure_read &&
	    fs->superblock.total_freeblock == free_before &&
	    scratch_remain(fs) == scratch_before &&
	    inode_equal(&inode, &original), label,
	    "failed transition must restore ownership");
	check(v7fs_superblock_writeback(fs) == 0, label,
	    "rollback superblock writeback failed");
	v7fs_io_fini(fs);
	if (load_raw(fd) != 0) {
		fail(label, "image reload failed");
		return;
	}
	reader_root_ownership(label);
}

static void
regress(int fd)
{
	struct v7fs_self *fs;
	struct v7fs_inode root, inode;
	struct v7fs_fileattr attr;
	char name[8];
	v7fs_ino_t ino;
	unsigned i, allocated;
	uint8_t before[512];
	int error;
	struct v7fs_daddr_map address_map;
	const size_t last_addressable = (10u + 128u + 128u * 128u +
	    128u * 128u * 128u) * V7FS_BSIZE;

	check(v7fs_datablock_addr(last_addressable, &address_map) == 0 &&
	    address_map.level == 3 && address_map.index[0] == 127 &&
	    address_map.index[1] == 127 && address_map.index[2] == 127 &&
	    v7fs_datablock_addr(last_addressable + 1, &address_map) == ENOSPC &&
	    v7fs_datablock_addr(SIZE_MAX, &address_map) == ENOSPC,
	    "regress-address-range", "triple-indirect limit is incorrect");
	{
		struct v7fs_inode left, right;

		memset(&left, 0, sizeof(left));
		memset(&right, 0xff, sizeof(right));
		right.filesize = left.filesize;
		for (i = 0; i < V7FS_NADDR; i++)
			right.addr[i] = left.addr[i];
		check(v7fs_inode_data_equal(&left, &right),
		    "regress-inode-data-equal", "unrelated bytes changed comparison");
		right.addr[1] = 1;
		check(!v7fs_inode_data_equal(&left, &right),
		    "regress-inode-data-equal", "changed block was ignored");
	}
	regress_uncertain_publication(fd);
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-stale-successive", "format or mount failed");
	} else if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
		fail("regress-stale-successive", "root inode load failed");
		v7fs_io_fini(fs);
	} else {
		uint32_t free_before = fs->superblock.total_freeblock;
		int count = fs->superblock.nfreeblock;

		root.addr[1] = fs->superblock.freeblock[count - 2];
		root.addr[2] = fs->superblock.freeblock[count - 1];
		error = v7fs_datablock_expand(fs, &root,
		    V7FS_BSIZE + 1 - root.filesize);
		if (error == 0)
			error = v7fs_datablock_expand(fs, &root, V7FS_BSIZE);
		check(error == 0 && fs->superblock.total_freeblock == free_before - 2,
		    "regress-stale-successive", "stale slot blocked later expansion");
		check(v7fs_superblock_writeback(fs) == 0, "regress-stale-successive",
		    "superblock writeback failed");
		v7fs_io_fini(fs);
		if (load_raw(fd) == 0)
			reader_root_ownership("regress-stale-successive");
		else
			fail("regress-stale-successive", "image reload failed");
	}
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-stale-future-allocated", "format or mount failed");
	} else if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
		fail("regress-stale-future-allocated", "root inode load failed");
		v7fs_io_fini(fs);
	} else {
		uint32_t free_before = fs->superblock.total_freeblock;

		root.addr[2] = root.addr[0];
		error = v7fs_datablock_expand(fs, &root,
		    V7FS_BSIZE + 1 - root.filesize);
		check(error == 0 && root.addr[2] == root.addr[0] &&
		    fs->superblock.total_freeblock == free_before - 1,
		    "regress-stale-future-allocated", "future allocated slot blocked growth");
		if (error == 0)
			error = v7fs_datablock_expand(fs, &root, V7FS_BSIZE);
		check(error == EIO && root.addr[2] == root.addr[0] &&
		    fs->superblock.total_freeblock == free_before - 1,
		    "regress-stale-future-allocated", "allocated slot was reused");
		v7fs_io_fini(fs);
	}
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-stale-multiple", "format or mount failed");
	} else if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
		fail("regress-stale-multiple", "root inode load failed");
		v7fs_io_fini(fs);
	} else {
		uint32_t free_before = fs->superblock.total_freeblock;
		int count = fs->superblock.nfreeblock;

		root.addr[1] = fs->superblock.freeblock[count - 2];
		root.addr[2] = root.addr[0];
		error = v7fs_datablock_expand(fs, &root,
		    2 * V7FS_BSIZE + 1 - root.filesize);
		check(error == EIO && root.addr[1] == fs->superblock.freeblock[count - 2] &&
		    root.addr[2] == root.addr[0] &&
		    fs->superblock.total_freeblock == free_before,
		    "regress-stale-multiple", "rejected slot changed prior stale slots");
		root.addr[2] = fs->superblock.freeblock[count - 1];
		error = v7fs_datablock_expand(fs, &root,
		    2 * V7FS_BSIZE + 1 - root.filesize);
		check(error == 0 && fs->superblock.total_freeblock == free_before - 2,
		    "regress-stale-multiple", "free slots blocked multi-block growth");
		check(v7fs_superblock_writeback(fs) == 0, "regress-stale-multiple",
		    "superblock writeback failed");
		v7fs_io_fini(fs);
		if (load_raw(fd) == 0)
			reader_root_ownership("regress-stale-multiple");
		else
			fail("regress-stale-multiple", "image reload failed");
	}
#ifdef STATIC_BUFFER
	if (format_image(fd, 64, 0) != 0) {
		fail("regress-reformat-smaller", "format fixture failed");
	} else {
		struct v7fs_mount_device smaller;

		memset(&smaller, 0, sizeof(smaller));
		smaller.device.fd = fd;
		smaller.sectors = SECTORS / 2;
		smaller.endian = byte_order;
		check(v7fs_newfs(&smaller, 64) == 0, "regress-reformat-smaller",
		    "larger old superblock prevented reformatting");
	}
#endif
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-stale-direct", "format or mount failed");
	} else if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
		fail("regress-stale-direct", "root inode load failed");
		v7fs_io_fini(fs);
	} else {
		v7fs_daddr_t free_block = fs->superblock.freeblock[
		    fs->superblock.nfreeblock - 1];
		uint32_t free_before = fs->superblock.total_freeblock;

		root.addr[1] = free_block;
		error = v7fs_datablock_expand(fs, &root,
		    V7FS_BSIZE + 1 - root.filesize);
		check(error == 0 && root.addr[1] != 0 &&
		    fs->superblock.total_freeblock == free_before - 1,
		    "regress-stale-direct", "free stale slot blocked growth");
		check(v7fs_superblock_writeback(fs) == 0,
		    "regress-stale-direct", "superblock writeback failed");
		v7fs_io_fini(fs);
		if (load_raw(fd) == 0)
			reader_root_ownership("regress-stale-direct");
		else
			fail("regress-stale-direct", "image reload failed");
	}
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-stale-allocated", "format or mount failed");
	} else if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
		fail("regress-stale-allocated", "root inode load failed");
		v7fs_io_fini(fs);
	} else {
		uint32_t free_before = fs->superblock.total_freeblock;

		root.addr[1] = root.addr[0];
		error = v7fs_datablock_expand(fs, &root,
		    V7FS_BSIZE + 1 - root.filesize);
		check(error == EIO && root.addr[1] == root.addr[0] &&
		    fs->superblock.total_freeblock == free_before,
		    "regress-stale-allocated", "allocated slot was reused");
		v7fs_io_fini(fs);
	}
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-stale-free-chain", "format or mount failed");
	} else if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
		fail("regress-stale-free-chain", "root inode load failed");
		v7fs_io_fini(fs);
	} else {
		void *buffer = scratch_read(fs, fs->superblock.freeblock[0]);
		v7fs_daddr_t disk_block = 0;
		uint32_t free_before = fs->superblock.total_freeblock;
		int scratch_before;

		if (buffer != NULL) {
			memcpy(&disk_block, (uint8_t *)buffer +
			    offsetof(struct v7fs_freeblock, freeblock) +
			    sizeof(disk_block), sizeof(disk_block));
			scratch_free(fs, buffer);
		}
		scratch_before = scratch_remain(fs);
		root.addr[1] = V7FS_VAL32(fs, disk_block);
		if (root.addr[1] == 0) {
			fail("regress-stale-free-chain", "free-list fixture failed");
		} else {
			saved_sector_read = fs->io.read;
			fs->io.read = fail_sector_read;
			error = v7fs_datablock_expand(fs, &root,
			    V7FS_BSIZE + 1 - root.filesize);
			fs->io.read = saved_sector_read;
			check(error == EIO && root.addr[1] == V7FS_VAL32(fs, disk_block) &&
			    fs->superblock.total_freeblock == free_before &&
			    scratch_remain(fs) == scratch_before, "regress-stale-free-chain",
			    "free-list read failure was ignored");
			error = v7fs_datablock_expand(fs, &root,
			    V7FS_BSIZE + 1 - root.filesize);
			check(error == 0 && fs->superblock.total_freeblock == free_before - 1,
			    "regress-stale-free-chain",
			    "free block in chained list blocked growth");
		}
		check(v7fs_superblock_writeback(fs) == 0, "regress-stale-free-chain",
		    "superblock writeback failed");
		v7fs_io_fini(fs);
		if (load_raw(fd) == 0)
			reader_root_ownership("regress-stale-free-chain");
		else
			fail("regress-stale-free-chain", "image reload failed");
	}
	regress_shrink_regrow(fd);
	regress_shrink_retry(fd);
	regress_shrink_root_retry(fd);
	regress_shrink_transition(fd, 11, 9, "regress-shrink-single");
	regress_shrink_transition(fd, 139, 137, "regress-shrink-double");
	regress_shrink_write_failure(fd);
	regress_rename_failures(fd);
	regress_rename_write_compensation(fd);
	regress_rename_replacement_compensation(fd);
	regress_rename_hierarchy(fd);
	regress_lookup_full_name(fd);
#ifdef STATIC_BUFFER
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-scratch-exhaustion", "format or mount failed");
	} else {
		void *held[V7FS_SELF_NSCRATCH];
		unsigned slot;

		for (slot = 0; slot < V7FS_SELF_NSCRATCH; slot++) {
			held[slot] = scratch_read(fs, V7FS_ILIST_SECTOR);
			if (held[slot] == NULL)
				break;
		}
		check(slot == V7FS_SELF_NSCRATCH && scratch_remain(fs) == 0,
		    "regress-scratch-exhaustion", "could not hold every scratch slot");
		if (slot == V7FS_SELF_NSCRATCH)
			check(v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE) == EIO,
			    "regress-scratch-exhaustion", "exhaustion did not return EIO");
		while (slot != 0)
			scratch_free(fs, held[--slot]);
		check(scratch_remain(fs) == V7FS_SELF_NSCRATCH,
		    "regress-scratch-exhaustion", "scratch slots were not reusable");
		v7fs_io_fini(fs);
	}
#endif
	if (format_image(fd, 64, 0) != 0) {
		fail("regress-volume-read", "format failed");
	} else {
		struct v7fs_mount_device device;

		memset(&device, 0, sizeof(device));
		device.device.fd = fd;
		device.sectors = SECTORS;
		device.endian = byte_order;
		if (v7fs_io_init(&fs, &device, V7FS_BSIZE) != 0) {
			fail("regress-volume-read", "backend initialization failed");
		} else {
			saved_sector_read = fs->io.read;
			sector_reads = 0;
			failed_read_number = 2;
			fs->io.read = fail_selected_read;
			error = v7fs_superblock_load(fs);
			fs->io.read = saved_sector_read;
			check(error == EIO && sector_reads == 2,
			    "regress-volume-read", "volume read failure must be EIO");
			v7fs_io_fini(fs);
		}
	}
	for (i = 1; i <= 3; i++)
		regress_create_failure(fd, false, false, i);
	for (i = 1; i <= 7; i++)
		regress_create_failure(fd, true, false, i);
	for (i = 2; i <= 4; i++)
		regress_create_failure(fd, false, true, i);
	regress_symlink_writeback(fd);
	regress_inode_address_width(fd);
	regress_remove_failure(fd, 0);
	regress_remove_failure(fd, 1);
	regress_remove_failure(fd, 2);
	regress_remove_last_block_read(fd);
	regress_remove_retry(fd, false, 0);
	regress_remove_retry(fd, true, 0);
	regress_remove_retry(fd, true, 1);
	regress_remove_retry(fd, true, 2);
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-inode-release", "format or mount failed");
	} else {
		bool (*write_sector)(void *, uint8_t *, daddr_t) = fs->io.write;
		uint32_t free_before;

		if (v7fs_inode_allocate(fs, &ino) != 0) {
			fail("regress-inode-release", "inode allocation failed");
		} else {
			memset(&inode, 0, sizeof(inode));
			inode.inode_number = ino;
			inode.mode = V7FS_IFREG | 0644;
			check(v7fs_inode_writeback(fs, &inode) == 0,
			    "regress-inode-release", "fixture inode write failed");
			free_before = fs->superblock.total_freeinode;
			fs->io.write = fail_sector_write;
			error = v7fs_inode_deallocate(fs, ino);
			check(error == EIO, "regress-inode-release",
			    "failed clear must return EIO");
			fs->io.write = write_sector;
			check(fs->superblock.total_freeinode == free_before,
			    "regress-inode-release", "failed clear freed an inode");
			check(v7fs_inode_load(fs, &inode, ino) == 0 &&
			    inode.mode != 0, "regress-inode-release",
			    "failed clear changed the allocated inode");
		}
		v7fs_io_fini(fs);
	}
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-lookup-read", "format or mount failed");
	} else {
		bool (*read_sector)(void *, uint8_t *, daddr_t) = fs->io.read;
		uint32_t free_before = fs->superblock.total_freeinode;

		if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
			fail("regress-lookup-read", "root inode load failed");
		} else {
			memset(&attr, 0, sizeof(attr));
			attr.mode = V7FS_IFREG | 0644;
			check(v7fs_file_lookup_by_number(fs, &root, V7FS_ROOT_INODE,
			    NULL) == 0, "regress-lookup-number", "existing inode must be found");
			check(v7fs_file_lookup_by_number(fs, &root, 65535, NULL) == ENOENT,
			    "regress-lookup-number", "missing inode must return ENOENT");
			fs->io.read = fail_sector_read;
			check(v7fs_ilist_foreach(fs, accept_inode, NULL) == EIO,
			    "regress-ilist-read", "inode list read must return EIO");
			check(v7fs_directory_replace_entry(fs, &root, "..", 2) == EIO,
			    "regress-replace-read", "directory read must return EIO");
			check(v7fs_file_lookup_by_number(fs, &root, V7FS_ROOT_INODE,
			    NULL) == EIO, "regress-lookup-number", "read must return EIO");
			error = v7fs_file_lookup_by_name(fs, &root, "missing", 7, &ino);
			check(error == EIO, "regress-lookup-read",
			    "lookup must return the read failure");
			error = v7fs_file_allocate(fs, &root, "missing", 7, &attr, &ino);
			fs->io.read = read_sector;
			check(error == EIO && fs->superblock.total_freeinode == free_before,
			    "regress-lookup-read", "creation must stop before allocation");
		}
		v7fs_io_fini(fs);
	}

	regress_indirect_rollback(fd, 0);
	regress_indirect_rollback(fd, 1);
	regress_indirect_rollback(fd, 2);
	regress_indirect_rollback(fd, 3);
	regress_indirect_transition(fd, 9, 4, "regress-single-transition");
	regress_indirect_transition(fd, 137, 8, "regress-double-transition");

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-grow-rollback", "format or mount failed");
	} else if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
		fail("regress-grow-rollback", "root inode load failed");
		v7fs_io_fini(fs);
	} else {
		struct v7fs_inode saved = root;
		uint32_t free_before = fs->superblock.total_freeblock;
		int scratch_before = scratch_remain(fs);

		saved_sector_read = fs->io.read;
		sector_reads = 0;
		failed_read_number = 2;
		fs->io.read = fail_selected_read;
		error = v7fs_datablock_expand(fs, &root, 2 * V7FS_BSIZE);
		fs->io.read = saved_sector_read;
		check(error == EIO && sector_reads >= failed_read_number &&
		    fs->superblock.total_freeblock == free_before &&
		    scratch_remain(fs) == scratch_before &&
		    inode_equal(&root, &saved),
		    "regress-grow-rollback", "failed growth must release prior blocks");
		v7fs_io_fini(fs);
	}

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-superblock-writeback", "format or mount failed");
	} else {
		bool (*write_sector)(void *, uint8_t *, daddr_t) = fs->io.write;

		fs->superblock.modified = true;
		fs->io.write = fail_sector_write;
		error = v7fs_superblock_writeback(fs);
		fs->io.write = write_sector;
		check(error == EIO && fs->superblock.modified,
		    "regress-superblock-writeback", "failed write must remain dirty");
		check(v7fs_superblock_writeback(fs) == 0 &&
		    !fs->superblock.modified, "regress-superblock-writeback",
		    "retry must write and clear the dirty flag");
		v7fs_io_fini(fs);
	}

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-grow-writeback", "format or mount failed");
	} else {
		bool (*write_sector)(void *, uint8_t *, daddr_t) = fs->io.write;

		if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0) {
			fail("regress-grow-writeback", "root inode load failed");
		} else {
			size_t size_before = root.filesize;

			fs->io.write = fail_sector_write;
			error = v7fs_datablock_expand(fs, &root, 1);
			fs->io.write = write_sector;
			check(error == EIO && root.filesize == size_before,
			    "regress-grow-writeback", "failed inode writeback");
		}
		v7fs_io_fini(fs);
	}
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-size-overflow", "format or mount failed");
	} else if (v7fs_inode_load(fs, &inode, V7FS_ROOT_INODE) != 0) {
		fail("regress-size-overflow", "inode load failed");
		v7fs_io_fini(fs);
	} else {
		struct v7fs_inode saved = inode;
		uint32_t free_before = fs->superblock.total_freeblock;

		error = v7fs_datablock_size_change(fs, SIZE_MAX, &inode);
		check(error == EFBIG && inode_equal(&inode, &saved) &&
		    fs->superblock.total_freeblock == free_before,
		    "regress-size-overflow", "oversized request changed the inode");
		v7fs_io_fini(fs);
	}

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-freeblock-refill", "format or mount failed");
	} else {
		bool (*read_sector)(void *, uint8_t *, daddr_t) = fs->io.read;
		v7fs_daddr_t block;
		uint32_t free_before = fs->superblock.total_freeblock;

		fs->superblock.nfreeblock = 1;
		fs->io.read = fail_sector_read;
		error = v7fs_datablock_allocate(fs, &block);
		fs->io.read = read_sector;
		check(error == EIO && block == 0 &&
		    fs->superblock.total_freeblock == free_before &&
		    fs->superblock.nfreeblock == 1, "regress-freeblock-refill",
		    "failed free-list refill must restore the cache entry");
		v7fs_io_fini(fs);
	}
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-block-read", "format or mount failed");
	} else {
		bool (*read_sector)(void *, uint8_t *, daddr_t) = fs->io.read;
		v7fs_daddr_t block;
		uint32_t free_before = fs->superblock.total_freeblock;
		int scratch_before = scratch_remain(fs);

		fs->io.read = fail_sector_read;
		error = v7fs_datablock_allocate(fs, &block);
		fs->io.read = read_sector;
		check(error == EIO && block == 0 &&
		    fs->superblock.total_freeblock == free_before &&
		    scratch_remain(fs) == scratch_before, "regress-block-read",
		    "failed block read must release the allocation and scratch");
		v7fs_io_fini(fs);
	}
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-block-write", "format or mount failed");
	} else {
		bool (*write_sector)(void *, uint8_t *, daddr_t) = fs->io.write;
		v7fs_daddr_t block;
		uint32_t free_before = fs->superblock.total_freeblock;
		int scratch_before = scratch_remain(fs);

		fs->io.write = fail_sector_write;
		error = v7fs_datablock_allocate(fs, &block);
		fs->io.write = write_sector;
		check(error == EIO && block == 0 &&
		    fs->superblock.total_freeblock == free_before &&
		    scratch_remain(fs) == scratch_before, "regress-block-write",
		    "failed block write must release the allocation and scratch");
		v7fs_io_fini(fs);
	}

	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-inode-read", "format or mount failed");
	} else {
		bool (*read_sector)(void *, uint8_t *, daddr_t) = fs->io.read;

		fs->superblock.nfreeinode = 0;
		fs->io.read = fail_sector_read;
		error = v7fs_freeinode_update(fs);
		check(error == EIO && fs->superblock.nfreeinode == 0,
		    "regress-inode-read", "failed refill must return EIO");
		if (error == EIO) {
			error = v7fs_inode_allocate(fs, &ino);
			check(error == EIO && ino == 0 &&
			    fs->superblock.nfreeinode == 0, "regress-inode-read",
			    "allocator must propagate failed refill");
		}
		fs->io.read = read_sector;
		v7fs_io_fini(fs);
	}

	/* newfs clears the whole inode list over media that held data. */
	if (format_image(fd, 64, 0xff) != 0 || load_raw(fd) != 0) {
		fail("regress-ilist", "format over filled media failed");
	} else {
		allocated = 0;
		for (ino = 1; ino <= (SB_ISIZE - 2) * 8; ino++)
			allocated += le16(dinode(ino)) != 0;
		check(allocated == 2 && SB_TINODE + 2 == (SB_ISIZE - 2) * 8u,
		    "regress-ilist", "stale inodes survive formatting");
	}

	/* Every inode slot up to the last can be allocated, then none. */
	memset(&attr, 0, sizeof(attr));
	attr.mode = V7FS_IFREG | 0644;
	if (format_image(fd, 16, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-last-inode", "format or mount failed");
	} else {
		for (i = 1; i <= 15; i++) {
			snprintf(name, sizeof(name), "f%u", i);
			if (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0)
				break;
			error = v7fs_file_allocate(fs, &root, name, strlen(name),
			    &attr, &ino);
			if (i <= 14 && error != 0) {
				fail("regress-last-inode",
				    "a free inode slot cannot be allocated");
				break;
			}
			if (i == 15)
				check(error == ENOSPC &&
				    fs->superblock.total_freeinode == 0,
				    "regress-last-inode",
				    "allocation past the last slot misreports");
		}
		if (i == 16) {
			fs->superblock.total_freeinode = 1;
			fs->superblock.nfreeinode = 0;
			error = v7fs_inode_allocate(fs, &ino);
			check(error == ENOSPC && ino == 0 &&
			    fs->superblock.nfreeinode == 0,
			    "regress-empty-inode-cache",
			    "an empty refill must not index the cache");
		}
		v7fs_io_fini(fs);
	}

	/* An inode number past the inode list names no inode. */
	if (format_image(fd, 64, 0) != 0 || (fs = mount_image(fd)) == NULL) {
		fail("regress-inode-bound", "format or mount failed");
	} else {
		/* Inode 785 would sit in sector 100, inside the data area. */
		ino = 785;
		if (lseek(fd, 100 * 512, SEEK_SET) != 100 * 512 ||
		    read(fd, before, sizeof(before)) != (ssize_t)sizeof(before)) {
			fail("regress-inode-bound", "read of sector 100 failed");
		} else {
			check(v7fs_inode_load(fs, &inode, ino) != 0,
			    "regress-inode-bound", "an out-of-list inode loads");
			memset(&inode, 0, sizeof(inode));
			inode.inode_number = ino;
			inode.mode = V7FS_IFREG | 0644;
			check(v7fs_inode_writeback(fs, &inode) != 0,
			    "regress-inode-bound", "an out-of-list inode writes");
			check(load_raw(fd) == 0 &&
			    memcmp(image + 100 * 512, before, sizeof(before)) == 0,
			    "regress-inode-bound", "a data block was overwritten");
		}
		v7fs_io_fini(fs);
	}

	/* A superblock whose free-block cache count is invalid is refused. */
	if (format_image(fd, 64, 0) != 0 || load_raw(fd) != 0) {
		fail("regress-superblock", "format failed");
	} else {
		put16(image + 512 + 6, 0xffff);		/* nfreeblock = -1 */
		if (lseek(fd, 512, SEEK_SET) != 512 ||
		    write(fd, image + 512, 512) != 512) {
			fail("regress-superblock", "superblock write failed");
		} else if ((fs = mount_image(fd)) != NULL) {
			fail("regress-superblock", "a negative nfreeblock mounts");
			v7fs_io_fini(fs);
		}
		put16(image + 512 + 6, 0);		/* empty cache, blocks free */
		if (lseek(fd, 512, SEEK_SET) != 512 ||
		    write(fd, image + 512, 512) != 512) {
			fail("regress-superblock", "superblock write failed");
		} else if ((fs = mount_image(fd)) != NULL) {
			fail("regress-superblock",
			    "an empty block cache with free blocks mounts");
			v7fs_io_fini(fs);
		}
	}
}

static int
write_pattern(const char *dir, const char *name, const uint8_t *data,
    size_t size)
{
	char path[4096];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((f = fopen(path, "wb")) == NULL || fwrite(data, 1, size, f) != size ||
	    fclose(f) != 0) {
		perror(path);
		return 2;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct v7fs_self *fs;
	struct v7fs_inode root;
	v7fs_ino_t inos[4];
	char *end;
	unsigned long parent_blocks;
	int fd, error, verify;

	fill_patterns();
	if (argc == 4 && strcmp(argv[3], "patterns") == 0)
		return write_pattern(argv[1], "hello", hello, sizeof(hello) - 1) ||
		    write_pattern(argv[1], "big", big, sizeof(big)) ||
		    write_pattern(argv[1], "inner", inner, sizeof(inner)) ? 2 : 0;
	verify = argc == 4 && strcmp(argv[3], "verify") == 0;
	if (argc == 4 && strcmp(argv[3], "regress") == 0 &&
	    (strcmp(argv[2], "little") == 0 || strcmp(argv[2], "pdp") == 0)) {
		byte_order = strcmp(argv[2], "pdp") == 0 ?
		    PDP_ENDIAN : LITTLE_ENDIAN;
		if ((fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0600)) == -1) {
			perror(argv[1]);
			return 2;
		}
		regress(fd);
		close(fd);
		if (failures != 0)
			return 1;
		printf("v7fs_core: regression scenarios hold\n");
		return 0;
	}
	if (argc == 5 && strcmp(argv[3], "parent") == 0 &&
	    (strcmp(argv[2], "little") == 0 || strcmp(argv[2], "pdp") == 0)) {
		errno = 0;
		parent_blocks = strtoul(argv[4], &end, 10);
		if (errno != 0 || *end != '\0' || parent_blocks > UINT_MAX)
			return 2;
		byte_order = strcmp(argv[2], "pdp") == 0 ?
		    PDP_ENDIAN : LITTLE_ENDIAN;
		if ((fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0600)) == -1) {
			perror(argv[1]);
			return 2;
		}
		error = write_parent_fixture(fd, (unsigned)parent_blocks);
		if (close(fd) != 0 && error == 0)
			error = errno;
		return error == 0 ? 0 : 1;
	}
	if ((argc != 3 && !verify) || (strcmp(argv[2], "little") != 0 &&
	    strcmp(argv[2], "pdp") != 0)) {
		fprintf(stderr, "usage: v7fs_core_test IMAGE little|pdp [verify]\n"
		    "       v7fs_core_test IMAGE little|pdp regress\n"
		    "       v7fs_core_test IMAGE little|pdp parent BLOCKS\n"
		    "       v7fs_core_test DIR - patterns\n");
		return 2;
	}
	byte_order = strcmp(argv[2], "pdp") == 0 ? PDP_ENDIAN : LITTLE_ENDIAN;
	if (verify) {
		if ((fd = open(argv[1], O_RDWR)) == -1) {
			perror(argv[1]);
			return 2;
		}
	} else {
		if ((fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0600)) == -1 ||
		    ftruncate(fd, (off_t)sizeof(image)) == -1) {
			perror(argv[1]);
			return 2;
		}
		if ((error = populate(fd, inos)) != 0) {
			printf("FAIL populate: core returned error %d\n", error);
			return 1;
		}
	}

	if ((fs = mount_image(fd)) == NULL) {
		printf("FAIL remount: the core cannot mount the image\n");
		return 1;
	}
	core_read(fs, V7FS_ROOT_INODE, "hello", hello, sizeof(hello) - 1, "core-hello");
	core_read(fs, V7FS_ROOT_INODE, "big", big, sizeof(big), "core-big");
	if (verify && (v7fs_inode_load(fs, &root, V7FS_ROOT_INODE) != 0 ||
	    v7fs_file_lookup_by_name(fs, &root, "sub", 3, &inos[2]) != 0)) {
		printf("FAIL core-sub: lookup through the core failed\n");
		return 1;
	}
	core_read(fs, inos[2], "inner", inner, sizeof(inner), "core-inner");
	v7fs_io_fini(fs);

	if (lseek(fd, 0, SEEK_SET) != 0 ||
	    read(fd, image, sizeof(image)) != (ssize_t)sizeof(image)) {
		perror("read image");
		return 2;
	}
	close(fd);
	reader();

	if (failures != 0)
		return 1;
	printf("v7fs_core: %s: core and reader agree on the image\n", argv[2]);
	return 0;
}
