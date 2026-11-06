# Host, ILP32 and Thumb-1 gate for usr.bin/dc and usr.bin/bc.
#
#   check          check-process-groups, check-native, check-ilp32,
#                  check-sanitized, check-mutants
#   check-native   host builds of both programs from the tree sources against
#                  lib/libbn, run over cases.py: arithmetic against Python
#                  integers, the register table against a model, and the rest
#                  against golden.json
#   check-ilp32    the same at -m32, where u_long, size_t and pointers are the
#                  target's 32 bits
#   check-sanitized the same under AddressSanitizer and UndefinedBehavior
#                  Sanitizer
#   check-process-groups
#                  procgroup.py's calibration of the leak scan: a single-child
#                  kill leaves an orphan the scan finds, the group launch of
#                  run_cases.py and mutate.py leaves none
#   check-mutants  every faulted copy of bcode.c's register table and bc.y's
#                  name table must be rejected by the cases
#   check-qemu     dc as a Thumb-1 (ARMv6-M) program under qemu-arm user-mode
#                  emulation over the dc cases; bc forks and is not run there
#
# golden.json holds the output of the pinned OpenBSD sources linked against
# libcrypto (and libedit for bc); gen_golden.sh regenerates it. Only Linux
# hosts run the gate: it depends on glibc's exported __progname and on
# bison's yacc mode.
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

.if empty(PYTHON)
.error PYTHON must name the intended interpreter
.endif

HOSTCC?=	cc
ILP32_CC=	${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk

DCDIR=		${TOPSRC}/usr.bin/dc
BCDIR=		${TOPSRC}/usr.bin/bc
LIBBN=		${TOPSRC}/lib/libbn
HOST_OS!=	uname -s
BISON!=		for b in /opt/homebrew/opt/bison/bin/bison /usr/local/opt/bison/bin/bison ; do \
			if [ -x "$$b" ] ; then echo "$$b" ; exit 0 ; fi ; \
		done ; echo bison
LEX_HOST?=	flex

# The programs name themselves from argv[0], so each binary is called dc or
# bc inside a directory named for its variant.
BASE_CFLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror -D_DEFAULT_SOURCE \
		-O1 -include ${.CURDIR}/host_compat.h -I${LIBBN}
DC_FILES=	${DCDIR}/dc.c ${DCDIR}/bcode.c ${DCDIR}/inout.c ${DCDIR}/mem.c \
		${DCDIR}/stack.c ${LIBBN}/bn.c host_compat.c
DC_HEADERS=	${DCDIR}/compat.h ${DCDIR}/extern.h ${DCDIR}/bcode.h ${LIBBN}/bn.h
BC_HEADERS=	${BCDIR}/extern.h ${BCDIR}/pathnames.h
# The tree's malloc(0) returns NULL where a host C library returns a pointer;
# zero_alloc.c restores the tree's answer in every dc and bc built here.
ZERO_ALLOC=	zero_alloc.c -Wl,--wrap=malloc,--wrap=realloc,--wrap=reallocarray

all: check

# One bison run writes the parser and its header. build/bc.c is the only
# target with that recipe, so a parallel make cannot start a second writer;
# build/bc.h waits for it and regenerates only when the header alone is gone.
BC_BISON=	${BISON} -y -Wno-yacc -Wno-conflicts-sr -Wno-conflicts-rr -o bc.c -d ${BCDIR}/bc.y

build/bc.c: ${BCDIR}/bc.y
	mkdir -p build
	cd build && ${BC_BISON}

build/bc.h: build/bc.c
	@test -f $@ || (cd build && ${BC_BISON})

build/scan.c: ${BCDIR}/scan.l
	mkdir -p build
	${LEX_HOST} -o $@ ${BCDIR}/scan.l

# One variant is a directory of dc and bc built with its own flags.
VARIANT_native=
VARIANT_ilp32=	-m32
VARIANT_san=	${SAN_FLAGS}

.for v in native ilp32 san
build/${v}/dc/dc: ${DCDIR}/main.c ${DC_FILES} ${DC_HEADERS} host_compat.h zero_alloc.c
	mkdir -p build/${v}/dc
	${HOSTCC} ${BASE_CFLAGS} ${VARIANT_${v}} -o $@ ${DCDIR}/main.c ${DC_FILES} ${ZERO_ALLOC}

build/${v}/bc/bc: build/bc.c build/bc.h build/scan.c ${DC_FILES} ${DC_HEADERS} \
		${BC_HEADERS} ${BCDIR}/bc.library host_compat.h zero_alloc.c
	mkdir -p build/${v}/bc
	${HOSTCC} ${BASE_CFLAGS} ${VARIANT_${v}} \
	    -D'_PATH_LIBB="${BCDIR}/bc.library"' -Ibuild -I${BCDIR} -o $@ \
	    build/bc.c build/scan.c ${DC_FILES} ${ZERO_ALLOC}
.endfor

# dc with the allocation accounting of peak_alloc.c, for the heap figures.
build/peak/dc/dc: ${DCDIR}/main.c ${DC_FILES} ${DC_HEADERS} host_compat.h peak_alloc.c
	mkdir -p build/peak/dc
	${HOSTCC} ${BASE_CFLAGS} -m32 -o $@ ${DCDIR}/main.c ${DC_FILES} peak_alloc.c \
	    -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=reallocarray,--wrap=strdup,--wrap=free

include ${TOPSRC}/tools/host-test.mk

.if ${HOST_OS} == "Linux"
RUN_CASES=	${PYTHON} -I run_cases.py --golden golden.json

# The recursive makes of check-ilp32 and check-sanitized run beside
# check-native under -j, so the pair exists before any variant starts.
check-native check-ilp32 check-sanitized: build/bc.h

check: check-process-groups check-grammar check-native check-ilp32 check-sanitized check-mutants

# usr.bin/bc builds with bison's conflict warnings off, because an LALR
# bison cannot %expect reduce/reduce conflicts; this target holds the
# grammar at the one shift/reduce and sixteen reduce/reduce conflicts the
# OpenBSD grammar has.
check-grammar:
	@sh ${.CURDIR}/check_grammar.sh ${BISON} ${BCDIR}/bc.y

check-process-groups:
	mkdir -p build
	${PYTHON} -I procgroup.py --selftest build

check-native: build/native/dc/dc build/native/bc/bc
	${RUN_CASES} --dc build/native/dc/dc --bc build/native/bc/bc

# tools/compiler-probe.sh proves a -m32 program links and runs with
# <stddef.h> alone; dc and bc also need the 32-bit C library headers
# (<limits.h>, <sys/stat.h>), which a runner can lack while the probe
# passes. REQUIRE_ILP32=1, set by the provisioned ILP32 CI job, turns a
# missing runtime into a failure instead of a SKIP.
ILP32_LIBC_OK!=	printf '\043include <limits.h>\n\043include <stdio.h>\n\043include <sys/stat.h>\nint main(void) { return 0; }\n' | \
		${ILP32_CC:Ucc} -m32 -x c -o /dev/null - >/dev/null 2>&1 && echo yes || echo no
REQUIRE_ILP32?=	0

check-ilp32:
.if ${ILP32_OK} == "yes" && ${ILP32_LIBC_OK} == "yes"
	@${MAKE} build/ilp32/dc/dc build/ilp32/bc/bc
	${RUN_CASES} --dc build/ilp32/dc/dc --bc build/ilp32/bc/bc
.elif ${REQUIRE_ILP32} == "1"
	@echo "FAIL dcbc ilp32: REQUIRE_ILP32=1 but no -m32 C library (probe ${ILP32_OK}, headers ${ILP32_LIBC_OK})" >&2; exit 1
.else
	@echo "SKIP dcbc ilp32: no -m32 C library (probe ${ILP32_OK}, headers ${ILP32_LIBC_OK}); no -m32 execution occurred"
.endif

check-sanitized:
.if ${SAN_OK} == "yes"
	@${MAKE} build/san/dc/dc build/san/bc/bc
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${RUN_CASES} --dc build/san/dc/dc --bc build/san/bc/bc
.else
	@echo "SKIP dcbc sanitizers: sanitizer runtime unavailable; no sanitized run occurred"
.endif

# Peak live heap of dc at -m32 over the programs in peak.json, against the
# ceilings recorded there.
check-peak:
.if ${ILP32_OK} == "yes" && ${ILP32_LIBC_OK} == "yes"
	@${MAKE} build/peak/dc/dc
	${PYTHON} -I run_cases.py --peak build/peak/dc/dc --peak-limits peak.json
.elif ${REQUIRE_ILP32} == "1"
	@echo "FAIL dcbc peak: REQUIRE_ILP32=1 but no -m32 C library (probe ${ILP32_OK}, headers ${ILP32_LIBC_OK})" >&2; exit 1
.else
	@echo "SKIP dcbc peak: no -m32 C library (probe ${ILP32_OK}, headers ${ILP32_LIBC_OK}); no heap was measured"
.endif

# The heap-leak mutants are rejected only by the -m32 peak run, so without
# the 32-bit C library they are reported UNJUDGED; REQUIRE_ILP32=1 fails.
MUTATE=		HOSTCC="${HOSTCC}" BISON="${BISON}" LEX_HOST="${LEX_HOST}" \
		${PYTHON} -I mutate.py --root ${TOPSRC} --golden golden.json \
		--work build/mutants

check-mutants: build/native/dc/dc build/native/bc/bc
.if ${ILP32_OK} == "yes" && ${ILP32_LIBC_OK} == "yes"
	${MUTATE}
.elif ${REQUIRE_ILP32} == "1"
	@echo "FAIL dcbc mutants: REQUIRE_ILP32=1 but no -m32 C library for the heap run" >&2; exit 1
.else
	${MUTATE} --no-peak
.endif

# dc cases only: bc needs fork, which newlib in user mode lacks. newlib's
# getopt neither has optreset nor stops at the first operand the way
# lib/libc/stdlib/getopt.c does, so the tree's getopt is linked in its place:
# the program then parses its arguments with the code the target runs.
getopt-arm.o: ${TOPSRC}/lib/libc/stdlib/getopt.c
	${QEMU_ARM_CC} -std=gnu17 -w -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
	    -include ${.CURDIR}/host_compat.h \
	    -c -o $@ ${TOPSRC}/lib/libc/stdlib/getopt.c

dc-arm: ${DCDIR}/main.c ${DC_FILES} ${DC_HEADERS} host_compat.h qemu_shim.c zero_alloc.c \
		qemu_shim/err.h getopt-arm.o ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -include ${.CURDIR}/host_compat.h \
	    -I${.CURDIR}/qemu_shim -I${LIBBN} -I${DCDIR} -o $@ ${DCDIR}/main.c \
	    ${DCDIR}/dc.c ${DCDIR}/bcode.c ${DCDIR}/inout.c ${DCDIR}/mem.c \
	    ${DCDIR}/stack.c ${LIBBN}/bn.c qemu_shim.c getopt-arm.o ${QEMU_SUPPORT} \
	    ${ZERO_ALLOC} ${QEMU_ARM_LIBS}

check-qemu:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "dcbc qemu: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP dcbc qemu: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} dc-arm
	mkdir -p build/arm
	cp dc-arm build/arm/dc
	${RUN_CASES} --dc "${QEMU_ARM} build/arm/dc" --dc-only --corpus-limit 120 --no-host-text
.endif
.else
check check-process-groups check-native check-ilp32 check-sanitized check-mutants check-peak check-qemu:
	@echo "SKIP dcbc: the gate runs on Linux hosts only; no execution occurred"
.endif

clean:
	rm -rf build dc-arm getopt-arm.o __pycache__

.PHONY: all check check-grammar check-process-groups check-native check-ilp32 check-sanitized check-peak \
    check-mutants check-qemu clean
