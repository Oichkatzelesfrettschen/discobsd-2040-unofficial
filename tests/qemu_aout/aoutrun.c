/*
 * aoutrun: load a DiscoBSD RP2040 OMAGIC a.out into the user window at
 * 0x20000000 and run it under qemu-arm user mode, answering its system
 * calls from the Linux kernel underneath.
 *
 * The a.out is the file elf2aout writes: a 32-byte struct exec
 * (sys/sys/exec_aout.h), text, then data, and an entry point linked at
 * USER_DATA_START (lib/elf32-arm.ld). The loader maps USER_DATA_SIZE bytes
 * at that address, copies text and data, leaves bss zero, builds the stack
 * the kernel builds in exec_setupstack (sys/kern/exec_subr.c) and jumps to
 * the entry with argc, argv and envp in r0 to r2.
 *
 * The program reaches the kernel with "svc #N" and reads the failure
 * indication from the carry flag (lib/libc/arm/sys/SYS.h). Under qemu-user
 * a Thumb svc always selects a Linux call by r7, so the loader rewrites each
 * svc site to the permanently undefined "udf #N" encoding (an svc with bit 8
 * of the opcode cleared) and serves the resulting SIGILL, or SIGTRAP for
 * "udf #1", which the Linux ARM ABI reserves as the Thumb breakpoint: it
 * decodes N from the instruction at the faulting pc, runs the call through
 * the Linux kernel, writes r0, sets or clears the carry flag in the saved CPSR and
 * resumes after the instruction. The svc sites are listed in a file of
 * hexadecimal addresses, one per line, produced from the linked ELF by
 * objdump, because a text scan cannot tell an svc from a literal-pool halfword.
 *
 * Floating point reaches the RP2040 boot ROM: lib/libc/arm/gen/rom_float_*.S
 * resolve each routine through the ROM header at address 0x10 and the table
 * lookup helper it names. When the emulator gives the guest address 0 (the
 * caller passes -B or sets QEMU_GUEST_BASE), the loader builds a ROM there
 * with the header, the function and data tables, the lookup helper and one
 * stub per routine the libc resolves, each stub jumping to the libgcc
 * soft-float routine of the same operation. The libc glue then runs as it
 * does on the board; the arithmetic is libgcc's, not the mask ROM's.
 *
 * The caller is a test: fork, exec, signals, pipes and the file system calls
 * whose structures differ between DiscoBSD and Linux are outside the served
 * subset, and a call outside it ends the run with status 70 and its number
 * on stderr.
 *
 * Usage: qemu-arm ./aoutrun SITES AOUT [ARG...]
 */

typedef unsigned int u32;
typedef int i32;
typedef unsigned char u8;
typedef unsigned short u16;
typedef long long i64;

#define USER_DATA_START 0x20000000u
#define USER_DATA_SIZE (144u * 1024u)
#define USER_DATA_END (USER_DATA_START + USER_DATA_SIZE)
#define OMAGIC 0407u
#define MAX_SITES 4096
#define LOADER_EXIT_UNIMPL 70
#define LOADER_EXIT_SETUP 71

struct exec {
	u32 a_midmag, a_text, a_data, a_bss, a_reltext, a_reldata, a_syms,
	    a_entry;
};

/* Linux EABI calls the loader itself makes. */
enum {
	L_EXIT_GROUP = 248, L_READ = 3, L_WRITE = 4, L_OPEN = 5, L_CLOSE = 6,
	L_MMAP2 = 192, L_SIGACTION = 174, L_SIGALTSTACK = 186,
	L_GETPID = 20, L_GETPPID = 64, L_GETUID = 199, L_GETEUID = 201,
	L_GETGID = 200, L_GETEGID = 202, L_GETTIMEOFDAY = 78,
	L_FSTAT64 = 197, L_LSEEK = 19
};

/* DiscoBSD call numbers served (sys/kern/syscalls.master). */
enum {
	B_EXIT = 1, B_READ = 3, B_WRITE = 4, B_OPEN = 5, B_CLOSE = 6,
	B_LSEEK = 19, B_GETPID = 20, B_GETUID = 24, B_GETEUID = 25,
	B_GETPPID = 27, B_GETGID = 47, B_GETEGID = 48, B_IOCTL = 54,
	B_FSTAT = 62, B_BRK = 69, B_GETTIMEOFDAY = 116
};

#define E_IO 5
#define E_NOTTY 25
#define E_NOMEM 12
#define E_NOSYS 78
#define E_INVAL 22
#define E_AGAIN 35
#define E_OPNOTSUPP 45
#define E_TIMEDOUT 60
#define E_CONNREFUSED 61
#define E_LOOP 62
#define E_NAMETOOLONG 63
#define E_NOTEMPTY 66
#define E_DQUOT 69
#define E_STALE 70
#define E_NOLCK 77

#define PSR_C (1u << 29)

static long
lx(long n, long a, long b, long c, long d, long e, long f)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;
	register long r3 __asm__("r3") = d;
	register long r4 __asm__("r4") = e;
	register long r5 __asm__("r5") = f;

	__asm__ volatile("svc #0"
	    : "+r"(r0)
	    : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
	    : "memory");
	return r0;
}

static void
die(const char *msg, int status)
{
	unsigned n = 0;

	while (msg[n] != '\0')
		n++;
	lx(L_WRITE, 2, (long)msg, (long)n, 0, 0, 0);
	lx(L_EXIT_GROUP, status, 0, 0, 0, 0, 0);
	for (;;)
		;
}

static void
put_dec(int fd, u32 v)
{
	char b[12];
	int i = 12;

	do {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v != 0);
	lx(L_WRITE, fd, (long)(b + i), 12 - i, 0, 0, 0);
}

/* sigcontext and ucontext of the Linux ARM ABI (arch/arm uapi). */
struct lx_sigcontext {
	u32 trap_no, error_code, oldmask;
	u32 r[13];
	u32 sp, lr, pc, cpsr, fault_address;
};

struct lx_ucontext {
	u32 uc_flags;
	u32 uc_link;
	u32 ss_sp, ss_flags, ss_size;
	struct lx_sigcontext mc;
};

struct lx_ksigaction {
	void *handler;
	u32 flags;
	void *restorer;
	u32 mask[2];
};

struct lx_stack {
	void *sp;
	i32 flags;
	u32 size;
};

#define LX_SA_SIGINFO 4u
#define LX_SA_ONSTACK 0x08000000u
#define LX_SA_RESTORER 0x04000000u
#define LX_SA_NODEFER 0x40000000u
#define LX_SIGILL 4
#define LX_SIGTRAP 5

static u32 sites[MAX_SITES];
static unsigned nsites;
static u32 brk_cur;
static u32 brk_start;
static unsigned char altstack[8192] __attribute__((aligned(8)));

/* DiscoBSD struct stat: fourteen words (sys/sys/stat.h). */
struct bsd_stat {
	i32 st_dev;
	u32 st_ino, st_mode;
	i32 st_nlink;
	u32 st_uid, st_gid;
	i32 st_rdev, st_size, st_atime, st_mtime, st_ctime;
	i32 st_blksize, st_blocks;
	u32 st_flags;
};

/* struct stat64 of the Linux ARM EABI. */
struct lx_stat64 {
	i64 st_dev;
	u8 pad0[4];
	u32 st_ino32;
	u32 st_mode, st_nlink, st_uid, st_gid;
	i64 st_rdev;
	u8 pad3[4];
	i64 st_size;
	u32 st_blksize;
	i64 st_blocks;
	u32 st_atime, st_atime_nsec, st_mtime, st_mtime_nsec, st_ctime,
	    st_ctime_nsec;
	i64 st_ino;
};

/*
 * Linux ARM EABI errno values to DiscoBSD's (sys/sys/errno.h). The two
 * numberings agree from EPERM (1) through ERANGE (34) except EAGAIN, which
 * the DiscoBSD libc spells EWOULDBLOCK (35). Linux numbers from 35 up name
 * different conditions in DiscoBSD, so each one the served subset can
 * produce is translated by name, and one without a DiscoBSD counterpart
 * becomes EIO instead of a number that means something else.
 */
static int
errno_to_bsd(int e)
{
	if (e >= 1 && e <= 34 && e != 11)
		return e;
	switch (e) {
	case 11:
		return E_AGAIN;
	case 35:	/* EDEADLK */
		return E_AGAIN;
	case 36:	/* ENAMETOOLONG */
		return E_NAMETOOLONG;
	case 37:	/* ENOLCK */
		return E_NOLCK;
	case 38:	/* ENOSYS */
		return E_NOSYS;
	case 39:	/* ENOTEMPTY */
		return E_NOTEMPTY;
	case 40:	/* ELOOP */
		return E_LOOP;
	case 95:	/* EOPNOTSUPP */
		return E_OPNOTSUPP;
	case 110:	/* ETIMEDOUT */
		return E_TIMEDOUT;
	case 111:	/* ECONNREFUSED */
		return E_CONNREFUSED;
	case 116:	/* ESTALE */
		return E_STALE;
	case 122:	/* EDQUOT */
		return E_DQUOT;
	default:
		return E_IO;
	}
}

static int
site_known(u32 pc)
{
	unsigned lo = 0, hi = nsites;

	while (lo < hi) {
		unsigned mid = (lo + hi) / 2;

		if (sites[mid] == pc)
			return 1;
		if (sites[mid] < pc)
			lo = mid + 1;
		else
			hi = mid;
	}
	return 0;
}

static u32
bsd_open_flags(u32 f)
{
	u32 l = f & 3u;

	if (f & 0x0004u)
		l |= 04000u;	/* O_NONBLOCK */
	if (f & 0x0008u)
		l |= 02000u;	/* O_APPEND */
	if (f & 0x0200u)
		l |= 0100u;	/* O_CREAT */
	if (f & 0x0400u)
		l |= 01000u;	/* O_TRUNC */
	if (f & 0x0800u)
		l |= 0200u;	/* O_EXCL */
	return l;
}

/*
 * Serve one DiscoBSD call. The result is returned in *rv; a nonzero return
 * is a BSD errno and sets the carry flag.
 */
static int
serve(u32 code, u32 *r, u32 sp, u32 *rv)
{
	long x;

	switch (code) {
	case B_EXIT:
		lx(L_EXIT_GROUP, (long)r[0], 0, 0, 0, 0, 0);
		return 0;
	case B_READ:
		x = lx(L_READ, (long)r[0], (long)r[1], (long)r[2], 0, 0, 0);
		break;
	case B_WRITE:
		x = lx(L_WRITE, (long)r[0], (long)r[1], (long)r[2], 0, 0, 0);
		break;
	case B_OPEN:
		x = lx(L_OPEN, (long)r[0], (long)bsd_open_flags(r[1]),
		    (long)r[2], 0, 0, 0);
		break;
	case B_CLOSE:
		x = lx(L_CLOSE, (long)r[0], 0, 0, 0, 0, 0);
		break;
	case B_GETPID:
		x = lx(L_GETPID, 0, 0, 0, 0, 0, 0);
		break;
	case B_GETPPID:
		x = lx(L_GETPPID, 0, 0, 0, 0, 0, 0);
		break;
	case B_GETUID:
		x = lx(L_GETUID, 0, 0, 0, 0, 0, 0);
		break;
	case B_GETEUID:
		x = lx(L_GETEUID, 0, 0, 0, 0, 0, 0);
		break;
	case B_GETGID:
		x = lx(L_GETGID, 0, 0, 0, 0, 0, 0);
		break;
	case B_GETEGID:
		x = lx(L_GETEGID, 0, 0, 0, 0, 0, 0);
		break;
	case B_GETTIMEOFDAY:
		x = lx(L_GETTIMEOFDAY, (long)r[0], 0, 0, 0, 0, 0);
		break;
	case B_IOCTL:
		/* isatty() asks TIOCGETP; no descriptor here is a DiscoBSD tty. */
		return E_NOTTY;
	case B_LSEEK:
		/*
		 * lseek(int fd, off_t off, int whence) with off_t a 32-bit long:
		 * the kernel reads {fd, off, sbase} from r0..r2
		 * (sys/kern/ufs_syscalls2.c) and the libc stub moves nothing.
		 */
		x = lx(L_LSEEK, (long)r[0], (long)r[1], (long)r[2], 0, 0, 0);
		break;
	case B_FSTAT: {
		struct lx_stat64 s;
		struct bsd_stat *b = (struct bsd_stat *)r[1];

		x = lx(L_FSTAT64, (long)r[0], (long)&s, 0, 0, 0, 0);
		if (x == 0) {
			b->st_dev = (i32)s.st_dev;
			b->st_ino = (u32)s.st_ino;
			b->st_mode = s.st_mode;
			b->st_nlink = (i32)s.st_nlink;
			b->st_uid = s.st_uid;
			b->st_gid = s.st_gid;
			b->st_rdev = (i32)s.st_rdev;
			b->st_size = (i32)s.st_size;
			b->st_atime = (i32)s.st_atime;
			b->st_mtime = (i32)s.st_mtime;
			b->st_ctime = (i32)s.st_ctime;
			b->st_blksize = (i32)s.st_blksize;
			b->st_blocks = (i32)s.st_blocks;
			b->st_flags = 0;
		}
		break;
	}
	case B_BRK: {
		/*
		 * The kernel keeps the break between the image and the stack
		 * and answers ENOMEM past the stack (sys/kern/kern_mman.c).
		 * The window is already mapped, so the loader tracks the
		 * break and bounds it by the stack pointer.
		 */
		u32 want = r[0];

		/* Subtraction keeps a request near 2^32 from wrapping. */
		if (want < brk_start || want > USER_DATA_END || want >= sp ||
		    sp - want < 1024u)
			return E_NOMEM;
		brk_cur = want;
		*rv = 0;
		return 0;
	}
	default:
		put_dec(2, code);
		die(" <- unserved DiscoBSD system call (aoutrun)\n",
		    LOADER_EXIT_UNIMPL);
		return 0;
	}
	if (x < 0 && x > -4096) {
		return errno_to_bsd((int)-x);
	}
	*rv = (u32)x;
	return 0;
}

static void
on_sigill(int sig, void *info, void *vuc)
{
	struct lx_ucontext *uc = vuc;
	struct lx_sigcontext *mc = &uc->mc;
	u32 pc = mc->pc;
	u32 rv = 0;
	int err;

	(void)sig;
	(void)info;
	if (!site_known(pc))
		die("aoutrun: SIGILL outside the recorded svc sites\n", 132);
	err = serve(*(const u8 *)pc, mc->r, mc->sp, &rv);
	if (err != 0) {
		mc->r[0] = (u32)err;
		mc->cpsr |= PSR_C;
	} else {
		mc->r[0] = rv;
		mc->cpsr &= ~PSR_C;
	}
	mc->pc = pc + 2u;
}

__asm__(
"	.thumb\n"
"	.thumb_func\n"
"restorer:\n"
"	movs	r7, #173\n"	/* rt_sigreturn */
"	svc	#0\n");
extern void restorer(void);

static u32
parse_hex(const char *s, const char **end)
{
	u32 v = 0;

	for (;; s++) {
		char c = *s;

		if (c >= '0' && c <= '9')
			v = v * 16u + (u32)(c - '0');
		else if (c >= 'a' && c <= 'f')
			v = v * 16u + (u32)(c - 'a' + 10);
		else
			break;
	}
	*end = s;
	return v;
}

static char sitebuf[MAX_SITES * 9u + 16u];

static void
load_sites(const char *path)
{
	long fd = lx(L_OPEN, (long)path, 0, 0, 0, 0, 0);
	long n, total = 0;
	const char *p;

	if (fd < 0)
		die("aoutrun: cannot open the svc site list\n", LOADER_EXIT_SETUP);
	for (;;) {
		n = lx(L_READ, fd, (long)(sitebuf + total),
		    (long)(sizeof sitebuf - 1u - (unsigned long)total), 0, 0, 0);
		if (n <= 0)
			break;
		total += n;
	}
	lx(L_CLOSE, fd, 0, 0, 0, 0, 0);
	sitebuf[total] = '\0';
	for (p = sitebuf; *p != '\0';) {
		const char *e;
		u32 v = parse_hex(p, &e);

		if (e != p) {
			if (nsites == MAX_SITES)
				die("aoutrun: too many svc sites\n", LOADER_EXIT_SETUP);
			if (nsites != 0 && v <= sites[nsites - 1u])
				die("aoutrun: svc sites must ascend\n", LOADER_EXIT_SETUP);
			sites[nsites++] = v;
		}
		while (*e != '\0' && *e != '\n')
			e++;
		p = (*e == '\n') ? e + 1 : e;
	}
}

static unsigned
cstrlen(const char *s)
{
	unsigned n = 0;

	while (s[n] != '\0')
		n++;
	return n;
}

static void
copy(void *d, const void *s, unsigned n)
{
	u8 *dd = d;
	const u8 *ss = s;

	while (n--)
		*dd++ = *ss++;
}


/* Boot ROM model (RP2040 datasheet 2.8: header at 0x10, tables by code). */
#define ROM_SIZE 0x4000u
#define ROM_FUNC_TABLE 0x20u
#define ROM_DATA_TABLE 0x30u
#define ROM_SF_TABLE 0x100u
#define ROM_SD_TABLE 0x200u
#define ROM_STUBS 0x300u
#define ROM_CODE(a, b) ((u16)((u16)(a) | ((u16)(b) << 8)))

extern void __aeabi_fadd(void), __aeabi_fsub(void), __aeabi_fmul(void),
    __aeabi_fdiv(void), __aeabi_f2uiz(void), __aeabi_l2f(void),
    __aeabi_ul2f(void), __aeabi_f2ulz(void), __aeabi_f2d(void),
    __aeabi_dadd(void), __aeabi_dsub(void), __aeabi_dmul(void),
    __aeabi_ddiv(void), __aeabi_d2uiz(void), __aeabi_l2d(void),
    __aeabi_ul2d(void), __aeabi_d2ulz(void), __aeabi_d2f(void);

static u32
rom_clz32(u32 x)
{
	u32 n = 0;

	if (x == 0)
		return 32;
	while (!(x & 0x80000000u)) {
		x <<= 1;
		n++;
	}
	return n;
}

/* rom_table_lookup(table, code): a zero-terminated array of {code, ptr}. */
static u32
rom_table_lookup(const u16 *table, u32 code)
{
	for (; table[0] != 0; table += 2)
		if (table[0] == code)
			return table[1];
	return 0;
}

static u32 rom_next_stub = ROM_STUBS;

/*
 * Write a 16-byte stub at the next free ROM slot that preserves r0-r3 and
 * tail-calls target: push {r0}; ldr r0, [pc, #8]; mov ip, r0; pop {r0};
 * bx ip; nop; .word target. Returns the stub address with the Thumb bit.
 */
static u32
rom_stub(u8 *rom, u32 target)
{
	u16 *h = (u16 *)(rom + rom_next_stub);
	u32 addr = rom_next_stub | 1u;

	h[0] = 0xb401;
	h[1] = 0x4802;
	h[2] = 0x4684;
	h[3] = 0xbc01;
	h[4] = 0x4760;
	h[5] = 0x46c0;
	h[6] = (u16)(target & 0xffffu);
	h[7] = (u16)(target >> 16);
	rom_next_stub += 16u;
	return addr;
}

static int
rom_build(void)
{
	static const struct {
		u32 offset;
		void (*single)(void);
		void (*dbl)(void);
	} ops[] = {
		{0x00, __aeabi_fadd, __aeabi_dadd},
		{0x04, __aeabi_fsub, __aeabi_dsub},
		{0x08, __aeabi_fmul, __aeabi_dmul},
		{0x0c, __aeabi_fdiv, __aeabi_ddiv},
		{0x24, __aeabi_f2uiz, __aeabi_d2uiz},
		{0x5c, __aeabi_l2f, __aeabi_l2d},
		{0x64, __aeabi_ul2f, __aeabi_ul2d},
		{0x74, __aeabi_f2ulz, __aeabi_d2ulz},
		{0x7c, __aeabi_f2d, __aeabi_d2f},
	};
	u8 *rom = (u8 *)lx(L_MMAP2, 0, ROM_SIZE, 3, 0x32, -1, 0);
	u16 *func = (u16 *)(rom + ROM_FUNC_TABLE);
	u16 *data = (u16 *)(rom + ROM_DATA_TABLE);
	u32 *sf = (u32 *)(rom + ROM_SF_TABLE);
	u32 *sd = (u32 *)(rom + ROM_SD_TABLE);
	unsigned i;

	if (rom != 0)
		return 0;
	rom[0x10] = 'M';
	rom[0x11] = 'u';
	rom[0x12] = 1;
	rom[0x13] = 3;	/* Boot ROM V3 */
	*(u16 *)(rom + 0x14) = ROM_FUNC_TABLE;
	*(u16 *)(rom + 0x16) = ROM_DATA_TABLE;
	*(u16 *)(rom + 0x18) = (u16)rom_stub(rom, (u32)rom_table_lookup);
	func[0] = ROM_CODE('L', '3');
	func[1] = (u16)rom_stub(rom, (u32)rom_clz32);
	data[0] = ROM_CODE('S', 'F');
	data[1] = ROM_SF_TABLE;
	data[2] = ROM_CODE('S', 'D');
	data[3] = ROM_SD_TABLE;
	for (i = 0; i < sizeof ops / sizeof ops[0]; i++) {
		sf[ops[i].offset / 4u] = rom_stub(rom, (u32)ops[i].single);
		sd[ops[i].offset / 4u] = rom_stub(rom, (u32)ops[i].dbl);
	}
	return lx(125, (long)rom, ROM_SIZE, 5, 0, 0, 0) == 0;	/* r-x */
}

void loader_main(u32 *sp0) __attribute__((used, noreturn));

void
loader_main(u32 *sp0)
{
	int argc = (int)sp0[0];
	char **argv = (char **)(sp0 + 1);
	char **envp = argv + argc + 1;
	struct exec h;
	long fd, n;
	u8 *win;
	u32 total, i, entry, top, spc, need;
	char **uargv, **uenv;
	char *ucp;
	int uargc, envc = 0;
	struct lx_ksigaction sa;
	struct lx_stack ss;

	if (argc < 3)
		die("usage: aoutrun SITES AOUT [ARG...]\n", LOADER_EXIT_SETUP);
	load_sites(argv[1]);

	fd = lx(L_OPEN, (long)argv[2], 0, 0, 0, 0, 0);
	if (fd < 0)
		die("aoutrun: cannot open the a.out\n", LOADER_EXIT_SETUP);
	n = lx(L_READ, fd, (long)&h, (long)sizeof h, 0, 0, 0);
	if (n != (long)sizeof h || (h.a_midmag & 0xffffu) != OMAGIC)
		die("aoutrun: not an OMAGIC a.out\n", LOADER_EXIT_SETUP);
	total = h.a_text + h.a_data;
	if (total < h.a_text || total + h.a_bss < total ||
	    total + h.a_bss > USER_DATA_SIZE - 4096u)
		die("aoutrun: image exceeds the user window\n", LOADER_EXIT_SETUP);

	/* MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, read/write/execute. */
	win = (u8 *)lx(L_MMAP2, (long)USER_DATA_START, (long)USER_DATA_SIZE, 7,
	    0x32, -1, 0);
	if ((u32)win != USER_DATA_START)
		die("aoutrun: cannot map the user window\n", LOADER_EXIT_SETUP);
	for (i = 0; i < total;) {
		n = lx(L_READ, fd, (long)(win + i), (long)(total - i), 0, 0, 0);
		if (n <= 0)
			die("aoutrun: a.out is truncated\n", LOADER_EXIT_SETUP);
		i += (u32)n;
	}
	lx(L_CLOSE, fd, 0, 0, 0, 0, 0);
	/* Without a guest base the emulator refuses address 0; floating point
	 * then faults at the ROM header, and nothing else is affected. */
	(void)rom_build();

	/* svc #N (0xdfNN) becomes udf #N (0xdeNN). */
	for (i = 0; i < nsites; i++) {
		u32 a = sites[i];
		u16 *hw;

		if (a < USER_DATA_START || a + 2u > USER_DATA_START + h.a_text)
			die("aoutrun: svc site outside the text\n", LOADER_EXIT_SETUP);
		hw = (u16 *)a;
		if ((*hw & 0xff00u) != 0xdf00u)
			die("aoutrun: svc site does not hold an svc\n", LOADER_EXIT_SETUP);
		*hw = (u16)(*hw & ~0x0100u);
	}

	brk_start = USER_DATA_START + total + h.a_bss;
	brk_start = (brk_start + 3u) & ~3u;
	brk_cur = brk_start;

	/* Stack as exec_setupstack builds it. */
	while (envp[envc] != 0)
		envc++;
	uargc = argc - 2;
	top = USER_DATA_END - 4u;
	spc = 0;
	for (i = 0; i < (u32)uargc; i++) {
		spc += cstrlen(argv[2 + i]) + 1u;
		if (spc > USER_DATA_SIZE)
			die("aoutrun: arguments exceed the user window\n",
			    LOADER_EXIT_SETUP);
	}
	for (i = 0; i < (u32)envc; i++) {
		spc += cstrlen(envp[i]) + 1u;
		if (spc > USER_DATA_SIZE)
			die("aoutrun: environment exceeds the user window\n",
			    LOADER_EXIT_SETUP);
	}
	/*
	 * Strings, the two pointer vectors, the 8-byte alignment word and the
	 * 64-byte entry frame must all lie above the break. spc is bounded by
	 * the window and each string has at least one byte, so the sum cannot
	 * wrap.
	 */
	need = ((spc + 3u) & ~3u) + (u32)(envc + 1) * 4u +
	    (u32)(uargc + 1) * 4u + 4u + 64u;
	if (need > top - brk_start)
		die("aoutrun: arguments and environment overlap the image\n",
		    LOADER_EXIT_SETUP);
	ucp = (char *)(top - ((spc + 3u) & ~3u));
	uenv = (char **)((u32)ucp - (u32)(envc + 1) * 4u);
	uargv = (char **)((u32)uenv - (u32)(uargc + 1) * 4u);
	if ((u32)uargv & 7u) {
		ucp -= 4;
		uenv--;
		uargv--;
	}
	{
		char *cp = ucp;

		for (i = 0; i < (u32)uargc; i++) {
			unsigned l = cstrlen(argv[2 + i]) + 1u;

			copy(cp, argv[2 + i], l);
			uargv[i] = cp;
			cp += l;
		}
		uargv[uargc] = 0;
		for (i = 0; i < (u32)envc; i++) {
			unsigned l = cstrlen(envp[i]) + 1u;

			copy(cp, envp[i], l);
			uenv[i] = cp;
			cp += l;
		}
		uenv[envc] = 0;
	}
	*(char ***)top = uargv;

	/* Serve SIGILL on a stack of the loader's own. */
	ss.sp = altstack;
	ss.flags = 0;
	ss.size = sizeof altstack;
	if (lx(L_SIGALTSTACK, (long)&ss, 0, 0, 0, 0, 0) != 0)
		die("aoutrun: sigaltstack failed\n", LOADER_EXIT_SETUP);
	sa.handler = (void *)on_sigill;
	sa.flags = LX_SA_SIGINFO | LX_SA_ONSTACK | LX_SA_RESTORER | LX_SA_NODEFER;
	sa.restorer = (void *)restorer;
	sa.mask[0] = sa.mask[1] = 0;
	if (lx(L_SIGACTION, LX_SIGILL, (long)&sa, 0, 8, 0, 0) != 0)
		die("aoutrun: sigaction failed\n", LOADER_EXIT_SETUP);
	/* The kernel's Thumb breakpoint is "udf #1", which is exit's number. */
	if (lx(L_SIGACTION, LX_SIGTRAP, (long)&sa, 0, 8, 0, 0) != 0)
		die("aoutrun: sigaction failed\n", LOADER_EXIT_SETUP);

	entry = h.a_entry;
	{
		register u32 r0 __asm__("r0") = (u32)uargc;
		register u32 r1 __asm__("r1") = (u32)uargv;
		register u32 r2 __asm__("r2") = (u32)uenv;
		register u32 r3 __asm__("r3") = entry;
		u32 usp = (u32)uargv - 64u;

		__asm__ volatile(
		    "mov	sp, %[usp]\n\t"
		    "bx	r3\n\t"
		    :
		    : "r"(r0), "r"(r1), "r"(r2), "r"(r3), [usp] "r"(usp)
		    : "memory");
	}
	for (;;)
		;
}

__asm__(
"	.thumb\n"
"	.syntax unified\n"
"	.globl	_start\n"
"	.thumb_func\n"
"_start:\n"
"	mov	r0, sp\n"
"	bl	loader_main\n");
