# Thumb-1 (ARMv6-M) execution resources for the qemu tier, shared by every
# Makefile that links a test as a newlib program and runs it under qemu-arm
# user mode. The including Makefile sets TOPSRC and GCCPREFIX (share/mk/sys.mk)
# and includes this file; it defines variables only, so it may sit anywhere in
# the including Makefile.
#
#   QEMU_ARM              the qemu-arm path, empty when it is absent
#   QEMU_ARM_CPU          the CPU model the program runs on; cortex-m0 is the
#                         ARMv6-M model, which raises SIGILL on a Thumb-2
#                         instruction and SIGBUS on an unaligned access, the
#                         faults a Cortex-M0+ takes, where qemu-arm's default
#                         model accepts both (tests/qemu_cpu_model proves it)
#   QEMU_ARM_RUN          the command prefix that runs one test program
#   QEMU_ARM_CPUFLAGS     the code-generation flags of the target core
#   QEMU_ARM_CC, QEMU_ARM_CFLAGS, QEMU_ARM_LIBS, QEMU_SUPPORT
#                         a newlib link of a test program for Cortex-M0+ with
#                         the Linux EABI system calls of
#                         tests/libbn/qemu_support.c, for qemu-arm user mode
#
# A consumer that finds qemu-arm absent prints a SKIP line naming the missing
# execution, and fails instead when REQUIRE_QEMU is set (root check-qemu).

QEMU_ARM!=	command -v qemu-arm 2>/dev/null || true
QEMU_ARM_CPU?=	cortex-m0
QEMU_ARM_RUN=	${QEMU_ARM} -cpu ${QEMU_ARM_CPU}
QEMU_ARM_CC=	${GCCPREFIX}-gcc
QEMU_ARM_CPUFLAGS= -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
QEMU_ARM_CFLAGS= -std=gnu17 -Wall -Wextra -Werror -Os ${QEMU_ARM_CPUFLAGS} \
		-nostartfiles -Wl,-Ttext=0x20000
QEMU_ARM_LIBS=	-lc -lgcc
QEMU_SUPPORT=	${TOPSRC}/tests/libbn/qemu_support.c
