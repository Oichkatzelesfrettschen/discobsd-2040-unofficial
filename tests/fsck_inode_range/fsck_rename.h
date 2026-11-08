/*
 * Forced into every sbin/fsck translation unit, ahead of the tree's headers.
 * Each name below belongs to a service the host C library implements over
 * layouts the tree's headers do not share (struct stat, FILE, struct fstab,
 * struct passwd), or to a system-wide effect a gate must not have (fork,
 * wait, sync). fsck_glue.c defines the replacements. stat is renamed through
 * a function-like macro so that "struct stat" keeps its tag.
 */
#ifndef FSCK_RENAME_H
#define FSCK_RENAME_H

#define stat(path, sb)		fsck_stat(path, sb)
#define fflush			fsck_fflush
#define fgetc			fsck_fgetc
#define setvbuf			fsck_setvbuf
#define getpwuid		fsck_getpwuid
#define getfsent		fsck_getfsent
#define setfsent		fsck_setfsent
#define endfsent		fsck_endfsent
#define setpassent		fsck_setpassent
#define fork			fsck_fork
#define wait			fsck_wait
#define sync			fsck_sync

#endif /* FSCK_RENAME_H */
