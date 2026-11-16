/*
 * savecore -- copy a crash dump off the PicoCalc's SD card.
 *
 * The kernel writes the dump (machine/dump.h) to the card's partition of
 * type DUMP_PART_TYPE when it panics. savecore validates the header, the
 * table and every segment checksum while copying, publishes dump.N and an
 * info.N text file under the directory atomically, advances the directory's
 * bounds file, and only then zeroes the header sector, the order of 4.4BSD
 * savecore: a dump leaves the card only once a complete copy is on stable
 * storage. It copies only when the file system keeps the reserve free
 * afterwards (DUMP_RESERVE_BYTES, or the minfree file's kilobytes).
 *
 * The functions sysdep.c supplies (dump_device, media_bytes, free_bytes,
 * running_image_crc) are the system interfaces; tests/picocalc/dump_test.c
 * links this file with stand-ins for them and drives main() on the host.
 */
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/dump.h>

#include "savecore.h"

#define	INFO_BYTES	1024		/* An info file's size bound. */
#define	MESSAGE_MAX	80		/* Message bytes info.N repeats. */

static const char *dir;
static int verbose;

static const unsigned crc32_nibble[16] = {
	0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
	0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
	0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
	0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU,
};

/* zlib's crc32() continued over len bytes; start from and finish with ~0. */
static unsigned
crc32_run(unsigned crc, const unsigned char *p, unsigned len)
{
	while (len-- != 0) {
		crc ^= *p++;
		crc = (crc >> 4) ^ crc32_nibble[crc & 15];
		crc = (crc >> 4) ^ crc32_nibble[crc & 15];
	}
	return crc;
}

static void
usage(void)
{
	fprintf(stderr, "usage: savecore [-cv] [-f device] directory\n");
	exit(1);
}

/* Read a decimal number from dir/name; fallback when it does not exist. */
static long
read_number(const char *name, long fallback)
{
	char path[256], text[16];
	long value;
	int fd, n;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((fd = open(path, O_RDONLY)) < 0)
		return fallback;
	n = read(fd, text, sizeof(text) - 1);
	close(fd);
	if (n <= 0)
		return fallback;
	text[n] = '\0';
	value = strtol(text, NULL, 10);
	return value < 0 ? fallback : value;
}

/* Write, fsync and close; 0 or -1 with errno set. */
static int
write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	ssize_t n;

	while (len != 0) {
		if ((n = write(fd, p, len)) <= 0)
			return -1;
		p += n;
		len -= n;
	}
	return 0;
}

static int
finish_file(int fd)
{
	int error = fsync(fd);

	if (close(fd) != 0)
		error = -1;
	return error;
}

/*
 * Check sector 0: magic, version, table size, checksum, and a length that
 * matches the table and fits the partition. Returns NULL or the reason.
 */
static const char *
check_header(const unsigned char *sect, long long media)
{
	struct dump_header h;
	struct dump_segment s;
	unsigned long long sectors = 1;
	unsigned crc, i;
	unsigned char copy[DUMP_SECTOR];

	memcpy(&h, sect, sizeof(h));
	if (h.dh_version != DUMP_VERSION)
		return "unknown version";
	if (h.dh_nsegments == 0 || h.dh_nsegments > DUMP_MAXSEGMENTS)
		return "bad segment count";
	memcpy(copy, sect, DUMP_SECTOR);
	memset(copy + offsetof(struct dump_header, dh_crc), 0, 4);
	crc = ~crc32_run(~0U, copy, sizeof(h) + h.dh_nsegments * sizeof(s));
	if (crc != h.dh_crc)
		return "header checksum mismatch";
	for (i = 0; i < h.dh_nsegments; i++) {
		memcpy(&s, sect + sizeof(h) + i * sizeof(s), sizeof(s));
		if (s.ds_bytes > media)
			return "segment longer than the partition";
		sectors += DUMP_SECTORS(s.ds_bytes);
	}
	if (h.dh_bytes != sectors * DUMP_SECTOR)
		return "length disagrees with the table";
	if ((long long)h.dh_bytes > media)
		return "longer than the partition";
	return NULL;
}

/* Zero the header sector: the dump is no longer on the card. */
static int
clear_dump(int dev)
{
	unsigned char zero[DUMP_SECTOR];

	memset(zero, 0, sizeof(zero));
	if (lseek(dev, 0, SEEK_SET) != 0 ||
	    write(dev, zero, sizeof(zero)) != sizeof(zero) || fsync(dev) != 0)
		return -1;
	return 0;
}

/*
 * Copy dh_bytes from the device to fd, checking each segment's CRC, and
 * keep up to MESSAGE_MAX bytes of the first message segment. The raw
 * device takes transfers that start on a kilobyte, so the copy moves two
 * sectors at a time and a last odd sector alone. Returns NULL or the
 * reason.
 */
static const char *
copy_dump(int dev, int fd, const unsigned char *sect0, char *message)
{
	struct dump_header h;
	struct dump_segment s;
	unsigned char buf[2 * DUMP_SECTOR];
	unsigned seg = 0, left = 0, crc = 0, off, len, n, i;
	static char why[64];

	memcpy(&h, sect0, sizeof(h));
	memset(&s, 0, sizeof(s));
	message[0] = '\0';
	for (off = 0; off < h.dh_bytes; off += n) {
		n = h.dh_bytes - off < sizeof(buf) ? h.dh_bytes - off :
		    sizeof(buf);
		if (lseek(dev, (off_t)off, SEEK_SET) != (off_t)off ||
		    read(dev, buf, n) != (ssize_t)n)
			return "read error";
		/* Sector 0 is the header check_header() accepted. */
		for (i = off == 0; i < n / DUMP_SECTOR; i++) {
			while (left == 0) {
				/* The next segment with bytes starts here. */
				if (seg >= h.dh_nsegments)
					return "data beyond the last segment";
				memcpy(&s, sect0 + sizeof(h) + seg * sizeof(s),
				    sizeof(s));
				if (s.ds_bytes == 0 && s.ds_crc != 0)
					goto badsum;
				if (s.ds_bytes == 0)
					seg++;
				left = s.ds_bytes;
				crc = ~0U;
			}
			len = left < DUMP_SECTOR ? left : DUMP_SECTOR;
			crc = crc32_run(crc, buf + i * DUMP_SECTOR, len);
			if (s.ds_kind == DUMP_SEG_MESSAGE && message[0] == '\0') {
				memcpy(message, buf + i * DUMP_SECTOR,
				    len < MESSAGE_MAX ? len : MESSAGE_MAX);
				message[len < MESSAGE_MAX ? len : MESSAGE_MAX] =
				    '\0';
			}
			left -= len;
			if (left == 0) {
				if (~crc != s.ds_crc)
					goto badsum;
				seg++;
			}
		}
		if (write_all(fd, buf, n) != 0)
			return strerror(errno);
	}
	while (left == 0 && seg < h.dh_nsegments) {
		memcpy(&s, sect0 + sizeof(h) + seg * sizeof(s), sizeof(s));
		if (s.ds_bytes != 0)
			break;
		seg++;
	}
	if (left != 0 || seg != h.dh_nsegments)
		return "shorter than its table";
	return NULL;
badsum:
	snprintf(why, sizeof(why), "segment %u checksum mismatch", seg);
	return why;
}

/* Write info.N's text into fd. */
static int
write_info(int fd, const unsigned char *sect0, const char *message,
    unsigned running, int have_running)
{
	struct dump_header h;
	struct dump_segment s;
	char text[INFO_BYTES];
	size_t used;
	unsigned i;

	memcpy(&h, sect0, sizeof(h));
	used = snprintf(text, sizeof(text),
	    "message: %s\nkernel image crc32: %08x\nrunning image crc32: ",
	    message, h.dh_image_crc);
	if (have_running)
		used += snprintf(text + used, sizeof(text) - used, "%08x%s\n",
		    running, running == h.dh_image_crc ? "" : " (differs)");
	else
		used += snprintf(text + used, sizeof(text) - used,
		    "unknown\n");
	used += snprintf(text + used, sizeof(text) - used,
	    "time: %u\nbytes: %u\nsegments: kind address bytes crc32\n",
	    h.dh_time, h.dh_bytes);
	for (i = 0; i < h.dh_nsegments && used < sizeof(text); i++) {
		memcpy(&s, sect0 + sizeof(h) + i * sizeof(s), sizeof(s));
		used += snprintf(text + used, sizeof(text) - used,
		    "  %u %08x %u %08x\n", s.ds_kind, s.ds_address,
		    s.ds_bytes, s.ds_crc);
	}
	if (used >= sizeof(text))
		used = sizeof(text) - 1;
	return write_all(fd, text, used);
}

int
main(int argc, char **argv)
{
	unsigned char sect0[DUMP_SECTOR];
	char device[64], dumppath[256], infopath[256], tmp[260], tmp2[260];
	char number[24];
	char message[MESSAGE_MAX + 1];
	const char *devname = NULL, *why;
	struct dump_header h;
	long long media, avail, reserve;
	unsigned running;
	int clear = 0, have_running, dev, fd, dfd, ch;
	long bounds;

	while ((ch = getopt(argc, argv, "cf:v")) != -1) {
		switch (ch) {
		case 'c':
			clear = 1;
			break;
		case 'f':
			devname = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
	if (argc - optind > 1 || (!clear && argc - optind != 1))
		usage();
	dir = argv[optind];

	if (devname == NULL) {
		if (dump_device(device, sizeof(device)) != 0) {
			fprintf(stderr, "savecore: no dump partition on the SD "
			    "card\n");
			return 1;
		}
		devname = device;
	}
	if ((dev = open(devname, O_RDWR)) < 0) {
		fprintf(stderr, "savecore: %s: %s\n", devname,
		    strerror(errno));
		return 1;
	}
	if (read(dev, sect0, DUMP_SECTOR) != DUMP_SECTOR) {
		fprintf(stderr, "savecore: %s: cannot read\n", devname);
		return 1;
	}
	memcpy(&h, sect0, sizeof(h));
	if (h.dh_magic != DUMP_MAGIC) {
		if (verbose)
			printf("savecore: no dump on %s\n", devname);
		return 0;
	}
	if (clear) {
		if (clear_dump(dev) != 0) {
			fprintf(stderr, "savecore: %s: %s\n", devname,
			    strerror(errno));
			return 1;
		}
		printf("savecore: dump on %s cleared\n", devname);
		return 0;
	}
	media = media_bytes(dev);
	if ((why = check_header(sect0, media)) != NULL) {
		fprintf(stderr, "savecore: %s: dump header invalid: %s\n",
		    devname, why);
		return 1;
	}

	reserve = read_number("minfree", -1);
	reserve = reserve < 0 ? (long long)DUMP_RESERVE_BYTES : reserve * 1024;
	if ((avail = free_bytes(dir)) < 0) {
		fprintf(stderr, "savecore: %s: %s\n", dir, strerror(errno));
		return 1;
	}
	if (avail < (long long)h.dh_bytes + INFO_BYTES + reserve) {
		fprintf(stderr, "savecore: %s: %lld bytes free, %lld needed "
		    "for the dump and the reserve; dump left on %s\n", dir,
		    avail, (long long)h.dh_bytes + INFO_BYTES + reserve,
		    devname);
		return 1;
	}

	bounds = read_number("bounds", 0);
	snprintf(dumppath, sizeof(dumppath), "%s/dump.%ld", dir, bounds);
	snprintf(infopath, sizeof(infopath), "%s/info.%ld", dir, bounds);
	snprintf(tmp, sizeof(tmp), "%s.tmp", dumppath);
	snprintf(tmp2, sizeof(tmp2), "%s.tmp", infopath);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
		fprintf(stderr, "savecore: %s: %s\n", tmp, strerror(errno));
		return 1;
	}
	if ((why = copy_dump(dev, fd, sect0, message)) != NULL ||
	    finish_file(fd) != 0) {
		fprintf(stderr, "savecore: %s: %s; dump left on %s\n", tmp,
		    why != NULL ? why : strerror(errno), devname);
		unlink(tmp);
		return 1;
	}
	have_running = running_image_crc(&running) == 0;
	if ((fd = open(tmp2, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0 ||
	    write_info(fd, sect0, message, running, have_running) != 0 ||
	    finish_file(fd) != 0) {
		fprintf(stderr, "savecore: %s: %s; dump left on %s\n", tmp2,
		    strerror(errno), devname);
		unlink(tmp);
		unlink(tmp2);
		return 1;
	}
	if (rename(tmp, dumppath) != 0 || rename(tmp2, infopath) != 0) {
		fprintf(stderr, "savecore: %s: %s; dump left on %s\n", dir,
		    strerror(errno), devname);
		return 1;
	}
	snprintf(tmp, sizeof(tmp), "%s/bounds.tmp", dir);
	snprintf(tmp2, sizeof(tmp2), "%s/bounds", dir);
	snprintf(number, sizeof(number), "%ld\n", bounds + 1);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0 ||
	    write_all(fd, number, strlen(number)) != 0 ||
	    finish_file(fd) != 0 || rename(tmp, tmp2) != 0 ||
	    (dfd = open(dir, O_RDONLY)) < 0 || finish_file(dfd) != 0) {
		fprintf(stderr, "savecore: %s: %s; dump left on the card\n",
		    tmp2, strerror(errno));
		return 1;
	}
	if (clear_dump(dev) != 0) {
		fprintf(stderr, "savecore: %s: cannot clear the dump: %s\n",
		    devname, strerror(errno));
		return 1;
	}
	printf("savecore: %s: %s\n", dumppath, message);
	return 0;
}
