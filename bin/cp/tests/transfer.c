#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int destination = -1;
static int interrupted_read, interrupted_write;

static int
mode(const char *name)
{
    const char *value = getenv("CP_TEST");
    return value && strcmp(value, name) == 0;
}

ssize_t
cp_read(int fd, void *buf, size_t size)
{
    if (mode("interrupt") && !interrupted_read++) {
        errno = EINTR;
        return -1;
    }
    return read(fd, buf, size);
}

ssize_t
cp_write(int fd, const void *buf, size_t size)
{
    destination = fd;
    if (mode("interrupt") && !interrupted_write++) {
        errno = EINTR;
        return -1;
    }
    if (mode("zero"))
        return 0;
    if (mode("full")) {
        errno = ENOSPC;
        return -1;
    }
    if (mode("short") && size > 7)
        size = 7;
    return write(fd, buf, size);
}

int
cp_close(int fd)
{
    int result = close(fd);
    if (mode("close") && fd == destination) {
        errno = EIO;
        return -1;
    }
    return result;
}
