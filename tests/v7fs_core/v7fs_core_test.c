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
 * Exit status: 0 when every check holds, 1 when a check fails (each
 * failure prints "FAIL <label>: ..."), 2 when the environment fails.
 */

#include <sys/types.h>
#include <sys/param.h>	/* BYTE_ORDER, LITTLE_ENDIAN, PDP_ENDIAN */
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
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

/*
 * v7fs_freeinode_update() walks a uint16_t pointer to the superblock's
 * freeinode cache, which the packed struct places at offset 210. The pointer
 * is aligned when that member's address in struct v7fs_self is even; check.sh
 * compiles v7fs_superblock.c without -Waddress-of-packed-member on the
 * strength of this assertion, and repeats it for the ARM target when the
 * cross compiler is present.
 */
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

int
main(int argc, char **argv)
{
	struct v7fs_self *fs;
	v7fs_ino_t inos[4];
	int fd, error;

	if (argc != 3 || (strcmp(argv[2], "little") != 0 &&
	    strcmp(argv[2], "pdp") != 0)) {
		fprintf(stderr, "usage: v7fs_core_test IMAGE little|pdp\n");
		return 2;
	}
	byte_order = strcmp(argv[2], "pdp") == 0 ? PDP_ENDIAN : LITTLE_ENDIAN;
	fill_patterns();
	if ((fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0600)) == -1 ||
	    ftruncate(fd, (off_t)sizeof(image)) == -1) {
		perror(argv[1]);
		return 2;
	}
	if ((error = populate(fd, inos)) != 0) {
		printf("FAIL populate: core returned error %d\n", error);
		return 1;
	}

	if ((fs = mount_image(fd)) == NULL) {
		printf("FAIL remount: the core cannot mount the image\n");
		return 1;
	}
	core_read(fs, V7FS_ROOT_INODE, "hello", hello, sizeof(hello) - 1, "core-hello");
	core_read(fs, V7FS_ROOT_INODE, "big", big, sizeof(big), "core-big");
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
