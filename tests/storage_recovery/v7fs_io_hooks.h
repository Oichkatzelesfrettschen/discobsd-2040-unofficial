#ifndef STORAGE_V7FS_IO_HOOKS_H
#define STORAGE_V7FS_IO_HOOKS_H

#include <sys/types.h>
#include <unistd.h>

/* Host fortified declarations retain their symbols before hook substitution. */
ssize_t storage_test_read(int, void *, size_t);
ssize_t storage_test_write(int, const void *, size_t);
off_t storage_test_seek(int, off_t, int);

#define read storage_test_read
#define write storage_test_write
#define lseek storage_test_seek

#endif
