/*
 * sbin/newfs_v7fs/main.c reports progress through NetBSD fsck's progress
 * bar (sbin/fsck/progress.h). v7fs_core_test.c defines these as silent
 * functions; struct progress_arg comes from newfs_v7fs.h.
 */
#ifndef V7FS_CORE_PROGRESS_H
#define V7FS_CORE_PROGRESS_H

struct progress_arg;
void progress(const struct progress_arg *);
void progress_done(void);

#endif /* !V7FS_CORE_PROGRESS_H */
