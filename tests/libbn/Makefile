# Host and cross gate for lib/libbn.
#
#   check        host build and run against the generated vectors: native,
#                -m32 (ILP32) where the host runs it, and AddressSanitizer
#                plus UndefinedBehaviorSanitizer where available; then the
#                mutants, each of which must be rejected
#   check-cross  strict-C17 Cortex-M0+ object, symbol allow-list with a
#                known-bad negative control, and the size line
#   check-qemu   the same test linked with newlib as a Thumb-1 (ARMv6-M)
#                program and run under qemu-arm user-mode emulation; a
#                skipped run reports that no ARM execution occurred
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

.if empty(PYTHON)
.error PYTHON must name the intended interpreter
.endif

HOSTCC?=	cc
# tools/test-execution.mk probes the ILP32 runtime with ILP32_CC and supplies
# TEST_EXECUTION, which records a receipt for every executed or skipped
# variant (tools/test-execution-inventory.json).
ILP32_CC=	${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk
LIBDIR=		${TOPSRC}/lib/libbn
SOURCE=		${LIBDIR}/bn.c
HEADER=		${LIBDIR}/bn.h
BASE_FLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror \
		-Wold-style-definition -Wshadow -Wconversion -O1 -I${LIBDIR}
SAN_FLAGS=	-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer
TARGET_C17_FLAGS= -std=c17 -pedantic-errors -Wall -Wextra -Werror \
		-Wold-style-definition -Os -ffreestanding -mcpu=cortex-m0plus \
		-mthumb -mfloat-abi=soft
QEMU_ARM!=	command -v qemu-arm 2>/dev/null || true
SAN_OK!=	sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} ${SAN_FLAGS}

all: check

vectors.txt: gen_vectors.py
	${PYTHON} -I gen_vectors.py $@

bn_test: bn_test.c ${SOURCE} ${HEADER}
	${HOSTCC} ${BASE_FLAGS} -o $@ bn_test.c

bn_test32: bn_test.c ${SOURCE} ${HEADER}
	${HOSTCC} -m32 ${BASE_FLAGS} -o $@ bn_test.c

bn_test_san: bn_test.c ${SOURCE} ${HEADER}
	${HOSTCC} ${BASE_FLAGS} ${SAN_FLAGS} -o $@ bn_test.c

check: check-native check-ilp32 check-sanitized check-mutants

check-native: bn_test vectors.txt
	${TEST_EXECUTION} run libbn.native -- ./bn_test vectors.txt

check-ilp32: vectors.txt
.if ${ILP32_OK} == "yes"
	@${MAKE} bn_test32
	${TEST_EXECUTION} run libbn.ilp32 -- ./bn_test32 vectors.txt
.else
	${TEST_EXECUTION} skip libbn.ilp32 "libbn: ILP32 runtime unavailable"
.endif

# detect_leaks is off because the test counts live allocations itself and
# LeakSanitizer needs ptrace, which some sandboxes withhold.
check-sanitized: vectors.txt
.if ${SAN_OK} == "yes"
	@${MAKE} bn_test_san
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run libbn.sanitized -- ./bn_test_san vectors.txt
.else
	${TEST_EXECUTION} skip libbn.sanitized "libbn: sanitizer runtime unavailable"
.endif

mutants.stamp: bn_mutate.py ${SOURCE}
	rm -f bn_*.mutant.c bn_*.mutant.test bn_*.mutant.log
	${PYTHON} -I bn_mutate.py ${SOURCE} .
	touch $@

check-mutants: mutants.stamp vectors.txt bn_test.c check_mutants.sh
	HOSTCC=${HOSTCC} ${TEST_EXECUTION} run libbn.negative -- sh ./check_mutants.sh vectors.txt ${LIBDIR}

bn_target.o: ${SOURCE} ${HEADER}
	${GCCPREFIX}-gcc ${TARGET_C17_FLAGS} -nostdinc -I${TOPSRC}/include \
	    -I${LIBDIR} -c ${SOURCE} -o $@

bad_lmul.o: bad_lmul.c.in
	${GCCPREFIX}-gcc ${TARGET_C17_FLAGS} -nostdinc -I${TOPSRC}/include \
	    -x c -c bad_lmul.c.in -o $@

check-cross: bn_target.o bad_lmul.o check_object.sh
	@if sh ./check_object.sh ${GCCPREFIX}-nm ${GCCPREFIX}-size bad_lmul.o \
	    >/dev/null 2>&1; then \
		echo "libbn: symbol check accepted a 64-bit helper" >&2; exit 1; \
	fi
	@echo "libbn: symbol check rejects the 64-bit negative control"
	sh ./check_object.sh ${GCCPREFIX}-nm ${GCCPREFIX}-size bn_target.o

bn_test_arm: bn_test.c qemu_support.c ${SOURCE} ${HEADER}
	${GCCPREFIX}-gcc -std=gnu17 -Wall -Wextra -Werror -Os \
	    -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -I${LIBDIR} \
	    -nostartfiles -Wl,-Ttext=0x20000 -o $@ bn_test.c qemu_support.c \
	    -lc -lgcc

check-qemu: vectors.txt
.if empty(QEMU_ARM)
	@echo "SKIP libbn qemu: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} bn_test_arm
	${QEMU_ARM} ./bn_test_arm vectors.txt
.endif

clean:
	rm -f bn_test bn_test32 bn_test_san bn_test_arm bn_target.o bad_lmul.o vectors.txt \
	    bn_*.mutant.c bn_*.mutant.test bn_*.mutant.log mutants.stamp

.PHONY: all check check-native check-ilp32 check-sanitized check-mutants \
    check-cross check-qemu clean
