# Runs the AEABI division helpers of lib/libc/arm/gen/aeabi_div.S under
# qemu-arm as a Cortex-M0 (ARMv6-M) program. qemu-user has no SIO block,
# so the library source is assembled with DIVIDER_MODEL, which swaps the
# base address and the eight-cycle wait for a RAM register block and a
# model of the unsigned divide (harness.S); everything else in the helpers
# runs as shipped. divider_test.c compares every result with the software
# helpers the library used before the hardware divider (reference_div.S)
# and with a restoring division, over every pair of the edge operands and
# a seeded random set. check-mutants rebuilds the helpers with one defect
# at a time and requires the test to reject each.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

QEMU_ARM!=	command -v qemu-arm 2>/dev/null || true
QEMU_CPU?=	cortex-m0
CC_ARM=		${GCCPREFIX}-gcc
ARM_FLAGS=	-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
DIV_SOURCE?=	${TOPSRC}/lib/libc/arm/gen/aeabi_div.S
PROG?=		divider_test
CFLAGS_TEST=	-std=gnu17 -Wall -Wextra -Werror -Os ${ARM_FLAGS} \
		-ffreestanding -fno-builtin
LINK_FLAGS=	-nostdlib -nostartfiles -static -Wl,--build-id=none \
		-Wl,-z,noexecstack -Wl,--fatal-warnings -Wl,-Ttext=0x20000

all: check-qemu

${PROG}: ${PROG}_div.o ${PROG}_ref.o ${PROG}_harness.o ${PROG}_main.o
	${CC_ARM} ${ARM_FLAGS} ${LINK_FLAGS} -o $@ ${PROG}_main.o \
	    ${PROG}_harness.o ${PROG}_div.o ${PROG}_ref.o

${PROG}_div.o: ${DIV_SOURCE}
	${CC_ARM} -x assembler-with-cpp -DDIVIDER_MODEL ${ARM_FLAGS} \
	    -Wa,--fatal-warnings -c -o $@ ${DIV_SOURCE}

${PROG}_ref.o: reference_div.S
	${CC_ARM} -x assembler-with-cpp ${ARM_FLAGS} -Wa,--fatal-warnings \
	    -c -o $@ reference_div.S

${PROG}_harness.o: harness.S
	${CC_ARM} -x assembler-with-cpp ${ARM_FLAGS} -Wa,--fatal-warnings \
	    -c -o $@ harness.S

${PROG}_main.o: divider_test.c
	${CC_ARM} ${CFLAGS_TEST} -c -o $@ divider_test.c

check-qemu:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "divider_helpers: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP divider_helpers: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} ${PROG}
	${QEMU_ARM} -cpu ${QEMU_CPU} ./${PROG}
	@${MAKE} check-mutants
.endif

check-mutants:
	@if [ -z "${QEMU_ARM}" ]; then exit 0; fi; \
	MAKE="${MAKE}" QEMU_ARM="${QEMU_ARM}" QEMU_CPU="${QEMU_CPU}" \
	    sh ./divider_mutants.sh "${DIV_SOURCE}"

check: check-qemu

clean:
	rm -f divider_test *.o mutant_*.S mutant_* *.out

.PHONY: all check check-qemu check-mutants clean
