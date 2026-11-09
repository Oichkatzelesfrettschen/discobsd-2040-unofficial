/*
 * sbin/newfs_v7fs/main.c reports progress through NetBSD fsck's progress
 * bar (sbin/fsck/progress.h). Its callers here, usr.sbin/v7fs/v7fs.c and
 * tests/v7fs_core/v7fs_core_test.c, define both functions as silent;
 * struct progress_arg comes from newfs_v7fs.h.
 */
#ifndef V7FS_PROGRESS_H
#define V7FS_PROGRESS_H

struct progress_arg;
void progress(const struct progress_arg *);
void progress_done(void);

#endif /* !V7FS_PROGRESS_H */
