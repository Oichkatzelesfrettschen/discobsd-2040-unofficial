/*
 * Driver for the host build of bin/df: it runs the program's raw-device
 * path, ufs_df(), over an image file and leaves the "df -i" line on stdout.
 *
 * ufs_df() reads the superblock of the file it is given and reports the
 * counts the kernel's statfs would (sys/kern/ufs_syscalls2.c). The program
 * reaches it only for a character or block special file, which an image is
 * not, so the driver calls it directly. The Makefile builds bin/df/df.c
 * against the tree's headers at ILP32 and renames main, sync and fprintf:
 * main because the driver has its own, sync because a gate must not flush
 * the host's disks, and fprintf because include/stdio.h spells stderr as
 * &_iob[2], a FILE the host's libc does not own.
 *
 * df.c asks getmntinfo() for the mount table both to size its columns and to
 * name the mount point of the file; the image is mounted nowhere, so the
 * table here is empty.
 */
#include <sys/param.h>
#include <sys/mount.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The tree's stream table: df.c names stderr and hands it to df_fprintf. */
struct _iobuf _iob[3];

/* df.c reads the tree's errno, a plain int the host's libc does not set. */
int	df_errno;

extern int iflag;
void	ufs_df(char *file, int maxwidth);
int	df_fprintf(FILE *stream, const char *fmt, ...);
void	df_sync(void);

int
getmntinfo(struct statfs **mntbufp, int flags)
{
	(void)mntbufp;
	(void)flags;
	return (0);
}

void
df_sync(void)
{
}

/* df.c reports an unreadable superblock on stderr; keep it visible. */
int
df_fprintf(FILE *stream, const char *fmt, ...)
{
	char text[256];
	va_list ap;
	int n;

	(void)stream;
	va_start(ap, fmt);
	n = vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	if (n > 0)
		(void)write(2, text, (size_t)n);
	return (n);
}

int
main(int argc, char **argv)
{
	/* ufs_df() copies MNAMELEN bytes from the name it is given. */
	static char name[MNAMELEN];

	if (argc != 2 || strlen(argv[1]) >= sizeof name) {
		(void)df_fprintf(stderr, "usage: df_host IMAGE\n");
		return (2);
	}
	strcpy(name, argv[1]);
	iflag = 1;
	ufs_df(name, 0);
	return (0);
}
