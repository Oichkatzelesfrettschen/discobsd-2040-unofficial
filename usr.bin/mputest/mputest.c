/*
 * mputest: the deliberate fault test for the RP2040 port's MPU map.
 *
 * sys/arch/rp2040/rp2040/mpu.c admits unprivileged code to the boot ROM, the
 * process window and the SIO hardware divider, and nothing else, and reports
 * what the registers read back through machdep.mpu. This program takes that
 * report as the claim under test and checks it from user mode:
 *
 *   1. a read of kernel RAM (the first word above the window, machparam.h
 *      USER_DATA_END), of kernel text in XIP flash (the kernel's vector
 *      table at 0x10000100) and of SIO CPUID (0xd0000000) each kill a child
 *      with SIGSEGV when machdep.mpu.enable is 1, and each succeed when it
 *      is 0;
 *   2. a read of the boot ROM (the magic word at 0x00000010, datasheet
 *      2.8.1), of the top of the process window, and of DIV_CSR (0xd0000078)
 *      succeed either way, and a float and a double division through the
 *      boot ROM return the right bits;
 *   3. machdep.mpu.enable, nregions and programmed agree with each other:
 *      enable is 1 only when programmed equals the map's four regions and
 *      nregions is the datasheet's eight.
 *
 *   4. the kernel takes the boot ROM, memory a process may read but not
 *      write, as the source of a transfer and refuses it as a target:
 *      write and writev from ROM bytes deliver them through a pipe,
 *      access on a NUL-terminated ROM string reaches namei and answers
 *      ENOENT, and a read into the ROM, a write source crossing the ROM's
 *      end and a write source in kernel text each fail with EFAULT
 *      (baduaddr_rspan, machdep.c). These hold whether or not the MPU is
 *      on, because the kernel admits from the static map.
 *
 *   5. a process that catches SIGSEGV gets its handler for a fault: a read
 *      of kernel RAM with a handler installed runs the handler with the
 *      faulting pc and the fault-time r4-r11 in its sigcontext, three
 *      times in a row, and the
 *      handler leaves through siglongjmp. This runs only when the MPU is
 *      on, since nothing faults otherwise; a kernel that signals a fault
 *      through the wrong frame replays the faulting load forever at
 *      HardFault priority, which stops the whole board and fails the
 *      Renode gate by its timeout.
 *
 *   6. the two ranges the map closes only by leaving them uncovered. The
 *      unstriped SRAM windows at 0x21000000-0x2103ffff (datasheet 2.6.2)
 *      name the user window and kernel RAM one bank at a time, so a user
 *      read there faults when the MPU is on and succeeds when it is off,
 *      which makes the probe a discriminator of the MPU. The cache-as-SRAM
 *      window at 0x15000000 (datasheet 2.6.3.1) answers every access with a
 *      bus error while the XIP cache is enabled, which is the reset state
 *      the kernel keeps, so a read there dies of SIGSEGV in both MPU
 *      states: the MPU stops it first when on, the bus error when off.
 *      That probe shows the range is closed to user mode; it does not by
 *      itself show the MPU.
 *
 *   7. "mputest guard" checks the break guard, region 7, from user mode;
 *      guard_probe below lists its probes. "mputest residue" checks that
 *      an exec leaves nothing of the previous image in the window.
 *
 * CPUID and DIV_CSR are 0x78 bytes apart inside one 256-byte region, so
 * checking both decides the subregion disables rather than the region.
 *
 * Each probe runs in a child so a kill leaves the parent to report it.
 * Prints one line per probe and "MPUTEST OK" when every line agrees with
 * the kernel's claim; exit status is the number of disagreements. A kernel
 * whose MPU is off passes with every kernel address readable, which is the
 * calibration: the same program on the same image distinguishes a map that
 * the registers accepted from one they dropped.
 */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <machine/cpu.h>
#include <machine/machparam.h>
#include <machine/mpuvar.h>
#include <setjmp.h>
#include <sys/uio.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	ROM_MAGIC_ADDR		0x00000010UL	/* "Mu", 0x01: datasheet 2.8.1. */
#define	KERNEL_TEXT_ADDR	0x10000100UL	/* Kernel vector table in XIP. */
#define	SIO_CPUID_ADDR		0xd0000000UL	/* SIO CPUID, datasheet 2.3.1.7. */
#define	SIO_DIV_CSR_ADDR	0xd0000078UL	/* DIV_CSR, datasheet 2.3.1.5. */
#define	ROM_END_ADDR		0x00004000UL	/* First byte past the ROM. */
#define	XIP_SRAM_ADDR		0x15000000UL	/* XIP cache as SRAM, 2.6.3.1. */
#define	SRAM_ALIAS_BASE		0x21000000UL	/* Unstriped SRAM0-3, 2.6.2. */
#define	SRAM_STRIPED_BASE	0x20000000UL	/* Striped SRAM0-3. */
#define	SRAM_BANK_SIZE		0x10000UL	/* One 64 KB bank. */
/*
 * Word w of bank b at its alias and at its striped twin: the alias lays the
 * banks end to end, the striped map interleaves them word by word.
 */
#define	SRAM_ALIAS(b, w)	(SRAM_ALIAS_BASE + SRAM_BANK_SIZE * (b) + 4UL * (w))
#define	SRAM_STRIPED(b, w)	(SRAM_STRIPED_BASE + 16UL * (w) + 4UL * (b))
/* Word 0x3000 of a bank is where the striped map crosses USER_DATA_END. */
#define	SRAM_KERNEL_WORD	(USER_DATA_SIZE / 16)

/* The alias probes name the striped twins their labels claim. */
_Static_assert(SRAM_STRIPED(0, 0) == USER_DATA_START &&
    SRAM_STRIPED(3, 0) == USER_DATA_START + 12, "alias window twin");
_Static_assert(SRAM_STRIPED(0, SRAM_KERNEL_WORD) == USER_DATA_END &&
    SRAM_STRIPED(3, SRAM_KERNEL_WORD) == USER_DATA_END + 12,
    "alias kernel twin");

#define	MAP_REGIONS		4		/* ROM, window low, high, divider. */
#define	DATASHEET_REGIONS	8		/* MPU_TYPE.DREGION reset value. */

static int
mpu_sysctl_int(int leaf)
{
	int mib[3], v;
	size_t len = sizeof v;

	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_MPU;
	mib[2] = leaf;
	if (sysctl(mib, 3, &v, &len, NULL, 0) < 0) {
		perror("sysctl machdep.mpu");
		exit(2);
	}
	return v;
}

/*
 * Read one word at addr in a child. Returns 0 when the child exited
 * normally, the signal number when it was killed, -1 on any other outcome.
 */
static int
probe(unsigned long addr)
{
	volatile unsigned *p = (volatile unsigned *)addr;
	int status;
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0) {
		(void)*p;
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid)
		return -1;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	if (WIFSIGNALED(status))
		return WTERMSIG(status);
	return -1;
}

static int
report(const char *what, unsigned long addr, int want, int got)
{
	printf("%-24s 0x%08lx %s (want %d, got %d)\n", what, addr,
	    want == got ? "ok" : "FAIL", want, got);
	return want == got ? 0 : 1;
}

/*
 * The boot ROM's fdiv_n builds 0xd0000000 in a register and writes the
 * dividend at offset 0x60 before reading the quotient, so a float or double
 * division from user mode is the end-to-end test of the divider subregion.
 * The operands are volatile so the division happens here rather than in the
 * compiler, and the results are compared as bits.
 */
static int
rom_divide(void)
{
	volatile float fa = 7.5f, fb = 2.5f;
	volatile double da = 7.5, db = 2.5;
	float fq = fa / fb;
	double dq = da / db;
	unsigned int fbits;
	unsigned long long dbits;
	int bad = 0;

	memcpy(&fbits, (void *)&fq, sizeof fbits);
	memcpy(&dbits, (void *)&dq, sizeof dbits);
	if (fbits != 0x40400000UL) {		/* 3.0f */
		printf("rom float divide: got %08x want 40400000\n", fbits);
		bad++;
	}
	if (dbits != 0x4008000000000000ULL) {	/* 3.0 */
		bad++;
		printf("rom double divide: mismatch\n");
	}
	return bad;
}

/*
 * A fault taken with a SIGSEGV handler installed. The handler records the
 * pc the kernel saved in its sigcontext and leaves through siglongjmp; the
 * read sits in caught_read, so the saved pc must lie in that function.
 */
static sigjmp_buf caught_env;
static volatile unsigned long caught_pc;
static volatile int caught_regs_ok;

/* r4 through r11 as caught_read sets them before its faulting load. */
static const unsigned caught_marks[8] = {
	0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb
};

static void
caught_handler(int sig, int code, struct sigcontext *scp)
{
	(void)code;
	caught_pc = scp->sc_pc;
	caught_regs_ok = scp->sc_r4 == caught_marks[0] &&
	    scp->sc_r5 == caught_marks[1] && scp->sc_r6 == caught_marks[2] &&
	    scp->sc_r7 == caught_marks[3] && scp->sc_r8 == caught_marks[4] &&
	    scp->sc_r9 == caught_marks[5] && scp->sc_r10 == caught_marks[6] &&
	    scp->sc_r11 == caught_marks[7];
	siglongjmp(caught_env, sig);
}

/*
 * Load r4-r11 with caught_marks, then read addr. The kernel's sigcontext
 * must carry those values, which only the fault-time register bank holds.
 */
static __attribute__((noinline)) unsigned
caught_read(unsigned long addr)
{
	unsigned v;

	__asm volatile (
	    "movs	r4, #0x88\n\t"
	    "mov	r8, r4\n\t"
	    "movs	r4, #0x99\n\t"
	    "mov	r9, r4\n\t"
	    "movs	r4, #0xaa\n\t"
	    "mov	r10, r4\n\t"
	    "movs	r4, #0xbb\n\t"
	    "mov	r11, r4\n\t"
	    "movs	r4, #0x44\n\t"
	    "movs	r5, #0x55\n\t"
	    "movs	r6, #0x66\n\t"
	    "movs	r7, #0x77\n\t"
	    "ldr	%0, [%1]\n\t"
	    : "=r" (v)
	    : "r" (addr)
	    : "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "memory");
	return v;
}

static int
caught_fault(void)
{
	struct sigaction sa;
	/* Live across siglongjmp, so kept in memory. */
	volatile unsigned long low = (unsigned long)caught_read;
	volatile int i, bad = 0;
	int sig;

	/* BSD handlers take (sig, code, scp); void (*)(void) is the cast GCC admits. */
	sa.sa_handler = (void (*)(int))(void (*)(void))caught_handler;
	sa.sa_mask = 0;			/* sigemptyset is a comma expression. */
	sa.sa_flags = 0;
	if (sigaction(SIGSEGV, &sa, NULL) == -1) {
		perror("sigaction");
		exit(2);
	}
	low &= ~1UL;			/* The Thumb bit of the function address. */
	for (i = 0; i < 3; i++) {
		caught_pc = 0;
		caught_regs_ok = 0;
		sig = sigsetjmp(caught_env, 1);
		if (sig == 0) {
			(void)caught_read(USER_DATA_END);
			bad++;		/* The read must not complete. */
			continue;
		}
		bad += sig != SIGSEGV || caught_pc < low || caught_pc >= low + 64 ||
		    !caught_regs_ok;
	}
	sa.sa_handler = SIG_DFL;
	(void)sigaction(SIGSEGV, &sa, NULL);
	return bad;
}

/*
 * One transfer's outcome: 0 when it moved want bytes, the errno when it
 * failed, -1 when it moved some other count.
 */
static int
xfer_result(ssize_t n, size_t want)
{
	if (n < 0)
		return errno;
	return (size_t)n == want ? 0 : -1;
}

static volatile unsigned long rom_origin = 0;

/* The first NUL-terminated run of three or more printable ROM bytes. */
static const char *
rom_string(void)
{
	const volatile unsigned char *p;
	unsigned long i, run = 0;

	/*
	 * The ROM starts at address 0, which GCC treats as a null object;
	 * loading the base through a volatile hides it from bounds analysis.
	 */
	p = (const volatile unsigned char *)rom_origin;

	for (i = 0; i < ROM_END_ADDR; i++) {
		if (p[i] == '\0') {
			if (run >= 3)
				return (const char *)(i - run);
			run = 0;
		} else if (p[i] >= 0x20 && p[i] < 0x7f)
			run++;
		else
			run = 0;
	}
	return NULL;
}

/*
 * The read-only admission: the boot ROM as a transfer source through a
 * pipe and through namei, and as a target, a crossing source and a kernel
 * source refused.
 */
static int
rom_transfers(void)
{
	const volatile unsigned char *rom;
	unsigned char got[16];
	struct iovec iov;
	const char *path;
	int fds[2], fails = 0, i, same;

	/* GCC treats addresses below 4 KB as null objects; see rom_string. */
	rom = (const volatile unsigned char *)(rom_origin + ROM_MAGIC_ADDR);
	if (pipe(fds) < 0) {
		perror("pipe");
		exit(2);
	}
	fails += report("rom write source", ROM_MAGIC_ADDR, 0,
	    xfer_result(write(fds[1], (void *)ROM_MAGIC_ADDR, sizeof got),
	    sizeof got));
	memset(got, 0, sizeof got);
	same = read(fds[0], got, sizeof got) == (ssize_t)sizeof got;
	for (i = 0; same && i < (int)sizeof got; i++)
		same = got[i] == rom[i];
	fails += report("rom bytes delivered", ROM_MAGIC_ADDR, 1, same);

	iov.iov_base = (void *)ROM_MAGIC_ADDR;
	iov.iov_len = 4;
	fails += report("rom writev source", ROM_MAGIC_ADDR, 0,
	    xfer_result(writev(fds[1], &iov, 1), 4));
	(void)read(fds[0], got, 4);

	fails += report("rom end crossing", ROM_END_ADDR - 8, EFAULT,
	    xfer_result(write(fds[1], (void *)(ROM_END_ADDR - 8), 16), 16));
	fails += report("kernel text source", KERNEL_TEXT_ADDR, EFAULT,
	    xfer_result(write(fds[1], (void *)KERNEL_TEXT_ADDR, 4), 4));
	(void)write(fds[1], "x", 1);
	fails += report("rom read target", ROM_MAGIC_ADDR, EFAULT,
	    xfer_result(read(fds[0], (void *)ROM_MAGIC_ADDR, 1), 1));
	(void)close(fds[0]);
	(void)close(fds[1]);

	path = rom_string();
	if (path == NULL) {
		printf("rom path: no NUL-terminated string in the ROM\n");
		return fails + 1;
	}
	errno = 0;
	fails += report("rom path to namei", (unsigned long)path, ENOENT,
	    access(path, F_OK) == 0 ? 0 : errno);
	return fails;
}

/*
 * The residue probe: a window handed to a new image holds nothing of the
 * image before it.
 *
 * exec_clear() zeroes bss, the heap and the stack region, which leaves the
 * bytes between the top of an image and the bottom of its stack as the one
 * span an exec does not define. The probe fills that span with a marker,
 * execs the program again, and counts the marker words the new image still
 * reads. Both images derive the span from _end and USER_DATA_END, so they
 * agree on it without passing it.
 */
#define	RESIDUE_MARK		0x5ec2e7a5UL	/* Unlikely to occur by chance. */
#define	RESIDUE_STACK_SLACK	(32 * 1024)	/* Below any plausible sp. */
#define	RESIDUE_PATH		"/usr/bin/mputest"

extern char _end[];

/*
 * The gap runs from the first kilobyte boundary above this image to well
 * below the stack: the stack grows down from USER_DATA_END between
 * syscalls with no kernel bookkeeping, so the probe stays clear of it
 * rather than racing it.
 */
static unsigned long *
residue_low(void)
{
	return (unsigned long *)(((unsigned long)_end + 1023UL) & ~1023UL);
}

static unsigned long *
residue_high(void)
{
	return (unsigned long *)((unsigned long)USER_DATA_END -
	    RESIDUE_STACK_SLACK);
}

static int
residue_plant(void)
{
	unsigned long *word, *low = residue_low(), *high = residue_high();
	unsigned long count = 0;

	if (low >= high) {
		printf("residue: no gap between image and stack\n");
		return 2;
	}
	/*
	 * The span lies above the break, where the break guard (mpu.c) makes
	 * the first 32 bytes past the rounded break read-only to this process,
	 * so the break moves to the top of the span before the marker goes in.
	 */
	if ((char *)high > (char *)sbrk(0) &&
	    sbrk((int)((char *)high - (char *)sbrk(0))) == (void *)-1) {
		perror("residue: sbrk");
		return 2;
	}
	for (word = low; word < high; word++) {
		*word = RESIDUE_MARK;
		count++;
	}
	printf("residue: planted %lu words in %p..%p, exec\n", count,
	    (void *)low, (void *)high);
	fflush(stdout);
	execl(RESIDUE_PATH, "mputest", "residue", "check", (char *)NULL);
	perror("execl");
	return 2;
}

static int
residue_check(void)
{
	unsigned long *word, *low = residue_low(), *high = residue_high();
	unsigned long total = 0, found = 0;

	for (word = low; word < high; word++) {
		total++;
		if (*word == RESIDUE_MARK)
			found++;
	}
	printf("residue: %lu of %lu words survived exec\n", found, total);
	if (found == 0) {
		printf("RESIDUE OK\n");
		return 0;
	}
	printf("RESIDUE LEAK\n");
	return 1;
}

/*
 * The break guard probe: mpu.c region 7 (machine/breakguard.h) covers the
 * 32 bytes at the break rounded up to 32, read-only to user code, while the
 * stack stays BREAK_GUARD_MARGIN bytes clear of it. The probe predicts the
 * registers from sbrk(0) with its own copy of the encoding, then checks
 * each behavior the guard claims in a child, so a kill leaves the parent
 * to report it:
 *
 *   readback   machdep.mpu.guardbase and guardattr equal the prediction;
 *   coverage   a store at the guard base dies of SIGSEGV, a load there
 *              reads, and a store 32 bytes above it, past the guard,
 *              succeeds;
 *   pad        stores between the exact break and the guard base succeed;
 *              after sbrk(64) the old base takes a store and the new base
 *              faults, so the guard moved at the system call's return;
 *   caught     a SIGSEGV handler that grows the break and returns lets the
 *              replayed store complete;
 *   slow       a recursion that makes a system call in every frame reaches
 *              to within 192 bytes of the break, and the guard then reads
 *              off, because p_saddr records the deeper stack;
 *   tick       a stack that descends without a system call, spins for
 *              several clock ticks near the guard and then descends
 *              through it survives, because systick() turned the guard off;
 *   burst      a stack pointer moved into the guard by one instruction
 *              faults on its own exception frame, which the kernel answers
 *              with SIGKILL and the console line "fault: stack frame in
 *              break guard" rather than a lockup.
 *
 * With the MPU off every store succeeds, guardattr reads 0 and the burst is
 * skipped; that run is the calibration, and the last line names the state.
 */
#define	GUARD_BYTES		32UL
#define	GUARD_MARGIN		1024UL
#define	GUARD_RASR_FIXED	0x1200000fUL	/* XN, AP 010, SIZE 7, ENABLE. */
#define	GUARD_ROOM		3072UL		/* Break to stack in a child. */
#define	GUARD_SPIN		400000UL	/* Several 1 ms ticks. */
#define	GUARD_MARK		0x5a

static unsigned long
guard_base(void)
{
	return ((unsigned long)sbrk(0) + GUARD_BYTES - 1) & ~(GUARD_BYTES - 1);
}

static unsigned long
guard_rasr(unsigned long base)
{
	return GUARD_RASR_FIXED |
	    ((0xffUL & ~(1UL << ((base >> 5) & 7UL))) << 8);
}

/*
 * Run fn(arg) in a child, which ends in _exit. Returns 0 when the child
 * exited 0, the signal number when it was killed, 100 plus the exit status
 * otherwise.
 */
static int
guard_child(void (*fn)(unsigned long), unsigned long arg)
{
	int status;
	pid_t pid;

	fflush(stdout);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0) {
		fn(arg);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid)
		return -1;
	if (WIFSIGNALED(status))
		return WTERMSIG(status);
	return WEXITSTATUS(status) == 0 ? 0 : 100 + WEXITSTATUS(status);
}

static void
guard_store(unsigned long addr)
{
	*(volatile unsigned char *)addr = GUARD_MARK;
}

static void
guard_load(unsigned long addr)
{
	(void)*(volatile unsigned char *)addr;
}

/* Move the break to GUARD_ROOM bytes below the caller's stack. */
static void
guard_close_in(void)
{
	volatile char here;
	unsigned long want = ((unsigned long)&here - GUARD_ROOM) & ~7UL;

	if (sbrk((int)(want - (unsigned long)sbrk(0))) == (void *)-1)
		_exit(3);
}

static void
guard_grow(int sig)
{
	(void)sig;
	if (sbrk(64) == (void *)-1)
		_exit(3);
}

static void
guard_caught(unsigned long addr)
{
	(void)signal(SIGSEGV, guard_grow);
	*(volatile unsigned char *)addr = GUARD_MARK;
	if (*(volatile unsigned char *)addr != GUARD_MARK)
		_exit(4);
}

/* Each frame enters the kernel, which records the deeper stack. */
static int
guard_descend_slow(unsigned long lowest)
{
	volatile char frame[48];

	frame[0] = 1;
	(void)getpid();
	if ((unsigned long)frame >= lowest + 64)
		return guard_descend_slow(lowest) + frame[0] - 1;
	return 0;
}

static void
guard_slow(unsigned long unused)
{
	(void)unused;
	guard_close_in();
	(void)guard_descend_slow((unsigned long)sbrk(0) + 128);
	if (mpu_sysctl_int(CPU_MPU_GUARDATTR) != 0)
		_exit(4);
}

/* No system call: only systick() sees this stack before it reaches low. */
static int
guard_descend_quiet(unsigned long spin_at, unsigned long low)
{
	volatile char frame[48];
	volatile unsigned long i;

	frame[0] = 1;
	if (spin_at != 0 && (unsigned long)frame <= spin_at) {
		for (i = 0; i < GUARD_SPIN; i++)
			continue;
		spin_at = 0;
	}
	if ((unsigned long)frame >= low + 64)
		return guard_descend_quiet(spin_at, low) + frame[0] - 1;
	return 0;
}

static void
guard_tick(unsigned long unused)
{
	unsigned long base;

	(void)unused;
	guard_close_in();
	base = guard_base();
	(void)guard_descend_quiet(base + GUARD_BYTES + GUARD_MARGIN / 2,
	    base + 8);
}

static void
guard_burst(unsigned long unused)
{
	unsigned long target;

	(void)unused;
	guard_close_in();
	target = guard_base() + 16;
	__asm volatile (
	    "mov	r1, sp\n\t"
	    "mov	sp, %0\n\t"
	    "push	{r1}\n\t"		/* Into the guard. */
	    "pop	{r1}\n\t"
	    "mov	sp, r1\n\t"
	    : : "l" (target) : "r1", "memory");
	_exit(5);
}

static int
guard_probe(void)
{
	unsigned long brk, base, rbar, rasr, old;
	int enable, fails = 0, segv;

	enable = mpu_sysctl_int(CPU_MPU_ENABLE);
	segv = enable ? SIGSEGV : 0;
	if (((unsigned long)sbrk(0) & (GUARD_BYTES - 1)) == 0 &&
	    sbrk(1) == (void *)-1) {
		perror("sbrk");
		return 2;
	}
	brk = (unsigned long)sbrk(0);
	base = guard_base();
	rbar = (unsigned long)mpu_sysctl_int(CPU_MPU_GUARDBASE);
	rasr = (unsigned long)mpu_sysctl_int(CPU_MPU_GUARDATTR);
	printf("guard: break 0x%08lx, base 0x%08lx, rbar 0x%08lx, rasr 0x%08lx\n",
	    brk, base, rbar, rasr);
	fails += report("guard rbar", base, 1,
	    !enable || rbar == ((base & ~0xffUL) | 7UL));
	fails += report("guard rasr", base, 1,
	    enable ? rasr == guard_rasr(base) : rasr == 0);

	fails += report("guard store", base, segv,
	    guard_child(guard_store, base));
	fails += report("guard load", base, 0, guard_child(guard_load, base));
	fails += report("past guard store", base + GUARD_BYTES, 0,
	    guard_child(guard_store, base + GUARD_BYTES));
	fails += report("pad store", base - 1, 0,
	    guard_child(guard_store, base - 1));

	old = base;
	if (sbrk(64) == (void *)-1) {
		perror("sbrk");
		return fails + 2;
	}
	base = guard_base();
	fails += report("old base after sbrk", old, 0,
	    guard_child(guard_store, old));
	fails += report("old base reads zero", old, 0,
	    *(volatile unsigned char *)old);
	fails += report("new base store", base, segv,
	    guard_child(guard_store, base));

	fails += report("caught store", base, 0,
	    guard_child(guard_caught, base));
	fails += report("slow descent", 0, 0, guard_child(guard_slow, 0));
	fails += report("tick retraction", 0, 0, guard_child(guard_tick, 0));
	if (enable)
		fails += report("burst", 0, SIGKILL,
		    guard_child(guard_burst, 0));

	if (fails == 0)
		printf("GUARD OK (mpu %s)\n", enable ? "on" : "off");
	else
		printf("GUARD FAIL: %d\n", fails);
	return fails;
}

int
main(int argc, char **argv)
{
	int ctrl, enable, nregions, programmed, fails = 0, closed;

	if (argc > 1 && strcmp(argv[1], "residue") == 0)
		return argc > 2 ? residue_check() : residue_plant();
	if (argc > 1 && strcmp(argv[1], "guard") == 0)
		return guard_probe();

	enable = mpu_sysctl_int(CPU_MPU_ENABLE);
	nregions = mpu_sysctl_int(CPU_MPU_NREGIONS);
	programmed = mpu_sysctl_int(CPU_MPU_PROGRAMMED);
	ctrl = mpu_sysctl_int(CPU_MPU_CTRL);
	printf("machdep.mpu: enable %d, nregions %d, programmed %d, ctrl 0x%x\n",
	    enable, nregions, programmed, ctrl);

	fails += report("nregions", 0, DATASHEET_REGIONS, nregions);
	/* RP2040 MPU_CTRL has PRIVDEFENA at bit 2 and ENABLE at bit 0. */
	fails += report("control readback", 0, enable ? 0x5 : 0, ctrl);
	fails += report("enable implies map", 0, enable,
	    enable && programmed == MAP_REGIONS);

	/* What a closed address does to a child under the claimed state. */
	closed = enable ? SIGSEGV : 0;
	fails += report("kernel ram", USER_DATA_END, closed,
	    probe(USER_DATA_END));
	fails += report("kernel text", KERNEL_TEXT_ADDR, closed,
	    probe(KERNEL_TEXT_ADDR));
	fails += report("sio cpuid", SIO_CPUID_ADDR, closed,
	    probe(SIO_CPUID_ADDR));

	/*
	 * The unstriped aliases are covered by no region. Bank 0 word 0 and
	 * bank 3 word 0 are the first words of the user window, which the
	 * process owns through 0x20000000 and may not reach through the alias;
	 * word SRAM_KERNEL_WORD of each bank is the twin of kernel RAM at
	 * USER_DATA_END, whose exposure would hand a process the kernel's data.
	 */
	fails += report("alias bank 0 window", SRAM_ALIAS(0, 0), closed,
	    probe(SRAM_ALIAS(0, 0)));
	fails += report("alias bank 3 window", SRAM_ALIAS(3, 0), closed,
	    probe(SRAM_ALIAS(3, 0)));
	fails += report("alias bank 0 kernel", SRAM_ALIAS(0, SRAM_KERNEL_WORD),
	    closed, probe(SRAM_ALIAS(0, SRAM_KERNEL_WORD)));
	fails += report("alias bank 3 kernel", SRAM_ALIAS(3, SRAM_KERNEL_WORD),
	    closed, probe(SRAM_ALIAS(3, SRAM_KERNEL_WORD)));

	/*
	 * Cache-as-SRAM bus-errors while the XIP cache is enabled, so the read
	 * dies in both MPU states: the expectation does not depend on enable.
	 * With the MPU on the region check faults first and the bus never sees
	 * the access; with it off the bus error is the HardFault.
	 */
	fails += report("xip cache sram", XIP_SRAM_ADDR, SIGSEGV,
	    probe(XIP_SRAM_ADDR));

	/* What stays open in both states. */
	fails += report("boot rom", ROM_MAGIC_ADDR, 0, probe(ROM_MAGIC_ADDR));
	fails += report("window top", USER_DATA_END - 4, 0,
	    probe(USER_DATA_END - 4));

	/*
	 * The divider sits 0x78 bytes above the CPUID that must fault, in a
	 * subregion of the same 256-byte region, so this pair decides the
	 * subregion granularity rather than the region's presence. The
	 * division that follows is what the boot ROM actually does with it.
	 */
	fails += report("sio divider", SIO_DIV_CSR_ADDR, 0,
	    probe(SIO_DIV_CSR_ADDR));
	fails += report("rom float divide", 0, 0, rom_divide());
	fails += rom_transfers();
	if (enable)
		fails += report("caught fault", USER_DATA_END, 0, caught_fault());

	if (fails == 0)
		printf("MPUTEST OK (mpu %s)\n", enable ? "on" : "off");
	else
		printf("MPUTEST FAIL: %d\n", fails);
	return fails;
}
