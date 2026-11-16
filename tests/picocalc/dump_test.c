/*
 * Host test of the crash dump round trip: the kernel's dump stream
 * (sys/arch/rp2040/rp2040/dump.c) writes a dump into a partition image,
 * and savecore(8) (sbin/savecore/savecore.c, compiled with main renamed to
 * savecore_main) validates and saves it from that image as its -f device.
 * The system interfaces savecore.h names are stand-ins here: the free space
 * and the running kernel's image CRC are the test's to set.
 *
 * Known-good: a complete dump is saved as dump.N and info.N, the bounds
 * file advances, and the card's header sector is zeroed only after that.
 * Known-bad: a corrupt segment, header checksum, version, segment count or
 * length, too little space for the dump plus the reserve, and a copy that
 * cannot be written each leave the dump on the card and no file behind; a
 * dump the writer did not finish reads as no dump.
 */

/* The command line renames savecore.c's main; this file keeps its own. */
#undef main

#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/dump.h>

#include "savecore.h"

int savecore_main(int argc, char **argv);
unsigned crc32_update(unsigned crc, const void *buf, unsigned len);

#define	PART_SECTORS	64
#define	IMAGE_CRC	0xc0ffee11U

static unsigned char part[PART_SECTORS][DUMP_SECTOR];
static int fail_at = -1;	/* The sector function fails here. */
static long long stub_free;
static unsigned stub_crc = IMAGE_CRC;
static int failures;
static char dir[64], image[96];

static char message[] = "panic: round trip";
static unsigned char kdata[5000], uarea[3072];
static const struct dump_span spans[3] = {
	{ DUMP_SEG_MESSAGE, message, sizeof(message) - 1 },
	{ DUMP_SEG_KERNEL_DATA, kdata, sizeof(kdata) },
	{ DUMP_SEG_UAREA, uarea, sizeof(uarea) },
};

/* 1 + 1 + 10 + 6 sectors. */
#define	DUMP_BYTES	(18 * DUMP_SECTOR)
#define	NEED		((long long)DUMP_BYTES + 1024 + DUMP_RESERVE_BYTES)

int
dump_device(char *path __attribute__((unused)),
    unsigned size __attribute__((unused)))
{
	return -1;
}

long long
media_bytes(int fd)
{
	struct stat st;

	return fstat(fd, &st) == 0 ? (long long)st.st_size : -1;
}

long long
free_bytes(const char *path __attribute__((unused)))
{
	return stub_free;
}

int
running_image_crc(unsigned *crc)
{
	*crc = stub_crc;
	return 0;
}

static int
put_sector(void *arg __attribute__((unused)), unsigned sector,
    const unsigned char *buf, unsigned len, unsigned *crc)
{
	if ((int)sector == fail_at || sector >= PART_SECTORS)
		return 5;
	memcpy(part[sector], buf, len);
	memset(part[sector] + len, 0, DUMP_SECTOR - len);
	if (crc != NULL)
		*crc = crc32_update(*crc, part[sector], len);
	return 0;
}

static void
check(const char *what, long got, long want)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: %ld, want %ld\n", what, got, want);
		failures++;
	}
}

static void
save_image(void)
{
	FILE *f = fopen(image, "wb");

	if (f == NULL || fwrite(part, sizeof(part), 1, f) != 1 ||
	    fclose(f) != 0) {
		perror(image);
		exit(2);
	}
}

/* A fresh dump in the image; returns dump_write()'s result. */
static int
write_dump(void)
{
	int error;

	memset(part, 0xa5, sizeof(part));
	error = dump_write(put_sector, NULL, PART_SECTORS, spans, 3,
	    IMAGE_CRC, 1700000000U);
	save_image();
	return error;
}

static int
run(const char *opt)
{
	char *argv[6];
	int argc = 0;

	argv[argc++] = "savecore";
	if (opt != NULL)
		argv[argc++] = (char *)opt;
	argv[argc++] = "-f";
	argv[argc++] = image;
	if (opt == NULL || strcmp(opt, "-c") != 0)
		argv[argc++] = dir;
	argv[argc] = NULL;
	optind = 1;
	fflush(stdout);
	return savecore_main(argc, argv);
}

static long
file_size(const char *name)
{
	char path[160];
	struct stat st;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static int
file_has(const char *name, const char *text)
{
	char path[160], buf[2048];
	FILE *f;
	size_t n;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((f = fopen(path, "r")) == NULL)
		return 0;
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	return strstr(buf, text) != NULL;
}

/* The image's header sector as savecore left it: 1 when a dump remains. */
static int
dump_on_card(void)
{
	unsigned char sect[DUMP_SECTOR];
	unsigned magic;
	int fd = open(image, O_RDONLY);

	if (fd < 0 || read(fd, sect, sizeof(sect)) != sizeof(sect)) {
		perror(image);
		exit(2);
	}
	close(fd);
	memcpy(&magic, sect, 4);
	return magic == DUMP_MAGIC;
}

/* Files in dir other than those the saved dumps account for. */
static int
stray_files(int ndumps)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0, i;
	char want[32];

	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.' || strcmp(e->d_name, "bounds") == 0 ||
		    strcmp(e->d_name, "minfree") == 0 ||
		    strcmp(e->d_name, "card.img") == 0)
			continue;
		for (i = 0; i < ndumps; i++) {
			snprintf(want, sizeof(want), "dump.%d", i);
			if (strcmp(e->d_name, want) == 0)
				break;
			snprintf(want, sizeof(want), "info.%d", i);
			if (strcmp(e->d_name, want) == 0)
				break;
		}
		if (i == ndumps)
			n++;
	}
	closedir(d);
	return n;
}

static void
corrupt(long offset, unsigned char value)
{
	int fd = open(image, O_RDWR);

	if (fd < 0 || lseek(fd, offset, SEEK_SET) != offset ||
	    write(fd, &value, 1) != 1) {
		perror(image);
		exit(2);
	}
	close(fd);
}

/*
 * Recompute the header checksum after a field changed, so the check that
 * field meets is the one that refuses it rather than the checksum.
 */
static void
reseal(void)
{
	unsigned char sect[DUMP_SECTOR];
	unsigned n, crc;
	int fd = open(image, O_RDWR);

	if (fd < 0 || read(fd, sect, sizeof(sect)) != sizeof(sect)) {
		perror(image);
		exit(2);
	}
	n = sect[6] | sect[7] << 8;
	if (n > DUMP_MAXSEGMENTS)
		n = DUMP_MAXSEGMENTS;
	memset(sect + 20, 0, 4);
	crc = ~crc32_update(~0U, sect, 24 + n * 16);
	memcpy(sect + 20, &crc, 4);
	if (lseek(fd, 0, SEEK_SET) != 0 ||
	    write(fd, sect, sizeof(sect)) != sizeof(sect)) {
		perror(image);
		exit(2);
	}
	close(fd);
}

/* A dump that savecore must refuse, leaving it on the card. */
static void
refused(const char *what, int ndumps)
{
	char name[96];

	snprintf(name, sizeof(name), "%s: status", what);
	check(name, run(NULL), 1);
	snprintf(name, sizeof(name), "%s: dump kept on the card", what);
	check(name, dump_on_card(), 1);
	snprintf(name, sizeof(name), "%s: files left behind", what);
	check(name, stray_files(ndumps), 0);
}

static void
put_file(const char *name, const char *text)
{
	char path[160];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((f = fopen(path, "w")) == NULL || fputs(text, f) == EOF ||
	    fclose(f) != 0) {
		perror(path);
		exit(2);
	}
}

int
main(void)
{
	unsigned char saved[DUMP_BYTES];
	char path[160];
	FILE *f;
	unsigned i;

	for (i = 0; i < sizeof(kdata); i++)
		kdata[i] = (unsigned char)(i * 11 + 7);
	for (i = 0; i < sizeof(uarea); i++)
		uarea[i] = (unsigned char)(i ^ 0x5c);
	snprintf(dir, sizeof(dir), "%s/dumptest.XXXXXX",
	    getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp");
	if (mkdtemp(dir) == NULL) {
		perror(dir);
		return 2;
	}
	snprintf(image, sizeof(image), "%s/card.img", dir);

	/* The writer's refusals. */
	check("dump into too small a partition",
	    dump_write(put_sector, NULL, 17, spans, 3, 0, 0), ENOSPC);
	check("dump with no spans", dump_write(put_sector, NULL,
	    PART_SECTORS, spans, 0, 0, 0), EINVAL);

	/* A complete dump, saved. */
	stub_free = NEED;
	check("write", write_dump(), 0);
	memcpy(saved, part, sizeof(saved));
	check("save", run(NULL), 0);
	check("dump cleared after the save", dump_on_card(), 0);
	check("dump.0 size", file_size("dump.0"), DUMP_BYTES);
	snprintf(path, sizeof(path), "%s/dump.0", dir);
	if ((f = fopen(path, "rb")) != NULL) {
		unsigned char copy[DUMP_BYTES];

		check("dump.0 read", (long)fread(copy, 1, sizeof(copy), f),
		    DUMP_BYTES);
		fclose(f);
		check("dump.0 matches the card", memcmp(copy, saved,
		    sizeof(copy)) == 0, 1);
	}
	check("info.0 message", file_has("info.0", "message: panic: round "
	    "trip\n"), 1);
	check("info.0 image", file_has("info.0", "kernel image crc32: "
	    "c0ffee11\n"), 1);
	check("info.0 running image", file_has("info.0", "running image "
	    "crc32: c0ffee11\n"), 1);
	check("info.0 segment", file_has("info.0", " 3 "), 1);
	check("bounds after one dump", file_has("bounds", "1\n"), 1);
	check("files after one dump", stray_files(1), 0);

	/* Nothing on the card: nothing saved. */
	check("no dump", run(NULL), 0);
	check("no dump: dump.1", file_size("dump.1"), -1);

	/* A second dump from another kernel than the running one. */
	stub_crc = 0x12345678U;
	write_dump();
	check("second save", run(NULL), 0);
	check("info.1 names the difference", file_has("info.1", "running "
	    "image crc32: 12345678 (differs)\n"), 1);
	check("bounds after two dumps", file_has("bounds", "2\n"), 1);
	stub_crc = IMAGE_CRC;

	/* Damage the checks find. */
	write_dump();
	corrupt(3 * DUMP_SECTOR + 17, 0x00);
	refused("segment byte", 2);
	write_dump();
	corrupt(20, 0x01);
	refused("header checksum", 2);
	write_dump();
	corrupt(4, 2);
	reseal();
	refused("version", 2);
	write_dump();
	corrupt(6, 0);
	reseal();
	refused("no segments", 2);
	write_dump();
	corrupt(6, DUMP_MAXSEGMENTS + 1);
	reseal();
	refused("too many segments", 2);
	write_dump();
	corrupt(17, (DUMP_BYTES >> 8) - 2);
	reseal();
	refused("length short of the table", 2);
	write_dump();
	corrupt(24 + 16 + 8 + 3, 0x01);
	reseal();
	refused("segment past the partition", 2);

	/* Space: the dump, its info file and the reserve, exactly. */
	write_dump();
	stub_free = NEED - 1;
	refused("one byte short", 2);
	put_file("minfree", "0\n");
	stub_free = NEED - DUMP_RESERVE_BYTES - 1;
	refused("one byte short of a zero reserve", 2);
	stub_free = NEED - DUMP_RESERVE_BYTES;
	check("save with a zero reserve", run(NULL), 0);
	check("bounds after three dumps", file_has("bounds", "3\n"), 1);
	snprintf(path, sizeof(path), "%s/minfree", dir);
	unlink(path);
	stub_free = NEED;

	/* A copy that cannot be written leaves the dump on the card. */
	write_dump();
	snprintf(path, sizeof(path), "%s/dump.3.tmp", dir);
	mkdir(path, 0700);
	check("copy blocked: status", run(NULL), 1);
	check("copy blocked: dump kept on the card", dump_on_card(), 1);
	check("copy blocked: bounds", file_has("bounds", "3\n"), 1);
	rmdir(path);

	/* -c clears without saving. */
	check("clear", run("-c"), 0);
	check("clear: dump gone", dump_on_card(), 0);
	check("clear: dump.3", file_size("dump.3"), -1);

	/* A dump the writer did not finish is no dump. */
	fail_at = 9;
	check("unfinished write", write_dump(), EIO);
	fail_at = -1;
	check("unfinished dump", run(NULL), 0);
	check("unfinished dump: dump.3", file_size("dump.3"), -1);

	if (failures == 0) {
		/* Leave the directory for inspection when a check failed. */
		for (i = 0; i < 4; i++) {
			snprintf(path, sizeof(path), "%s/dump.%u", dir, i);
			unlink(path);
			snprintf(path, sizeof(path), "%s/info.%u", dir, i);
			unlink(path);
		}
		snprintf(path, sizeof(path), "%s/bounds", dir);
		unlink(path);
		unlink(image);
		rmdir(dir);
	}
	if (failures != 0) {
		fprintf(stderr, "dump_test: %d failures in %s\n", failures,
		    dir);
		return 1;
	}
	printf("dump_test: ok\n");
	return 0;
}
