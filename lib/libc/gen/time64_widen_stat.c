#define _TIME_BITS 64
#include "time64.h"

/* The three stat wrappers share this copy; every field but a time is equal. */
void
__time64_widen_stat(struct stat *wide, const struct __stat32 *narrow)
{
    wide->st_dev = narrow->st_dev;
    wide->st_ino = narrow->st_ino;
    wide->st_mode = narrow->st_mode;
    wide->st_nlink = narrow->st_nlink;
    wide->st_uid = narrow->st_uid;
    wide->st_gid = narrow->st_gid;
    wide->st_rdev = narrow->st_rdev;
    wide->st_size = narrow->st_size;
    wide->st_atime = (time_t)narrow->st_atime;
    wide->st_mtime = (time_t)narrow->st_mtime;
    wide->st_ctime = (time_t)narrow->st_ctime;
    wide->st_blksize = narrow->st_blksize;
    wide->st_blocks = narrow->st_blocks;
    wide->st_flags = narrow->st_flags;
}
