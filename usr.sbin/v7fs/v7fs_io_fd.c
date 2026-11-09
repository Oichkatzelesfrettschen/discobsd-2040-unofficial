/*
 * Block I/O backend for the NetBSD v7fs core (sys/fs/v7fs) over a file
 * descriptor: an image file or a raw device, on DiscoBSD or a POSIX host.
 *
 * It replaces NetBSD's two backends, v7fs_io_kern.c (vnode I/O, kmem) and
 * v7fs_io_user.c (mmap of the whole device, malloc), under the interface
 * both implement: v7fs_io_init() and v7fs_io_fini() from v7fs_impl.h.
 *
 * RAM: one static struct v7fs_self (three 512-byte scratch blocks, the
 * in-core superblock and the I/O and byte-order operations) and one static
 * descriptor record, about 2 KB in .bss, and nothing from malloc. The core
 * is built with STATIC_BUFFER, so scratch_read() draws from the three blocks
 * the NetBSD kernel also sizes for, and a request beyond them returns NULL,
 * which every caller turns into EIO. One file system is open at a time; a
 * second v7fs_io_init() before v7fs_io_fini() fails with EBUSY.
 *
 * Every sector number is checked against the device size before the seek,
 * so an image whose block pointers lie past its end reports EIO instead of
 * reading or extending past it. The size is clamped to the sectors whose
 * byte offsets off_t can hold (2 GiB with DiscoBSD's 32-bit off_t).
 */

#include <sys/types.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>	/* SEEK_SET */
#include <string.h>
#include <unistd.h>

#include "v7fs.h"
#include "v7fs_impl.h"
#include "v7fs_endian.h"
#include "v7fs_io_fd.h"

struct fd_io {
	int	fd;
	daddr_t	sectors;
	bool	open;
};

static struct v7fs_self self;
static struct fd_io state;

static bool
fd_transfer(void *cookie, uint8_t *buf, daddr_t sector, bool writing)
{
	const struct fd_io *io = cookie;
	size_t done = 0;
	ssize_t n;

	if (sector < 0 || sector >= io->sectors)
		return false;
	if (lseek(io->fd, (off_t)sector * V7FS_BSIZE, SEEK_SET) == (off_t)-1)
		return false;
	while (done < V7FS_BSIZE) {
		if (writing)
			n = write(io->fd, buf + done, V7FS_BSIZE - done);
		else
			n = read(io->fd, buf + done, V7FS_BSIZE - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return false;
		done += (size_t)n;
	}
	return true;
}

static bool
fd_read(void *cookie, uint8_t *buf, daddr_t sector)
{
	return fd_transfer(cookie, buf, sector, false);
}

static bool
fd_write(void *cookie, uint8_t *buf, daddr_t sector)
{
	return fd_transfer(cookie, buf, sector, true);
}

int
v7fs_io_init(struct v7fs_self **fs, const struct v7fs_mount_device *mount,
    size_t block_size)
{
	if (block_size != V7FS_BSIZE || mount->sectors <= 0)
		return EINVAL;
	if (state.open)
		return EBUSY;

	memset(&self, 0, sizeof(self));
	self.scratch_free = -1;
	self.scratch_remain = V7FS_SELF_NSCRATCH;
	self.endian = mount->endian;
#ifdef V7FS_EI
	v7fs_endian_init(&self);
#endif
	state.fd = mount->device.fd;
	/*
	 * A caller that cannot size a raw device passes V7FS_DADDR_MAX + 1;
	 * every sector this backend seeks to must have its end within off_t.
	 */
	state.sectors = (uint64_t)mount->sectors > V7FS_FD_MAX_SECTORS ?
	    (daddr_t)V7FS_FD_MAX_SECTORS : mount->sectors;
	state.open = true;
	self.io.cookie = &state;
	self.io.read = fd_read;
	self.io.write = fd_write;
	*fs = &self;
	return 0;
}

/*
 * The caller owns the descriptor and its flush: v7fs.c checks fsync()
 * before reporting success, which this void interface cannot return.
 */
void
v7fs_io_fini(struct v7fs_self *fs)
{
	(void)fs;
	state.open = false;
}
