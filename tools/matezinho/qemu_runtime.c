/*
 * Runtime support for running MATEzinho as a Thumb-1 (ARMv6-M) program under
 * qemu-arm user-mode emulation. The program links newlib from the
 * arm-none-eabi toolchain and supplies the few Linux EABI system calls
 * newlib needs, including file seeks and rename, plus a bounded break area
 * carved from .bss. The desktop is compiled for
 * -mcpu=cortex-m0plus.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

enum { SYS_EXIT_GROUP = 248, SYS_READ = 3, SYS_WRITE = 4, SYS_OPEN = 5, SYS_CLOSE = 6 };

static long
sys3(long n, long a, long b, long c)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;

	__asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
	return r0;
}

static long
fixup(long r)
{
	if (r < 0 && r > -4096) {
		errno = (int)-r;
		return -1;
	}
	return r;
}

int
_open(const char *path, int flags, int mode)
{
	/* Translate newlib creation/truncation bits to Linux EABI. */
	int linux_flags = flags & 3;
	if (flags & 0x200) linux_flags |= 64;
	if (flags & 0x400) linux_flags |= 512;
	if (flags & 0x800) linux_flags |= 128;
	return (int)fixup(sys3(SYS_OPEN, (long)path, linux_flags, mode));
}

int
_read(int fd, char *buf, int len)
{
	return (int)fixup(sys3(SYS_READ, fd, (long)buf, len));
}

int
_write(int fd, const char *buf, int len)
{
	return (int)fixup(sys3(SYS_WRITE, fd, (long)buf, len));
}

int
_close(int fd)
{
	return (int)fixup(sys3(SYS_CLOSE, fd, 0, 0));
}

int
_fstat(int fd, struct stat *st)
{
	(void)fd;
	st->st_mode = S_IFCHR;
	return 0;
}

int
_isatty(int fd)
{
	(void)fd;
	return 1;
}

long
_lseek(int fd, long off, int whence)
{
	return fixup(sys3(19, fd, off, whence));
}

int
_getpid(void)
{
	return 1;
}

int
_kill(int pid, int sig)
{
	(void)pid;
	(void)sig;
	errno = EINVAL;
	return -1;
}

void
_exit(int status)
{
	for (;;)
		(void)sys3(SYS_EXIT_GROUP, status, 0, 0);
}

static char heap_area[16u << 10] __attribute__((aligned(8)));
static size_t heap_used;

void *
_sbrk(ptrdiff_t incr)
{
	void *p;

	if (incr < 0 || heap_used + (size_t)incr > sizeof(heap_area)) {
		errno = ENOMEM;
		return (void *)-1;
	}
	p = heap_area + heap_used;
	heap_used += (size_t)incr;
	return p;
}

/* newlib's __libc_init_array and __libc_fini_array call these. */
void
_init(void)
{
}

void
_fini(void)
{
}

extern int main(int, char **);
extern void __libc_init_array(void);
extern void exit(int);

/* Arguments join the statically bounded image before main starts. */
static char argument_bytes[4096];
static char *argument_vector[128];

void
c_start(long *sp)
{
    char **source = (char **)(sp + 1);
    size_t used = 0;
    int argc = (int)sp[0], i;

    /* Linux supplies the initial strings outside the simulated window.
     * Copy every application-visible argument into bounded resident state. */
    if(argc < 1 || argc >= 128) _exit(71);
    for(i = 0; i < argc; i++) {
        const char *p = source[i];
        argument_vector[i] = argument_bytes + used;
        do {
            if(used == sizeof(argument_bytes)) _exit(71);
            argument_bytes[used++] = *p;
        } while(*p++);
    }
    argument_vector[argc] = 0;
    __libc_init_array();
    exit(main(argc, argument_vector));
}

char mz_stack[8192] __attribute__((aligned(8), used));

__attribute__((naked)) void
_start(void)
{
	__asm__ volatile("mov r0, sp\n\tldr r1, =mz_stack+8192\n\tmov sp, r1\n\tbl c_start\n");
}

int _unlink(const char *p) { return (int)fixup(sys3(10, (long)p, 0, 0)); }
int _link(const char *a, const char *b) {
    return (int)fixup(sys3(9, (long)a, (long)b, 0));
}
int rename(const char *a, const char *b) {
    return (int)fixup(sys3(38, (long)a, (long)b, 0));
}
