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
DIV_DEFS?=	-DDIVIDER_MODEL
TEST_DEFS?=
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
	${CC_ARM} -x assembler-with-cpp ${DIV_DEFS} ${ARM_FLAGS} \
	    -Wa,--fatal-warnings -c -o $@ ${DIV_SOURCE}

${PROG}_ref.o: reference_div.S
	${CC_ARM} -x assembler-with-cpp ${ARM_FLAGS} -Wa,--fatal-warnings \
	    -c -o $@ reference_div.S

${PROG}_harness.o: harness.S
	${CC_ARM} -x assembler-with-cpp ${ARM_FLAGS} -Wa,--fatal-warnings \
	    -c -o $@ harness.S

${PROG}_main.o: divider_test.c
	${CC_ARM} ${CFLAGS_TEST} ${TEST_DEFS} -c -o $@ divider_test.c

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

# The library selects its division helpers per MACHINE (lib/libc/arm/gen/
# Makefile, DIV_SRC). STM32 is a Cortex-M4, whose Smaller C emits the same
# AEABI calls as the RP2040, so its helper must run without the SIO block:
# the machine's selected source runs here as a Cortex-M4 program with no
# divider model, against the same oracles, and qemu-user faults on any
# access to the SIO base. check-m4-mutants runs the RP2040 helper under that
# model (the defect of building it for every MACHINE) and requires a fault.
M4_FLAGS=	-mcpu=cortex-m4 -mthumb -mfloat-abi=soft
M4_QEMU_CPU?=	cortex-m4
GEN_DIR=	${TOPSRC}/lib/libc/arm/gen
M4_DIV_SRC!=	${MAKE} -s -C ${GEN_DIR} -V DIV_SRC MACHINE=stm32
RP2040_DIV_SRC!=${MAKE} -s -C ${GEN_DIR} -V DIV_SRC MACHINE=rp2040
M4_MAKE=	${MAKE} ARM_FLAGS="${M4_FLAGS}" DIV_DEFS= \
		    TEST_DEFS=-DDIVIDER_NO_MODEL

check-m4:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "divider_helpers: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP divider_helpers m4: qemu-arm unavailable; no ARM execution occurred"
.else
	@if [ x"${RP2040_DIV_SRC}" != x"aeabi_div.S" ]; then \
		echo "check-m4: rp2040 must select aeabi_div.S, got ${RP2040_DIV_SRC}" >&2; exit 1; fi
	@if [ x"${M4_DIV_SRC}" = x"${RP2040_DIV_SRC}" ]; then \
		echo "check-m4: stm32 selects the SIO helper ${M4_DIV_SRC}" >&2; exit 1; fi
	${M4_MAKE} PROG=m4_test DIV_SOURCE=${GEN_DIR}/${M4_DIV_SRC} m4_test
	${QEMU_ARM} -cpu ${M4_QEMU_CPU} ./m4_test
	@${MAKE} check-m4-mutants
.endif

# Mutant 1 builds the SIO helper for the Cortex-M4. Mutant 2 replaces the
# Cortex-M4 helper's remainder multiply-subtract with the quotient.
check-m4-mutants:
	@set -eu; \
	${M4_MAKE} PROG=mutant_m4_sio DIV_SOURCE=${GEN_DIR}/${RP2040_DIV_SRC} \
	    mutant_m4_sio > mutant_m4_sio.build 2>&1; \
	if ${QEMU_ARM} -cpu ${M4_QEMU_CPU} ./mutant_m4_sio > mutant_m4_sio.out 2>&1; then \
		echo "mutant m4_sio: SURVIVED" >&2; exit 1; fi; \
	echo "mutant m4_sio: rejected"; \
	sed 's/mls[[:space:]]*r1, r2, r1, r0/mov r1, r2/' ${GEN_DIR}/${M4_DIV_SRC} \
	    > mutant_m4_rem.S; \
	if cmp -s ${GEN_DIR}/${M4_DIV_SRC} mutant_m4_rem.S; then \
		echo "mutant m4_rem: the defect did not change the source" >&2; exit 2; fi; \
	${M4_MAKE} PROG=mutant_m4_rem DIV_SOURCE=$$(pwd)/mutant_m4_rem.S \
	    mutant_m4_rem > mutant_m4_rem.build 2>&1; \
	if ${QEMU_ARM} -cpu ${M4_QEMU_CPU} ./mutant_m4_rem > mutant_m4_rem.out 2>&1; then \
		echo "mutant m4_rem: SURVIVED" >&2; exit 1; fi; \
	echo "mutant m4_rem: rejected"

check: check-qemu check-m4

clean:
	rm -f divider_test m4_test *.o mutant_*.S mutant_* *.out

.PHONY: all check check-qemu check-mutants check-m4 check-m4-mutants clean
