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

int
main(int argc, char **argv)
{
	int enable, nregions, programmed, fails = 0, closed;

	if (argc > 1 && strcmp(argv[1], "residue") == 0)
		return argc > 2 ? residue_check() : residue_plant();

	enable = mpu_sysctl_int(CPU_MPU_ENABLE);
	nregions = mpu_sysctl_int(CPU_MPU_NREGIONS);
	programmed = mpu_sysctl_int(CPU_MPU_PROGRAMMED);
	printf("machdep.mpu: enable %d, nregions %d, programmed %d, ctrl 0x%x\n",
	    enable, nregions, programmed, mpu_sysctl_int(CPU_MPU_CTRL));

	fails += report("nregions", 0, DATASHEET_REGIONS, nregions);
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

	if (fails == 0)
		printf("MPUTEST OK (mpu %s)\n", enable ? "on" : "off");
	else
		printf("MPUTEST FAIL: %d\n", fails);
	return fails;
}
