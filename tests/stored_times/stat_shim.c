/*
 * Host stand-ins for the file-status and clock calls of a program built with
 * a four-byte signed time_t, the view the target programs have by default.
 *
 * The DiscoBSD kernel stores a file time as an unsigned 32-bit count and
 * returns it in a signed long, so a time after January 2038 reads back as a
 * negative number. A 32-bit glibc refuses such a time (EOVERFLOW), so the
 * stat family here reads the 64-bit time through statx() and narrows it to
 * time_t by modular conversion, which reproduces the target's bit pattern.
 * __wrap_time() returns the count in STORED_TIMES_NOW when that variable is
 * set, so a test can place the present anywhere in the stored range. A test
 * builds the program under test with
 *
 *   -D'stat(p,b)=stored_times_stat(p,b)'
 *   -D'lstat(p,b)=stored_times_lstat(p,b)'
 *   -D'fstat(d,b)=stored_times_fstat(d,b)'
 *
 * where the program calls the function by name (a function-like macro
 * leaves "struct stat" alone) and links with -Wl,--wrap=time.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>

static int
fill_status(const struct statx *source, struct stat *status)
{
	*status = (struct stat){0};
	status->st_dev = makedev(source->stx_dev_major, source->stx_dev_minor);
	status->st_ino = (ino_t)source->stx_ino;
	status->st_mode = source->stx_mode;
	status->st_nlink = source->stx_nlink;
	status->st_uid = source->stx_uid;
	status->st_gid = source->stx_gid;
	status->st_rdev = makedev(source->stx_rdev_major,
	    source->stx_rdev_minor);
	status->st_size = (off_t)source->stx_size;
	status->st_blksize = (blksize_t)source->stx_blksize;
	status->st_blocks = (blkcnt_t)source->stx_blocks;
	status->st_atime = (time_t)source->stx_atime.tv_sec;
	status->st_mtime = (time_t)source->stx_mtime.tv_sec;
	status->st_ctime = (time_t)source->stx_ctime.tv_sec;
	return 0;
}

static int
status_at(int descriptor, const char *path, int flags, struct stat *status)
{
	struct statx source;

	if (statx(descriptor, path, flags, STATX_BASIC_STATS, &source) < 0)
		return -1;
	return fill_status(&source, status);
}

int
stored_times_stat(const char *path, struct stat *status)
{
	return status_at(AT_FDCWD, path, 0, status);
}

int
stored_times_lstat(const char *path, struct stat *status)
{
	return status_at(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, status);
}

int
stored_times_fstat(int descriptor, struct stat *status)
{
	return status_at(descriptor, "", AT_EMPTY_PATH, status);
}

/*
 * time() under STORED_TIMES_NOW. The program is linked with
 * -Wl,--wrap=time -Wl,--wrap=__time64, which sends its call to one of these
 * (a 64-bit time_t view binds time() to the symbol __time64).
 */
time_t
__wrap_time(time_t *result)
{
	const char *text = getenv("STORED_TIMES_NOW");
	struct timespec now;

	if (text != NULL && *text != '\0') {
		now.tv_sec = (time_t)strtoul(text, NULL, 10);
	} else if (clock_gettime(CLOCK_REALTIME, &now) < 0) {
		return (time_t)-1;
	}
	if (result != NULL)
		*result = now.tv_sec;
	return now.tv_sec;
}

time_t
__wrap___time64(time_t *result)
{
	return __wrap_time(result);
}
