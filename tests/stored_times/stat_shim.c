/*
 * Host stand-ins for the file-status and clock calls of a program built with
 * a four-byte signed time_t, the view the target programs have by default.
 *
 * The DiscoBSD kernel stores a file time as an unsigned 32-bit count and
 * returns it in a signed long, so a time after January 2038 reads back as a
 * negative number. A 32-bit glibc refuses such a time (EOVERFLOW), so the
 * stat family here (Linux only; the four-byte view builds only there) reads
 * the 64-bit time through statx() and narrows it to time_t by modular
 * conversion, which reproduces the target's bit pattern. Only that view needs
 * them, so a build of any other view defines STORED_TIMES_NO_STATX and takes
 * the portable half of this file, which is the whole file on a host without
 * statx().
 *
 * stored_times_time() returns the count in STORED_TIMES_NOW when that
 * variable is set, so a test can place the present anywhere in the stored
 * range. A test builds this file and the program under test with
 *
 *   -D'time(t)=stored_times_time(t)'
 *   -D'stat(p,b)=stored_times_stat(p,b)'        (four-byte view only)
 *   -D'lstat(p,b)=stored_times_lstat(p,b)'      (four-byte view only)
 *   -D'fstat(d,b)=stored_times_fstat(d,b)'      (four-byte view only)
 *
 * where the program calls the function by name; a function-like macro
 * leaves "struct stat" alone. The 64-bit time_t view of glibc binds time()
 * to the symbol __time64 through an assembler label on the declaration, and
 * the shim, compiled with the same macro, defines the function under that
 * label, so one mechanism serves every view and needs no linker option.
 */
#if defined(__linux__) && !defined(STORED_TIMES_NO_STATX)
#define STORED_TIMES_STATX 1
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#ifdef STORED_TIMES_STATX
#include <sys/sysmacros.h>
#endif

#ifdef STORED_TIMES_STATX
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

#endif /* STORED_TIMES_STATX */

/* time() under STORED_TIMES_NOW, else the clock. */
time_t
stored_times_time(time_t *result)
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
