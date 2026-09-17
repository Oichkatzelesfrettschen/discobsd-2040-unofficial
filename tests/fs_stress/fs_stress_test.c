/*
 * Stress the host filesystem library every image is built with.
 *
 * tools/fsutil carries the tree's own UFS implementation: it creates the
 * root image, writes the manifest into it, and checks it. A defect there
 * ships a filesystem the board then has to live with, and the board is a
 * bad place to discover one. This gate exercises the allocation paths in
 * the patterns that break them -- growing a file past each indirection
 * boundary, truncating it back, deleting files out of order so the free
 * list fragments, and filling the volume until it refuses -- and calls
 * the consistency checker after each round.
 *
 * What the gate asserts is not fs_check()'s return value. That function is
 * fsck: it repairs what it finds, salvaging a corrupted free list and
 * relinking lost inodes, and it answers 1 either way. Asserting on it would
 * pass while quietly fixing the damage the gate exists to find. The gate
 * captures the checker's report instead and requires it to be clean --
 * no salvage phase, no duplicate or bad blocks, no unreferenced or
 * partially allocated inodes, no wrong counts -- which is the same thing a
 * person reads the report for.
 *
 * The scope is the host library, not sys/kern/ufs_*.c. The kernel's own
 * filesystem code needs a buffer cache and an inode cache that no gate in
 * tests/kernel supplies yet; this one covers the half of the pair that
 * decides what a board is handed at flash time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "bsdfs.h"

/*
 * The library reads this for its own tracing and fsutil(8) defines it;
 * the gate links the library without the command line front end, so it
 * supplies the flag and leaves it off.
 */
int verbose;

static unsigned checks;
static unsigned failures;

/* Everything the checker prints when it has found something to complain
   about. Phase headers and the summary line are not in this list. */
static const char *complaints[] = {
	"Salvage", "BAD", "DUP", "UNREF", "LINK COUNT", "PARTIALLY",
	"ALLOCATED INODE(S)", "OVERFLOW", "NO MEMORY", "Bad filesystem size",
};

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

/* The image under test. Small enough to fill, large enough to indirect. */
#define IMAGE_KBYTES	512
#define IMAGE_INODES	256
static const char *image_path;

/* A byte pattern a reader can tell apart from a neighbouring file's. */
static void
pattern(unsigned char *buf, unsigned long len, unsigned seed)
{
	unsigned long i;

	for (i = 0; i < len; i++)
		buf[i] = (unsigned char)((seed * 31u + i) & 0xff);
}

static int
pattern_matches(const unsigned char *buf, unsigned long len, unsigned seed)
{
	unsigned long i;

	for (i = 0; i < len; i++)
		if (buf[i] != (unsigned char)((seed * 31u + i) & 0xff))
			return 0;
	return 1;
}

/* Write one file of len bytes filled with the seed's pattern. */
static int
write_file(fs_t *fs, const char *name, unsigned long len, unsigned seed)
{
	fs_file_t file;
	unsigned char *buf;
	int ok;

	buf = malloc(len ? len : 1);
	if (buf == NULL)
		return 0;
	pattern(buf, len, seed);
	if (!fs_file_create(fs, &file, name, 0664)) {
		free(buf);
		return 0;
	}
	ok = len == 0 || fs_file_write(&file, buf, len);
	ok = fs_file_close(&file) && ok;
	free(buf);
	return ok;
}

/* Read a file back and confirm both its length and its contents. */
static int
read_matches(fs_t *fs, const char *name, unsigned long len, unsigned seed)
{
	fs_file_t file;
	unsigned char *buf;
	int ok;

	buf = malloc(len ? len : 1);
	if (buf == NULL)
		return 0;
	if (!fs_file_open(fs, &file, name, 0)) {
		free(buf);
		return 0;
	}
	ok = len == 0 || fs_file_read(&file, buf, len);
	if (ok && file.inode.size != len)
		ok = 0;
	if (ok && len != 0)
		ok = pattern_matches(buf, len, seed);
	fs_file_close(&file);
	free(buf);
	return ok;
}

/*
 * Run the checker with its report captured, and require the report to carry
 * no complaint. On failure the whole report is printed, because which phase
 * complained is the first thing a reader needs.
 */
static void
check_clean(fs_t *fs, int line)
{
	static char report[16384];
	int saved_fd, report_fd;
	FILE *f;
	long len;
	unsigned i;
	int rc;

	checks++;

	/* Capture the report by moving descriptor 1, which leaves the stream
	   object alone; reopening stdout itself disturbs more than the text. */
	fflush(stdout);
	saved_fd = dup(1);
	report_fd = open("fs_stress.report", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (saved_fd < 0 || report_fd < 0) {
		failures++;
		fprintf(stderr, "FAIL %s:%d: cannot capture the report\n",
		    __FILE__, line);
		return;
	}
	dup2(report_fd, 1);
	close(report_fd);

	rc = fs_check(fs);

	fflush(stdout);
	dup2(saved_fd, 1);
	close(saved_fd);

	report[0] = '\0';
	f = fopen("fs_stress.report", "r");
	if (f != NULL) {
		len = (long)fread(report, 1, sizeof report - 1, f);
		report[len < 0 ? 0 : len] = '\0';
		fclose(f);
	}
	remove("fs_stress.report");

	if (rc == 0) {
		failures++;
		printf("FAIL %s:%d: the checker refused the image\n",
		    __FILE__, line);
		printf("%s", report);
		return;
	}
	for (i = 0; i < sizeof complaints / sizeof complaints[0]; i++) {
		if (strstr(report, complaints[i]) != NULL) {
			failures++;
			printf("FAIL %s:%d: the checker reported \"%s\"\n",
			    __FILE__, line, complaints[i]);
			printf("%s", report);
			return;
		}
	}
}

/* A fresh image, checked before anything touches it. */
static int
fresh_image(fs_t *fs)
{
	unlink(image_path);
	if (!fs_create(fs, image_path, IMAGE_KBYTES, 0, IMAGE_INODES)) {
		printf("FAIL: cannot create %s\n", image_path);
		failures++;
		return 0;
	}
	return 1;
}

/*
 * A file that crosses each indirection boundary. BSDFS_BSIZE-sized blocks
 * are direct up to NADDR - 3, then single, then double indirect; a write
 * that spans one of those boundaries is where the block map gets it wrong.
 */
static void
indirection_boundaries(void)
{
	fs_t fs;
	unsigned long sizes[] = {
		1, BSDFS_BSIZE - 1, BSDFS_BSIZE, BSDFS_BSIZE + 1,
		BSDFS_BSIZE * 8, BSDFS_BSIZE * 9, BSDFS_BSIZE * 16 + 3,
	};
	char name[32];
	unsigned i;

	if (!fresh_image(&fs))
		return;

	for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
		snprintf(name, sizeof name, "/span%u", i);
		CHECK(write_file(&fs, name, sizes[i], i + 1));
	}
	check_clean(&fs, __LINE__);

	for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
		snprintf(name, sizeof name, "/span%u", i);
		CHECK(read_matches(&fs, name, sizes[i], i + 1));
	}
	check_clean(&fs, __LINE__);
	fs_close(&fs);

	/* The image is still consistent when reopened from scratch. */
	CHECK(fs_open(&fs, image_path, 1, 0) != 0);
	check_clean(&fs, __LINE__);
	CHECK(read_matches(&fs, "/span6", sizes[6], 7));
	fs_close(&fs);
}

/*
 * Delete files out of the order they were written, so the free list is
 * handed blocks that are not contiguous, then write again into the holes.
 * A free list that loses or double-counts a block shows up in fs_check.
 */
static void
fragmented_free_list(void)
{
	fs_t fs;
	char name[32];
	unsigned i;
	unsigned before, after;

	if (!fresh_image(&fs))
		return;

	for (i = 0; i < 12; i++) {
		snprintf(name, sizeof name, "/frag%u", i);
		CHECK(write_file(&fs, name, BSDFS_BSIZE * 3 + 7, i + 100));
	}
	check_clean(&fs, __LINE__);
	before = fs.tfree;

	/*
	 * Remove every other one, which leaves the free list interleaved.
	 * fs_inode_delete clears the inode but leaves writing it back to the
	 * caller, the way tools/fsutil's own unlink does; without the save
	 * the image keeps an inode the directory no longer names, and the
	 * checker reports it unreferenced.
	 */
	for (i = 0; i < 12; i += 2) {
		fs_inode_t inode;

		snprintf(name, sizeof name, "/frag%u", i);
		CHECK(fs_inode_delete(&fs, &inode, name) != 0);
		CHECK(fs_inode_save(&inode, 1) != 0);
	}
	check_clean(&fs, __LINE__);
	after = fs.tfree;
	CHECK(after > before);

	/* The survivors still read back whole. */
	for (i = 1; i < 12; i += 2) {
		snprintf(name, sizeof name, "/frag%u", i);
		CHECK(read_matches(&fs, name, BSDFS_BSIZE * 3 + 7, i + 100));
	}

	/* Writing back into the holes leaves the image consistent. */
	for (i = 0; i < 12; i += 2) {
		snprintf(name, sizeof name, "/refill%u", i);
		CHECK(write_file(&fs, name, BSDFS_BSIZE * 2, i + 200));
	}
	check_clean(&fs, __LINE__);
	for (i = 1; i < 12; i += 2) {
		snprintf(name, sizeof name, "/frag%u", i);
		CHECK(read_matches(&fs, name, BSDFS_BSIZE * 3 + 7, i + 100));
	}
	fs_close(&fs);
}

/*
 * Fill the volume. A filesystem that runs out of blocks has to say so and
 * stay consistent; one that keeps allocating past its own free count hands
 * the board an image whose blocks belong to two files at once.
 */
static void
fills_and_refuses(void)
{
	fs_t fs;
	char name[32];
	unsigned i;
	int refused = 0;

	if (!fresh_image(&fs))
		return;

	for (i = 0; i < 4000 && !refused; i++) {
		snprintf(name, sizeof name, "/fill%u", i);
		if (!write_file(&fs, name, BSDFS_BSIZE * 4, i + 300))
			refused = 1;
	}
	CHECK(refused);
	CHECK(i < 4000);

	/* Whatever it accepted is still a consistent filesystem. */
	check_clean(&fs, __LINE__);

	/* And what it wrote before the refusal still reads back. */
	CHECK(read_matches(&fs, "/fill0", BSDFS_BSIZE * 4, 300));
	fs_close(&fs);
}

/*
 * An empty file, a file of exactly one block, and names at the length
 * limit: the edges a directory entry and an inode meet.
 */
static void
edges(void)
{
	fs_t fs;
	char longname[BSDFS_MAXNAMLEN + 2];

	if (!fresh_image(&fs))
		return;

	CHECK(write_file(&fs, "/empty", 0, 1));
	CHECK(read_matches(&fs, "/empty", 0, 1));

	CHECK(write_file(&fs, "/oneblock", BSDFS_BSIZE, 2));
	CHECK(read_matches(&fs, "/oneblock", BSDFS_BSIZE, 2));

	longname[0] = '/';
	memset(longname + 1, 'n', BSDFS_MAXNAMLEN);
	longname[BSDFS_MAXNAMLEN + 1] = '\0';
	CHECK(write_file(&fs, longname, 64, 3));
	CHECK(read_matches(&fs, longname, 64, 3));

	check_clean(&fs, __LINE__);
	fs_close(&fs);
}

int
main(int argc, char **argv)
{
	image_path = argc > 1 ? argv[1] : "fs_stress.img";

	indirection_boundaries();
	fragmented_free_list();
	fills_and_refuses();
	edges();

	unlink(image_path);
	if (failures) {
		printf("fs_stress: %u of %u checks failed\n", failures, checks);
		return 1;
	}
	printf("fs_stress: %u checks passed\n", checks);
	return 0;
}
