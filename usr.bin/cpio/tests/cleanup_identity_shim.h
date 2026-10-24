#ifndef CPIO_CLEANUP_IDENTITY_SHIM_H
#define CPIO_CLEANUP_IDENTITY_SHIM_H

#include <sys/types.h>

int unlinkat_beneath(int root_descriptor, const char *path,
    int expected_descriptor);
int cpio_test_close(int descriptor);
ssize_t cpio_test_write(int descriptor, const void *buffer,
    size_t byte_count);

#endif
