/*
 * mputest: the deliberate fault test for the RP2040 port's MPU map.
 *
 * sys/arch/rp2040/rp2040/mpu.c admits unprivileged code to the boot ROM and
 * the process window and nothing else, and reports what the registers read
 * back through machdep.mpu. This program takes that report as the claim
 * under test and checks it from user mode:
 *
 *   1. a read of kernel RAM (the first word above the window, machparam.h
 *      USER_DATA_END), of kernel text in XIP flash (the kernel's vector
 *      table at 0x10000100) and of the SIO block (CPUID at 0xd0000000)
 *      each kill a child with SIGSEGV when machdep.mpu.enable is 1, and
 *      each succeed when it is 0;
 *   2. a read of the boot ROM (the magic word at 0x00000010, datasheet
 *      2.8.1) and of the top of the process window succeed either way;
 *   3. machdep.mpu.enable, nregions and programmed agree with each other:
 *      enable is 1 only when programmed equals the map's three regions and
 *      nregions is the datasheet's eight.
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define	ROM_MAGIC_ADDR		0x00000010UL	/* "Mu", 0x01: datasheet 2.8.1. */
#define	KERNEL_TEXT_ADDR	0x10000100UL	/* Kernel vector table in XIP. */
#define	SIO_CPUID_ADDR		0xd0000000UL	/* SIO CPUID, datasheet 2.3.1.7. */
#define	MAP_REGIONS		3		/* ROM, window low, window high. */
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

int
main(void)
{
	int enable, nregions, programmed, fails = 0, closed;

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

	if (fails == 0)
		printf("MPUTEST OK (mpu %s)\n", enable ? "on" : "off");
	else
		printf("MPUTEST FAIL: %d\n", fails);
	return fails;
}
