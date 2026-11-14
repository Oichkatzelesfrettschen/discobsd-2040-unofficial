/* Seekable read-only inputs for the ARM user-mode reader fixture. */
#define _lseek bn_unused_lseek
#include "../libbn/qemu_support.c"
#undef _lseek

long
_lseek(int fd, long offset, int whence)
{
    return fixup(sys3(19, fd, offset, whence));
}
