/*
 * savecore's system interfaces on the RP2040 (savecore.h): the SD card's
 * partition table, the disk ioctls, statfs(2) and machdep.image_crc.
 */
#include <sys/types.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <machine/cpu.h>
#include <machine/dump.h>

#include "savecore.h"

/*
 * The first MBR entry of type DUMP_PART_TYPE, the rule sddump() applies:
 * entry i is minor i + 1 of the card, /dev/rsd0a to /dev/rsd0d.
 */
int
dump_device(char *path, unsigned size)
{
	unsigned char sect[DUMP_SECTOR];
	struct diskpart dp;
	int fd, i, n;

	if ((fd = open("/dev/rsd0", O_RDONLY)) < 0)
		return -1;
	n = read(fd, sect, sizeof(sect));
	close(fd);
	if (n != sizeof(sect) || sect[510] != 0x55 || sect[511] != 0xaa)
		return -1;
	for (i = 0; i < 4; i++) {
		memcpy(&dp, &sect[446 + i * 16], sizeof(dp));
		if (dp.dp_type == DUMP_PART_TYPE && dp.dp_nsectors != 0) {
			snprintf(path, size, "/dev/rsd0%c", 'a' + i);
			return 0;
		}
	}
	return -1;
}

long long
media_bytes(int fd)
{
	struct stat st;
	int kbytes;

	if (ioctl(fd, DIOCGETMEDIASIZE, &kbytes) == 0)
		return (long long)kbytes * 1024;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
		return st.st_size;
	return -1;
}

long long
free_bytes(const char *dir)
{
	struct statfs fs;

	if (statfs(dir, &fs) != 0)
		return -1;
	return (long long)fs.f_bavail * fs.f_bsize;
}

int
running_image_crc(unsigned *crc)
{
	int mib[2] = { CTL_MACHDEP, CPU_IMAGE_CRC };
	size_t len = sizeof(*crc);

	return sysctl(mib, 2, crc, &len, NULL, 0) == 0 &&
	    len == sizeof(*crc) ? 0 : -1;
}
