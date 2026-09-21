/*
 * The descriptor and the file offset the gate answers for. dd_shim.h states
 * the input model; this file carries the descriptors, and every descriptor
 * other than DD_FAULT_FD reaches the C library unchanged so the output file
 * a scenario names is a real one.
 */
#include "dd_shim.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * Above the descriptors the gate's own files reach, so the test of a
 * descriptor against it is the whole dispatch.
 */
#define DD_FAULT_FD 501

struct dd_trace *dd_trace;
jmp_buf dd_jump;
int dd_jump_armed;

/*
 * Private to the process that reads: fork(2) gives each scenario its own
 * copy, while the trace stays in the shared mapping the parent reads.
 */
static off_t dd_fault_pos;
static unsigned dd_served;

void
dd_shim_init(void)
{
    dd_trace = mmap(NULL, sizeof(*dd_trace), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (dd_trace == MAP_FAILED) {
        perror("dd_contracts: mmap");
        exit(2);
    }
    dd_shim_reset();
}

void
dd_shim_reset(void)
{
    dd_trace->len = 0;
    dd_trace->text[0] = '\0';
    dd_fault_pos = 0;
    dd_served = 0;
}

/*
 * The mark the read limit leaves, read back by the parent after waitpid(2).
 */
int
dd_overran(void)
{
    return (strchr(dd_trace->text, DD_OVERRUN) != NULL);
}

static void
dd_note(char tag, long value)
{
    char item[32];
    int n;

    n = snprintf(item, sizeof item, "%c%ld ", tag, value);
    if (n < 0 || dd_trace->len + (unsigned)n >= DD_TRACE_MAX)
        return;
    memcpy(dd_trace->text + dd_trace->len, item, (size_t)n);
    dd_trace->len += (unsigned)n;
    dd_trace->text[dd_trace->len] = '\0';
}

int
dd_open(const char *path, int flags, ...)
{
    va_list ap;
    mode_t mode = 0;

    if (strcmp(path, DD_FAULT_INPUT) == 0) {
        dd_fault_pos = 0;
        return (DD_FAULT_FD);
    }
    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = (mode_t)va_arg(ap, int);
    va_end(ap);
    return (open(path, flags, mode));
}

ssize_t
dd_read(int fd, void *buf, size_t n)
{
    long blk;

    if (fd != DD_FAULT_FD)
        return (read(fd, buf, n));

    blk = (long)(dd_fault_pos / DD_BLOCK);
    /*
     * The end of the input ends the copy, so the limit returns it: the
     * child leaves a status and a marked trace for the parent instead of
     * reading the same block until something outside the gate kills it.
     */
    if (++dd_served > DD_READ_LIMIT) {
        if (!dd_overran())
            dd_note(DD_OVERRUN, blk);
        return (0);
    }
    if (blk >= DD_BLOCKS) {
        dd_note('z', blk);
        return (0);
    }
    if (blk == DD_BAD_BLOCK) {
        dd_note('e', blk);
        errno = EIO;
        return (-1);
    }
    if (n > DD_BLOCK)
        n = DD_BLOCK;
    memset(buf, 'A' + (int)blk, n);
    dd_fault_pos += (off_t)n;
    dd_note('r', blk);
    return ((ssize_t)n);
}

off_t
dd_lseek(int fd, off_t off, int whence)
{
    if (fd != DD_FAULT_FD)
        return (lseek(fd, off, whence));

    switch (whence) {
    case SEEK_SET:
        dd_fault_pos = off;
        break;
    case SEEK_CUR:
        dd_fault_pos += off;
        break;
    default:
        errno = EINVAL;
        return ((off_t)-1);
    }
    dd_note('s', (long)off);
    return (dd_fault_pos);
}

/*
 * A scenario that runs in a child leaves its status to waitpid(2), so the
 * unarmed case really exits. An operand check runs in the gate's own
 * process and arms dd_jump first, so a rejected operand returns a verdict
 * rather than ending the run.
 */
_Noreturn void
dd_exit(int status)
{
    if (dd_jump_armed) {
        dd_jump_armed = 0;
        longjmp(dd_jump, status + 1);
    }
    _exit(status);
}
