/*
 * Linux EABI syscall layer, used only to execute code that smlrc generated
 * under qemu-arm on the build host.
 *
 * DiscoBSD reaches the kernel with "svc #SYS_x" and reports failure in the
 * carry flag, which qemu-arm, being a Linux user emulator, does not
 * implement. Every entry point the tree's libc would take from its own
 * sys objects is defined here instead; because libc.a is an archive, a
 * member is pulled in only for a symbol still undefined at that point, so
 * these definitions displace the DiscoBSD ones and the rest of libc --
 * printf, malloc, the string functions -- links and runs unchanged.
 */

static int
qsys(int n, int a, int b, int c)
{
	register int r7 __asm__("r7") = n;
	register int r0 __asm__("r0") = a;
	register int r1 __asm__("r1") = b;
	register int r2 __asm__("r2") = c;

	__asm__ __volatile__("svc 0"
	    : "+r"(r0)
	    : "r"(r7), "r"(r1), "r"(r2)
	    : "memory");
	return r0;
}

#define SYS_exit	1
#define SYS_read	3
#define SYS_write	4
#define SYS_open	5
#define SYS_close	6
#define SYS_lseek	19
#define SYS_getpid	20
#define SYS_kill	37
#define SYS_brk		45
#define SYS_ioctl	54

int	write(int, const void *, int);
int	read(int, void *, int);
void	_exit(int);
int	close(int);
int	open(const char *, int, int);
long	lseek(int, long, int);
int	getpid(void);
int	kill(int, int);
int	isatty(int);
int	fstat(int, void *);
int	ioctl(int, unsigned long, void *);
int	gettimeofday(void *, void *);
char	*sbrk(int);
int	brk(void *);
int	_brk(void *);

char **environ;
const char *__progname = "";

int
write(int fd, const void *buf, int n)
{
	return qsys(SYS_write, fd, (int)buf, n);
}

int
read(int fd, void *buf, int n)
{
	return qsys(SYS_read, fd, (int)buf, n);
}

void
_exit(int code)
{
	qsys(SYS_exit, code, 0, 0);
	for (;;)
		;
}

int
close(int fd)
{
	return qsys(SYS_close, fd, 0, 0);
}

int
open(const char *path, int flags, int mode)
{
	return qsys(SYS_open, (int)path, flags, mode);
}

long
lseek(int fd, long off, int whence)
{
	return qsys(SYS_lseek, fd, (int)off, whence);
}

int
getpid(void)
{
	return qsys(SYS_getpid, 0, 0, 0);
}

int
kill(int pid, int sig)
{
	return qsys(SYS_kill, pid, sig, 0);
}

/* A pipe under qemu is not a terminal, and no test inspects the result. */
int
isatty(int fd)
{
	(void)fd;
	return 0;
}

int
fstat(int fd, void *st)
{
	(void)fd;
	(void)st;
	return -1;
}

int
ioctl(int fd, unsigned long req, void *arg)
{
	(void)fd;
	(void)req;
	(void)arg;
	return -1;
}

int
gettimeofday(void *tv, void *tz)
{
	(void)tv;
	(void)tz;
	return -1;
}

/*
 * Linux brk returns the resulting break rather than a status, so the
 * current break is tracked here and sbrk is built on it the way libc
 * expects: it returns the previous break.
 */
static char *qbrk;

int
_brk(void *addr)
{
	char *got = (char *)qsys(SYS_brk, (int)addr, 0, 0);

	if (got < (char *)addr)
		return -1;
	qbrk = got;
	return 0;
}

int
brk(void *addr)
{
	return _brk(addr);
}

char *
sbrk(int incr)
{
	char *old;

	if (qbrk == 0)
		qbrk = (char *)qsys(SYS_brk, 0, 0, 0);
	old = qbrk;
	if (incr == 0)
		return old;
	if (_brk(old + incr) < 0)
		return (char *)-1;
	return old;
}

extern int main(int, char **, char **);
extern void exit(int);

/*
 * The kernel hands _start a stack holding argc, then argv, then envp, so
 * the entry point only has to find them and reproduce what the tree's crt0
 * establishes before main: environ and __progname.
 */
void
__qstart(int *sp)
{
	int argc = sp[0];
	char **argv = (char **)(sp + 1);
	char **env = argv + argc + 1;

	environ = env;
	if (argc > 0 && argv[0] != 0) {
		const char *s;

		__progname = argv[0];
		for (s = __progname; *s != '\0'; s++)
			if (*s == '/')
				__progname = s + 1;
	}
	/* exit rather than _exit, so that libc flushes stdout the way the
	   tree's crt0 arranges. */
	exit(main(argc, argv, env));
}

__asm__(
"	.syntax	unified\n"
"	.text\n"
"	.thumb\n"
"	.thumb_func\n"
"	.globl	_start\n"
"_start:\n"
"	mov	r0, sp\n"
"	ldr	r1, =__qstart\n"
"	blx	r1\n"
"	.ltorg\n"
);
