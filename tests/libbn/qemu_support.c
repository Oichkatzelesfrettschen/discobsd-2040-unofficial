/*
 * Runtime support for running bn_test as a Thumb-1 (ARMv6-M) program under
 * qemu-arm user-mode emulation. The program links newlib from the
 * arm-none-eabi toolchain and supplies the few Linux EABI system calls
 * newlib needs: open, read, write, close and exit_group, plus a break area
 * carved from .bss. The code under test is the same bn.c, compiled for
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
	/* newlib's O_RDONLY is 0 on both sides; the test opens read-only. */
	return (int)fixup(sys3(SYS_OPEN, (long)path, flags, mode));
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
	(void)fd;
	(void)off;
	(void)whence;
	return 0;
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

static char heap_area[32u << 20] __attribute__((aligned(8)));
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

/*
 * newlib's exit() flushes and closes every newlib stream, which pulls
 * fclose and fflush into the link. A test that supplies its own stdio (the
 * tree's, under the same names) defines QEMU_SUPPORT_EXIT_DIRECT and leaves
 * through _exit() so those two symbols stay the test's.
 */
void
c_start(long *sp)
{
	__libc_init_array();
#ifdef QEMU_SUPPORT_EXIT_DIRECT
	_exit(main((int)sp[0], (char **)(sp + 1)));
#else
	exit(main((int)sp[0], (char **)(sp + 1)));
#endif
}

__attribute__((naked)) void
_start(void)
{
	__asm__ volatile("mov r0, sp\n\tbl c_start\n");
}
