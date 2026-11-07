/*
 * The two halves of the host fsck.
 *
 * sbin/fsck compiles against the tree's own headers, where struct stat,
 * FILE, struct fstab and struct passwd have the target's layout, and the host
 * C library cannot be handed any of them. fsck_glue.c is the half that sees
 * the tree's headers: it takes the calls the Makefile renames
 * (fsck_rename.h) and answers them from the three functions below.
 * fsck_host.c is the half that sees the host's headers and implements those
 * three. Neither half includes the other's headers, so this file declares
 * only plain integers and pointers.
 */
#ifndef FSCK_GLUE_H
#define FSCK_GLUE_H

/* Fill the type, device and raw-device numbers of path; 0 on success. */
int	fsck_host_stat(const char *path, unsigned *mode, unsigned *dev,
	    unsigned *rdev);

/* Flush the host's stdout, and read one byte of the host's stdin. */
void	fsck_host_flush(void);
int	fsck_host_getc(void);

#endif /* FSCK_GLUE_H */
