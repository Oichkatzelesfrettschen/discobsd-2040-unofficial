# Host, ILP32 and Thumb-1 gate for usr.bin/dc and usr.bin/bc.
#
#   check          check-native, check-ilp32, check-sanitized, check-mutants
#   check-native   host builds of both programs from the tree sources against
#                  lib/libbn, run over cases.py: arithmetic against Python
#                  integers, the register table against a model, and the rest
#                  against golden.json
#   check-ilp32    the same at -m32, where u_long, size_t and pointers are the
#                  target's 32 bits
#   check-sanitized the same under AddressSanitizer and UndefinedBehavior
#                  Sanitizer
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

all: check

build/bc.c build/bc.h: ${BCDIR}/bc.y
	mkdir -p build
	cd build && ${BISON} -y -Wno-yacc -o bc.c -d ${BCDIR}/bc.y

build/scan.c: ${BCDIR}/scan.l
	mkdir -p build
	${LEX_HOST} -o $@ ${BCDIR}/scan.l

# One variant is a directory of dc and bc built with its own flags.
VARIANT_native=
VARIANT_ilp32=	-m32
VARIANT_san=	${SAN_FLAGS}

.for v in native ilp32 san
build/${v}/dc/dc: ${DCDIR}/main.c ${DC_FILES} ${DC_HEADERS} host_compat.h
	mkdir -p build/${v}/dc
	${HOSTCC} ${BASE_CFLAGS} ${VARIANT_${v}} -o $@ ${DCDIR}/main.c ${DC_FILES}

build/${v}/bc/bc: build/bc.c build/bc.h build/scan.c ${DC_FILES} ${DC_HEADERS} \
		${BC_HEADERS} ${BCDIR}/bc.library host_compat.h
	mkdir -p build/${v}/bc
	${HOSTCC} ${BASE_CFLAGS} ${VARIANT_${v}} \
	    -D'_PATH_LIBB="${BCDIR}/bc.library"' -Ibuild -I${BCDIR} -o $@ \
	    build/bc.c build/scan.c ${DC_FILES}
.endfor

# dc with the allocation accounting of peak_alloc.c, for the heap figures.
build/peak/dc/dc: ${DCDIR}/main.c ${DC_FILES} ${DC_HEADERS} host_compat.h peak_alloc.c
	mkdir -p build/peak/dc
	${HOSTCC} ${BASE_CFLAGS} -m32 -o $@ ${DCDIR}/main.c ${DC_FILES} peak_alloc.c \
	    -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=reallocarray,--wrap=strdup,--wrap=free

include ${TOPSRC}/tools/host-test.mk

.if ${HOST_OS} == "Linux"
RUN_CASES=	${PYTHON} -I run_cases.py --golden golden.json

check: check-native check-ilp32 check-sanitized check-mutants

check-native: build/native/dc/dc build/native/bc/bc
	${RUN_CASES} --dc build/native/dc/dc --bc build/native/bc/bc

check-ilp32:
.if ${ILP32_OK} == "yes"
	@${MAKE} build/ilp32/dc/dc build/ilp32/bc/bc
	${RUN_CASES} --dc build/ilp32/dc/dc --bc build/ilp32/bc/bc
.else
	@echo "SKIP dcbc ilp32: ILP32 runtime unavailable; no -m32 execution occurred"
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
check-peak: build/peak/dc/dc
	${PYTHON} -I run_cases.py --peak build/peak/dc/dc --peak-limits peak.json

check-mutants: build/native/dc/dc build/native/bc/bc
	HOSTCC="${HOSTCC}" BISON="${BISON}" LEX_HOST="${LEX_HOST}" \
	    ${PYTHON} -I mutate.py --root ${TOPSRC} --golden golden.json \
	    --work build/mutants

# dc cases only: bc needs fork, which newlib in user mode lacks. newlib's
# getopt neither has optreset nor stops at the first operand the way
# lib/libc/stdlib/getopt.c does, so the tree's getopt is linked in its place:
# the program then parses its arguments with the code the target runs.
getopt-arm.o: ${TOPSRC}/lib/libc/stdlib/getopt.c
	${QEMU_ARM_CC} -std=gnu17 -w -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
	    -include ${.CURDIR}/host_compat.h \
	    -c -o $@ ${TOPSRC}/lib/libc/stdlib/getopt.c

dc-arm: ${DCDIR}/main.c ${DC_FILES} ${DC_HEADERS} host_compat.h qemu_shim.c \
		qemu_shim/err.h getopt-arm.o ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -include ${.CURDIR}/host_compat.h \
	    -I${.CURDIR}/qemu_shim -I${LIBBN} -I${DCDIR} -o $@ ${DCDIR}/main.c \
	    ${DCDIR}/dc.c ${DCDIR}/bcode.c ${DCDIR}/inout.c ${DCDIR}/mem.c \
	    ${DCDIR}/stack.c ${LIBBN}/bn.c qemu_shim.c getopt-arm.o ${QEMU_SUPPORT} \
	    ${QEMU_ARM_LIBS}

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
check check-native check-ilp32 check-sanitized check-mutants check-peak check-qemu:
	@echo "SKIP dcbc: the gate runs on Linux hosts only; no execution occurred"
.endif

clean:
	rm -rf build dc-arm getopt-arm.o __pycache__

.PHONY: all check check-native check-ilp32 check-sanitized check-peak \
    check-mutants check-qemu clean
