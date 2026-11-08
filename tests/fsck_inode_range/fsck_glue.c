/*
 * The tree half of the host fsck: it answers the calls fsck_rename.h moves
 * aside, over the tree's own struct stat, FILE, struct fstab and struct
 * passwd. See fsck_glue.h.
 *
 * fsck stats "/" and the image to decide whether the image is the root
 * device. An image file has no raw device number, so st_rdev is zero, and
 * st_dev is raised by one so that a root filesystem whose host device number
 * is zero cannot compare equal to it. Neither value is used otherwise.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fstab.h>
#include <pwd.h>

#include "fsck_glue.h"

/* The tree's stream table: fsck names the three streams and passes them on. */
struct _iobuf _iob[3];

int
fsck_stat(const char *path, struct stat *sb)
{
	unsigned mode, dev, rdev;

	if (fsck_host_stat(path, &mode, &dev, &rdev) < 0)
		return -1;
	*sb = (struct stat){ 0 };
	sb->st_mode = mode;
	sb->st_dev = (dev_t)(dev + 1);
	sb->st_rdev = 0;
	return 0;
}

int
fsck_fflush(FILE *stream)
{
	(void)stream;
	fsck_host_flush();
	return 0;
}

int
fsck_fgetc(FILE *stream)
{
	(void)stream;
	return fsck_host_getc();
}

int
fsck_setvbuf(FILE *stream, char *buf, int mode, unsigned size)
{
	(void)stream;
	(void)buf;
	(void)mode;
	(void)size;
	return 0;
}

struct passwd *
fsck_getpwuid(int uid)
{
	(void)uid;
	return NULL;
}

/*
 * The checklist belongs to the host and fsck is given an image on its
 * command line, so main() needs a first entry to proceed and never walks
 * the list.
 */
struct fstab *
fsck_getfsent(void)
{
	static struct fstab entry;

	return &entry;
}

int
fsck_setfsent(void)
{
	return 1;
}

void
fsck_endfsent(void)
{
}

int
fsck_setpassent(int stayopen)
{
	(void)stayopen;
	return 1;
}

int
fsck_fork(void)
{
	return -1;
}

int
fsck_wait(int *status)
{
	(void)status;
	return -1;
}

void
fsck_sync(void)
{
}

/*
 * setup() asks reply() whether to check a file that is not a device, and in
 * preen mode reply() ends fsck. The gate checks image files in preen mode, as
 * the boot does a device, so setup.c is compiled with this answer for that
 * one prompt.
 */
int
fsck_accept_image(char *prompt)
{
	(void)prompt;
	return 1;
}
