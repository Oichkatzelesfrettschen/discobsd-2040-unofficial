TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

HOSTCC?=	cc
STDLIB_DIR=	${TOPSRC}/lib/libc/stdlib
GEN_DIR=	${TOPSRC}/lib/libc/gen

# Strict C17 translation of the public headers for the selected target.  -I
# rather than -isystem keeps the headers themselves under -pedantic-errors.
# Each contract compiles to an object, because -fsyntax-only stops before
# the flow analysis that -Wreturn-type reports from.
TARGET_CC=	${GCCPREFIX}-gcc
TARGET_FLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror \
		-Wstrict-prototypes -Wmissing-prototypes \
		-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -Os \
		-nostdinc -I${TOPSRC}/include
MUTATE=		${PYTHON} ${.CURDIR}/mutate.py

HEADER_CONTRACTS=	stddef_contract stdalign_contract stdnoreturn_contract \
			limits_contract stdint_contract inttypes_contract
HEADER_MUTANTS=		stddef_no_max_align stdalign_weak_alignas \
			stdnoreturn_empty limits_llong_min_short \
			stdint_unsigned_max stdint_narrow_intmax \
			inttypes_pri64_long inttypes_scn16_int \
			inttypes_scn64_defined

# Each mutant header tree is the public one with a single header replaced,
# found first on the include path.  A contract that still translates against
# it measures nothing.  Both doubleword members go, since either alone
# keeps the union at the greatest alignment.
stddef_no_max_align.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/stddef.h $@/stddef.step \
	    "long long	__max_align_integer;" ""
	${MUTATE} $@/stddef.step $@/stddef.h \
	    "long double	__max_align_floating;" ""
stddef_no_max_align.contract= stddef_contract

# alignas that expands to nothing still parses, so only the layout
# assertions see it.
stdalign_weak_alignas.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/stdalign.h $@/stdalign.h \
	    "#define alignas			_Alignas" "#define alignas(x)"
stdalign_weak_alignas.contract= stdalign_contract

# A noreturn that expands to nothing leaves a declaration that still
# parses; the missing return then reaches -Wreturn-type.
stdnoreturn_empty.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/stdnoreturn.h $@/stdnoreturn.h \
	    "#define noreturn	_Noreturn" "#define noreturn"
stdnoreturn_empty.contract= stdnoreturn_contract

# <limits.h> reaches the machine header through the machine symlink, so the
# mutant tree carries its own machine directory.
limits_llong_min_short.mutant:
	@rm -rf $@ && mkdir -p $@/machine
	${MUTATE} ${TOPSRC}/include/machine/limits.h $@/machine/limits.h \
	    "(-9223372036854775807LL-1LL)" "(-9223372036854775807LL)"
limits_llong_min_short.contract= limits_contract

# <stdint.h> is a link to sys/sys/stdint.h, so the mutants read that file.
stdint_unsigned_max.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/sys/sys/stdint.h $@/stdint.h \
	    "#define UINT16_MAX          0xFFFF" \
	    "#define UINT16_MAX          0xFFFFU"
stdint_unsigned_max.contract= stdint_contract
stdint_narrow_intmax.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/sys/sys/stdint.h $@/stdint.h \
	    "typedef long long           intmax_t;" \
	    "typedef long                intmax_t;"
stdint_narrow_intmax.contract= stdint_contract

# The first two name a length modifier that disagrees with the type, which
# -Wformat reports.  The third defines a 64-bit fscanf macro that _doscan
# cannot honor.
inttypes_pri64_long.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/inttypes.h $@/inttypes.h \
	    '#define PRId64          "lld"' '#define PRId64          "ld"'
inttypes_pri64_long.contract= inttypes_contract
inttypes_scn16_int.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/inttypes.h $@/inttypes.h \
	    '#define SCNd16          "hd"' '#define SCNd16          "d"'
inttypes_scn16_int.contract= inttypes_contract
inttypes_scn64_defined.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/inttypes.h $@/inttypes.h \
	    '#define SCNd32          "d"' \
	    '#define SCNd32          "d"${.newline}#define SCNd64 "lld"'
inttypes_scn64_defined.contract= inttypes_contract

check-cross: check-header-contracts check-header-mutants check-imports

check-header-contracts:
.for contract in ${HEADER_CONTRACTS}
	${TARGET_CC} ${TARGET_FLAGS} -c -o ${contract}.o ${contract}.c
.endfor
	@echo "c17 headers: ${HEADER_CONTRACTS} translate as strict C17"

check-header-mutants: ${HEADER_MUTANTS:S/$/.mutant/}
.for mutant in ${HEADER_MUTANTS}
	@if ${TARGET_CC} -I${mutant}.mutant ${TARGET_FLAGS} -c \
	    -o ${mutant}.o ${${mutant}.contract}.c >${mutant}.log 2>&1; then \
		echo "c17 headers: mutant ${mutant} translated" >&2; exit 1; \
	fi
	@grep -q 'error' ${mutant}.log || { cat ${mutant}.log >&2; exit 1; }
.endfor
	@echo "c17 headers: ${HEADER_MUTANTS} rejected"

# The tree's sources under db_ names, so the hosting C library keeps the
# standard names and serves as the oracle.  -fsigned-char on the host makes
# an uncast input byte visible; the target's char is unsigned.
HOST_FLAGS=	-std=c17 -Wall -Wextra -Werror
STRTOLL_RENAMES= -Dstrtoull=db_strtoull -Dstrtoll=db_strtoll -Datoll=db_atoll
STRTOLL_SRCS=	strtoull strtoll atoll
ALLOC_RENAMES=	-Dmalloc=db_malloc -Dfree=db_free -Drealloc=db_realloc \
		-Daligned_alloc=db_aligned_alloc
# malloc.c keeps its old-style definitions; the kernel tier's excuse for
# them applies to the one unit that includes it.
ARENA_FLAGS=	-std=gnu17 -Wall -Wextra -Werror -Wno-deprecated-non-prototype

.for src in ${STRTOLL_SRCS}
host_${src}.o: ${STDLIB_DIR}/${src}.c ${STDLIB_DIR}/strtou64.h
	${HOSTCC} ${HOST_FLAGS} -fsigned-char ${STRTOLL_RENAMES} -c -o $@ \
	    ${STDLIB_DIR}/${src}.c
arm_${src}.o: ${STDLIB_DIR}/${src}.c ${STDLIB_DIR}/strtou64.h
	${QEMU_ARM_CC} ${HOST_FLAGS} -Os ${QEMU_ARM_CPUFLAGS} -fno-common \
	    ${STRTOLL_RENAMES} -c -o $@ ${STDLIB_DIR}/${src}.c
.endfor

strtoll_test: strtoll_test.c ${STRTOLL_SRCS:S/^/host_/:S/$/.o/}
	${HOSTCC} ${HOST_FLAGS} -fsigned-char -o $@ strtoll_test.c \
	    ${STRTOLL_SRCS:S/^/host_/:S/$/.o/}

strtoll_test_arm: strtoll_test.c ${STRTOLL_SRCS:S/^/arm_/:S/$/.o/} \
    ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -o $@ strtoll_test.c \
	    ${STRTOLL_SRCS:S/^/arm_/:S/$/.o/} ${QEMU_SUPPORT} ${QEMU_ARM_LIBS}

# The inttypes members; strtoimax and strtoumax reach the tree's strtoll
# and strtoull through the same renames.
INTTYPES_RENAMES= -Dimaxdiv=db_imaxdiv -Dimaxabs=db_imaxabs \
		-Dstrtoimax=db_strtoimax -Dstrtoumax=db_strtoumax \
		${STRTOLL_RENAMES}
INTTYPES_SRCS=	imaxdiv imaxabs strtoimax strtoumax
INTTYPES_HOST_OBJS= ${INTTYPES_SRCS:S/^/host_/:S/$/.o/} host_strtoull.o \
		host_strtoll.o
INTTYPES_ARM_OBJS= ${INTTYPES_SRCS:S/^/arm_/:S/$/.o/} arm_strtoull.o \
		arm_strtoll.o

.for src in ${INTTYPES_SRCS}
host_${src}.o: ${STDLIB_DIR}/${src}.c
	${HOSTCC} ${HOST_FLAGS} ${INTTYPES_RENAMES} -c -o $@ \
	    ${STDLIB_DIR}/${src}.c
arm_${src}.o: ${STDLIB_DIR}/${src}.c
	${QEMU_ARM_CC} ${HOST_FLAGS} -Os ${QEMU_ARM_CPUFLAGS} -fno-common \
	    ${INTTYPES_RENAMES} -c -o $@ ${STDLIB_DIR}/${src}.c
.endfor

inttypes_test: inttypes_test.c ${INTTYPES_HOST_OBJS}
	${HOSTCC} ${HOST_FLAGS} -o $@ inttypes_test.c ${INTTYPES_HOST_OBJS}

inttypes_test_arm: inttypes_test.c ${INTTYPES_ARM_OBJS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -o $@ inttypes_test.c \
	    ${INTTYPES_ARM_OBJS} ${QEMU_SUPPORT} ${QEMU_ARM_LIBS}

host_arena.o: arena_host.c ${GEN_DIR}/malloc.c ${GEN_DIR}/malloc_store.h
	${HOSTCC} ${ARENA_FLAGS} ${ALLOC_RENAMES} -c -o $@ arena_host.c
arm_arena.o: arena_host.c ${GEN_DIR}/malloc.c ${GEN_DIR}/malloc_store.h
	${QEMU_ARM_CC} ${ARENA_FLAGS} -Os ${QEMU_ARM_CPUFLAGS} -fno-common \
	    ${ALLOC_RENAMES} -c -o $@ arena_host.c
host_aligned_alloc.o: ${GEN_DIR}/aligned_alloc.c ${GEN_DIR}/malloc_store.h
	${HOSTCC} ${HOST_FLAGS} ${ALLOC_RENAMES} -c -o $@ \
	    ${GEN_DIR}/aligned_alloc.c
arm_aligned_alloc.o: ${GEN_DIR}/aligned_alloc.c ${GEN_DIR}/malloc_store.h
	${QEMU_ARM_CC} ${HOST_FLAGS} -Os ${QEMU_ARM_CPUFLAGS} -fno-common \
	    ${ALLOC_RENAMES} -c -o $@ ${GEN_DIR}/aligned_alloc.c

aligned_alloc_test: aligned_alloc_test.c host_arena.o host_aligned_alloc.o
	${HOSTCC} ${HOST_FLAGS} -o $@ aligned_alloc_test.c host_arena.o \
	    host_aligned_alloc.o

aligned_alloc_test_arm: aligned_alloc_test.c arm_arena.o \
    arm_aligned_alloc.o ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -o $@ aligned_alloc_test.c \
	    arm_arena.o arm_aligned_alloc.o ${QEMU_SUPPORT} ${QEMU_ARM_LIBS}

host_Exit.o: ${STDLIB_DIR}/_Exit.c
	${HOSTCC} ${HOST_FLAGS} -D_Exit=db_Exit -c -o $@ ${STDLIB_DIR}/_Exit.c

exit_probe: exit_probe.c host_Exit.o
	${HOSTCC} ${HOST_FLAGS} -o $@ exit_probe.c host_Exit.o

# One mutated source per mutant, each linked into the test that owns the
# contract it breaks.  A mutant that passes its test fails the gate.
#   strtoull_no_overflow  the limb carry never reports overflow
#   strtoll_no_llong_min  the negative limit loses LLONG_MIN's extra value
#   aligned_leak          the lead-in in front of an aligned block stays busy
#   aligned_half          the address is rounded to half the alignment
#   exit_runs_handlers    _Exit calls exit()
#   imaxdiv_rem_sign      the remainder takes the denominator's sign
#   imaxdiv_short_loop    the division stops one quotient bit early
strtoull_no_overflow.mutant.c: ${STDLIB_DIR}/strtoull.c
	${MUTATE} ${STDLIB_DIR}/strtoull.c $@ \
	    "result.overflow = 1;" "result.overflow = 0;"
strtoll_no_llong_min.mutant.c: ${STDLIB_DIR}/strtoll.c
	${MUTATE} ${STDLIB_DIR}/strtoll.c $@ \
	    "(unsigned long long)LLONG_MAX + result.negative" \
	    "(unsigned long long)LLONG_MAX"
aligned_leak.mutant.c: ${GEN_DIR}/aligned_alloc.c
	${MUTATE} ${GEN_DIR}/aligned_alloc.c $@ "free(block);" ""
aligned_half.mutant.c: ${GEN_DIR}/aligned_alloc.c
	${MUTATE} ${GEN_DIR}/aligned_alloc.c $@ \
	    "~(INT)(alignment - 1));" "~(INT)(alignment / 2 - 1));"
imaxdiv_rem_sign.mutant.c: ${STDLIB_DIR}/imaxdiv.c
	${MUTATE} ${STDLIB_DIR}/imaxdiv.c $@ \
	    "result.rem = numerator < 0" "result.rem = denominator < 0"
imaxdiv_short_loop.mutant.c: ${STDLIB_DIR}/imaxdiv.c
	${MUTATE} ${STDLIB_DIR}/imaxdiv.c $@ "bit < 64" "bit < 63"
exit_runs_handlers.mutant.c: ${STDLIB_DIR}/_Exit.c
	${MUTATE} ${STDLIB_DIR}/_Exit.c $@ "	_exit(status);" "	exit(status);"

# The renames reach the mutated source alone.  Applied to strtoll_test.c
# they would rename its oracle calls as well, and the test would compare
# the mutant with itself.
.for mutant in strtoull_no_overflow strtoll_no_llong_min
${mutant}.mutant.o: ${mutant}.mutant.c ${STDLIB_DIR}/strtou64.h
	${HOSTCC} ${HOST_FLAGS} -fsigned-char ${STRTOLL_RENAMES} \
	    -I${STDLIB_DIR} -c -o $@ ${mutant}.mutant.c
.endfor
strtoull_no_overflow.mutant.test: strtoull_no_overflow.mutant.o \
    strtoll_test.c host_strtoll.o host_atoll.o
	${HOSTCC} ${HOST_FLAGS} -fsigned-char -o $@ strtoll_test.c \
	    strtoull_no_overflow.mutant.o host_strtoll.o host_atoll.o
strtoll_no_llong_min.mutant.test: strtoll_no_llong_min.mutant.o \
    strtoll_test.c host_strtoull.o host_atoll.o
	${HOSTCC} ${HOST_FLAGS} -fsigned-char -o $@ strtoll_test.c \
	    host_strtoull.o strtoll_no_llong_min.mutant.o host_atoll.o
.for mutant in aligned_leak aligned_half
${mutant}.mutant.o: ${mutant}.mutant.c
	${HOSTCC} ${HOST_FLAGS} ${ALLOC_RENAMES} -I${GEN_DIR} -c -o $@ \
	    ${mutant}.mutant.c
${mutant}.mutant.test: ${mutant}.mutant.o aligned_alloc_test.c host_arena.o
	${HOSTCC} ${HOST_FLAGS} -o $@ aligned_alloc_test.c host_arena.o \
	    ${mutant}.mutant.o
.endfor
.for mutant in imaxdiv_rem_sign imaxdiv_short_loop
${mutant}.mutant.o: ${mutant}.mutant.c
	${HOSTCC} ${HOST_FLAGS} ${INTTYPES_RENAMES} -c -o $@ \
	    ${mutant}.mutant.c
${mutant}.mutant.test: ${mutant}.mutant.o inttypes_test.c \
    ${INTTYPES_HOST_OBJS}
	${HOSTCC} ${HOST_FLAGS} -o $@ inttypes_test.c ${mutant}.mutant.o \
	    ${INTTYPES_HOST_OBJS:Nhost_imaxdiv.o}
.endfor
exit_runs_handlers.mutant.o: exit_runs_handlers.mutant.c
	${HOSTCC} ${HOST_FLAGS} -D_Exit=db_Exit -c -o $@ \
	    exit_runs_handlers.mutant.c
exit_runs_handlers.mutant.test: exit_probe.c exit_runs_handlers.mutant.o
	${HOSTCC} ${HOST_FLAGS} -o $@ exit_probe.c exit_runs_handlers.mutant.o

RUN_MUTANTS=	strtoull_no_overflow strtoll_no_llong_min aligned_leak \
		aligned_half imaxdiv_rem_sign imaxdiv_short_loop
HOST_TESTS=	strtoll_test aligned_alloc_test inttypes_test

check-host: ${HOST_TESTS} exit_probe ${RUN_MUTANTS:S/$/.mutant.test/} \
    exit_runs_handlers.mutant.test
.for test in ${HOST_TESTS}
	./${test}
.endfor
	sh ${.CURDIR}/exit_probe.sh ./exit_probe 7
	@echo "c17 headers: _Exit ran no handler and flushed no stream"
.for mutant in ${RUN_MUTANTS}
	@if ./${mutant}.mutant.test >${mutant}.log 2>&1; then \
		echo "c17 headers: mutant ${mutant} passed its test" >&2; \
		exit 1; \
	fi
.endfor
	@if sh ${.CURDIR}/exit_probe.sh ./exit_runs_handlers.mutant.test 7 \
	    >exit_runs_handlers.log 2>&1; then \
		echo "c17 headers: mutant exit_runs_handlers passed" >&2; \
		exit 1; \
	fi
	@echo "c17 headers: ${RUN_MUTANTS} exit_runs_handlers rejected"

# Thumb-1 execution of the same tests on the ARMv6-M CPU model, where the
# arena word is four bytes and an 8-byte alignment takes the carving path.
# newlib is the oracle there.  REQUIRE_QEMU makes a missing qemu-arm an
# error; otherwise the target prints SKIP and no ARM execution has occurred.
check-qemu:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "c17 headers qemu: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP c17 headers qemu: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} ${HOST_TESTS:S/$/_arm/}
.for test in ${HOST_TESTS}
	${QEMU_ARM_RUN} ./${test}_arm
.endfor
.endif

# The members compiled as libc compiles them import the listed symbols and
# nothing else: no 64-bit multiply or divide routine, which libc does not
# carry.  divide_import_bad.c calibrates the checker on an object that does
# import one.
IMPORT_FLAGS=	${TARGET_FLAGS} -fno-common
strtoull.imports=	errno
strtoll.imports=	errno __discobsd_strtou64
atoll.imports=		strtoll
_Exit.imports=		_exit
aligned_alloc.imports=	errno free malloc realloc
imaxdiv.imports=
imaxabs.imports=
strtoimax.imports=	strtoll
strtoumax.imports=	strtoull
IMPORT_STDLIB=	strtoull strtoll atoll _Exit imaxdiv imaxabs strtoimax \
		strtoumax
IMPORT_GEN=	aligned_alloc

check-imports:
	${TARGET_CC} ${IMPORT_FLAGS} -c -o divide_import_bad.o \
	    divide_import_bad.c
	@if sh ${.CURDIR}/check_imports.sh ${GCCPREFIX}-nm divide_import_bad.o \
	    >divide_import_bad.log 2>&1; then \
		echo "c17 headers: import checker accepted a divide import" >&2; \
		exit 1; \
	fi
	@grep -q '__aeabi_uldivmod' divide_import_bad.log
.for member in ${IMPORT_STDLIB}
	${TARGET_CC} ${IMPORT_FLAGS} -c -o import_${member}.o \
	    ${STDLIB_DIR}/${member}.c
	sh ${.CURDIR}/check_imports.sh ${GCCPREFIX}-nm import_${member}.o \
	    ${${member}.imports}
.endfor
.for member in ${IMPORT_GEN}
	${TARGET_CC} ${IMPORT_FLAGS} -c -o import_${member}.o \
	    ${GEN_DIR}/${member}.c
	sh ${.CURDIR}/check_imports.sh ${GCCPREFIX}-nm import_${member}.o \
	    ${${member}.imports}
.endfor
	@echo "c17 headers: ${IMPORT_STDLIB} ${IMPORT_GEN} import only their listed symbols"

clean:
	rm -rf *.mutant *.log *.o *.mutant.c *.mutant.test \
	    ${HOST_TESTS} ${HOST_TESTS:S/$/_arm/} exit_probe

.PHONY: check-cross check-header-contracts check-header-mutants \
	check-imports check-host check-qemu clean \
	${HEADER_MUTANTS:S/$/.mutant/}
