# Host and cross gate for lib/libcalc.
#
#   check-null-sweep  every status-returning entry point of mathematics.h has a
#                  NULL_ARG entry in the NULL-pointer sweep of calc_test.c; a
#                  copy of the test without the calc_stddev entry must fail
#   check          host build and run against the specification vectors:
#                  native, -m32 (ILP32) where the host runs it, AddressSanitizer
#                  plus UndefinedBehaviorSanitizer where available, and the
#                  tree libm (lib/libm sources compiled for the host and linked
#                  ahead of the host libm); then the mutants, each of which
#                  must be rejected
#   check-cross    the library built with the tree's cross compiler and flags
#                  (MACHINE=rp2040), an undefined-symbol allow-list with a
#                  known-bad negative control, and text/data/bss per object
#   check-qemu     the same test linked with newlib as a Thumb-1 (ARMv6-M)
#                  program and run under qemu-arm user-mode emulation with
#                  libgcc's IEEE soft-float; a skipped run reports that no ARM
#                  execution occurred
#   vectors        regenerate complex_vectors.h from the specification and,
#                  for the pole-approach rows, from mpmath
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
LIBDIR=		${TOPSRC}/lib/libcalc
LIBSRCS!=	ls ${LIBDIR}/calc_*.c
HEADERS=	${LIBDIR}/mathematics.h ${LIBDIR}/calc_internal.h
SPEC=		${TOPSRC}/docs/research/picomite-maths-clean-room-spec.md
BASE_FLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror -Wshadow \
		-Wconversion -Wold-style-definition -O1 -I${LIBDIR}
# x87 excess precision would change the rounding of an ILP32 host build, so
# the 32-bit run uses SSE2 arithmetic as the double-precision reference.
ILP32_FLAGS=	-m32 -msse2 -mfpmath=sse

all: check

# tools/host-test.mk supplies the sanitizer probe, the tree libm objects and
# the qemu-arm newlib link shared with tests/libbn and usr.bin/calc.
include ${TOPSRC}/tools/host-test.mk

# complex_vectors.h is committed; it changes only when the specification or the
# pole-approach rows of gen_complex_vectors.py do. The pole rows need mpmath.
vectors:
	${PYTHON} -I gen_complex_vectors.py ${SPEC} complex_vectors.h

calc_test: calc_test.c complex_vectors.h ${LIBSRCS} ${HEADERS}
	${HOSTCC} ${BASE_FLAGS} -o $@ calc_test.c ${LIBSRCS} -lm

calc_test32: calc_test.c complex_vectors.h ${LIBSRCS} ${HEADERS}
	${HOSTCC} ${ILP32_FLAGS} ${BASE_FLAGS} -o $@ calc_test.c ${LIBSRCS} -lm

calc_test_san: calc_test.c complex_vectors.h ${LIBSRCS} ${HEADERS}
	${HOSTCC} ${BASE_FLAGS} ${SAN_FLAGS} -o $@ calc_test.c ${LIBSRCS} -lm

calc_test_treelibm: calc_test.c complex_vectors.h ${LIBSRCS} ${HEADERS} ${TREELIBM_OBJS}
	${HOSTCC} ${BASE_FLAGS} -fno-builtin -o $@ calc_test.c ${LIBSRCS} \
	    ${TREELIBM_OBJS} -lm

check: check-native check-ilp32 check-sanitized check-treelibm check-mutants check-null-sweep

check-native: calc_test
	${TEST_EXECUTION} run libcalc.native -- ./calc_test

check-ilp32:
.if ${ILP32_OK} == "yes"
	@${MAKE} calc_test32
	${TEST_EXECUTION} run libcalc.ilp32 -- ./calc_test32
.else
	${TEST_EXECUTION} skip libcalc.ilp32 "libcalc: ILP32 runtime unavailable"
.endif

# detect_leaks is off because LeakSanitizer needs ptrace, which some
# sandboxes withhold; libcalc allocates nothing.
check-sanitized:
.if ${SAN_OK} == "yes"
	@${MAKE} calc_test_san
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run libcalc.sanitized -- ./calc_test_san
.else
	${TEST_EXECUTION} skip libcalc.sanitized "libcalc: sanitizer runtime unavailable"
.endif

check-treelibm:
.if ${OBJCOPY_OK} == "yes"
	@${MAKE} calc_test_treelibm
	${TEST_EXECUTION} run libcalc.treelibm -- ./calc_test_treelibm
.else
	${TEST_EXECUTION} skip libcalc.treelibm "libcalc: objcopy unavailable"
.endif

# The calibration input is the test without one sweep entry: the script must
# reject it before its verdict on the real test counts.
check-null-sweep: check_null_sweep.sh calc_test.c ${LIBDIR}/mathematics.h
	@sed '/NULL_ARG(calc_stddev(/d' calc_test.c > null_sweep_negative.c
	@if sh ./check_null_sweep.sh ${LIBDIR}/mathematics.h null_sweep_negative.c \
	    >/dev/null 2>&1; then \
		echo "libcalc: null sweep check accepted a test without a calc_stddev entry" >&2; \
		rm -f null_sweep_negative.c; exit 1; \
	fi
	@rm -f null_sweep_negative.c
	@echo "libcalc: null sweep check rejects the missing-entry negative control"
	sh ./check_null_sweep.sh ${LIBDIR}/mathematics.h calc_test.c

mutants.stamp: calc_mutate.py ${LIBSRCS} ${HEADERS}
	${PYTHON} -I calc_mutate.py ${LIBDIR} mutants
	touch $@

check-mutants: mutants.stamp calc_test.c complex_vectors.h check_mutants.sh
	HOSTCC=${HOSTCC} ${TEST_EXECUTION} run libcalc.negative -- sh ./check_mutants.sh mutants

# The objects come from lib/libcalc/Makefile, so the gate measures what the
# library build ships. bad_malloc.o must be rejected by the same script.
bad_malloc.o: bad_malloc.c.in
	${GCCPREFIX}-gcc -std=c17 -pedantic-errors -Wall -Wextra -Werror -Os \
	    -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -nostdinc \
	    -I${TOPSRC}/include -x c -c bad_malloc.c.in -o $@

check-cross: bad_malloc.o check_object.sh
	@if sh ./check_object.sh ${GCCPREFIX}-ld ${GCCPREFIX}-nm ${GCCPREFIX}-size \
	    bad_malloc.o >/dev/null 2>&1; then \
		echo "libcalc: symbol check accepted an allocator reference" >&2; exit 1; \
	fi
	@echo "libcalc: symbol check rejects the allocator negative control"
	${MAKE} -C ${LIBDIR} all
	cd ${LIBDIR} && set -- && for f in calc_*.o; do \
		case $$f in *.host.o) ;; *) set -- "$$@" "$$f" ;; esac; done && \
	    sh ${.CURDIR}/check_object.sh ${GCCPREFIX}-ld ${GCCPREFIX}-nm \
	    ${GCCPREFIX}-size "$$@"

# The host complex.h grid is left out: newlib's catan, casinh, cacosh and
# catanh do not follow Annex G on the branch cuts, so they cannot serve as
# the reference there. The Annex G table in complex_vectors.h still runs.
# CALC_TEST_NO_SIGNALS drops the fault recovery of the NULL-pointer sweep: the
# bare-metal C library has no sigaction, and a fault ends the run.
calc_test_arm: calc_test.c complex_vectors.h ${LIBSRCS} ${HEADERS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -DCALC_TEST_NO_HOST_COMPLEX \
	    -DCALC_TEST_NO_SIGNALS -I${LIBDIR} \
	    -o $@ calc_test.c ${QEMU_SUPPORT} ${LIBSRCS} -lm ${QEMU_ARM_LIBS}

check-qemu:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "libcalc qemu: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP libcalc qemu: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} calc_test_arm
	${QEMU_ARM} ./calc_test_arm
.endif

clean:
	rm -f calc_test calc_test32 calc_test_san calc_test_treelibm calc_test_arm tl_*.o \
	    bad_malloc.o mutants.stamp null_sweep_negative.c
	$(PYTHON) -I -c 'import shutil; shutil.rmtree("mutants", ignore_errors=True)'

.PHONY: all vectors check check-native check-ilp32 check-sanitized \
    check-treelibm check-mutants check-null-sweep check-cross check-qemu clean
