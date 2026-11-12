TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

HOSTCC?=	cc
STDLIB_DIR=	${TOPSRC}/lib/libc/stdlib
GEN_DIR=	${TOPSRC}/lib/libc/gen
STDIO_DIR=	${TOPSRC}/lib/libc/stdio

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
			limits_contract stdint_contract inttypes_contract \
			ctype_contract stdlib_wide_contract errno_contract
HEADER_MUTANTS=		stddef_no_max_align stdalign_weak_alignas \
			stdnoreturn_empty limits_llong_min_short \
			stdint_unsigned_max stdint_narrow_intmax \
			inttypes_pri64_long inttypes_scn16_int \
			inttypes_scn64_long ctype_no_isblank \
			stddef_wchar_int limits_no_mb_len_max \
			stdlib_no_mbtowc inttypes_no_wcstoimax \
			errno_no_eilseq limits_ulong_uint

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

limits_ulong_uint.mutant:
	@rm -rf $@ && mkdir -p $@/machine
	${MUTATE} ${TOPSRC}/include/machine/limits.h $@/machine/limits.h \
	    "0xffffffffUL" "0xffffffff"
limits_ulong_uint.contract= limits_contract

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

# Each names a length modifier that disagrees with the type, which -Wformat
# reports.
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
inttypes_scn64_long.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/inttypes.h $@/inttypes.h \
	    '#define SCNu64          "llu"' '#define SCNu64          "lu"'
inttypes_scn64_long.contract= inttypes_contract

# A wchar_t of the right width but the wrong type still parses; the type of
# a wide literal then differs from it.
stddef_wchar_int.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/stddef.h $@/stddef.h \
	    "typedef __WCHAR_TYPE__ wchar_t;" "typedef int wchar_t;"
stddef_wchar_int.contract= stddef_contract
limits_no_mb_len_max.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/limits.h $@/limits.h \
	    "#define	MB_LEN_MAX	1" ""
limits_no_mb_len_max.contract= limits_contract
stdlib_no_mbtowc.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/stdlib.h $@/stdlib.h \
	    "int     mbtowc (wchar_t *, const char *, size_t);" ""
stdlib_no_mbtowc.contract= stdlib_wide_contract
inttypes_no_wcstoimax.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/inttypes.h $@/inttypes.h \
	    "intmax_t	wcstoimax(const wchar_t *, wchar_t **, int);" ""
inttypes_no_wcstoimax.contract= inttypes_contract

# <errno.h> is a link to sys/sys/errno.h, which the mutant reads.
errno_no_eilseq.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/sys/sys/errno.h $@/errno.h \
	    "#define EILSEQ          83      /* Illegal byte sequence */" ""
errno_no_eilseq.contract= errno_contract

# Without the prototype the contract's use of isblank is an implicit
# declaration.
ctype_no_isblank.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/ctype.h $@/ctype.h \
	    "int     isblank(int);" ""
ctype_no_isblank.contract= ctype_contract

# null_contract.c once per header that defines NULL, each included alone,
# because the definitions sit under #ifndef and the first one included
# decides the macro.  <sys/dir.h> needs <sys/types.h> in front of it.
NULL_HEADERS=	stddef.h stdio.h stdlib.h string.h time.h unistd.h \
		sys/param.h sys/dir.h
sys/dir.h.null=	-DNULL_NEEDS_TYPES

# One libc header and one header the kernel shares, each with NULL back at
# the integer 0.  The mutant tree is searched first, so only that header
# changes.
null_string_zero.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/string.h $@/string.h \
	    "#define	NULL	((void *)0)" "#define	NULL	0"
null_string_zero.header=	string.h
null_param_zero.mutant:
	@rm -rf $@ && mkdir -p $@/sys
	${MUTATE} ${TOPSRC}/sys/sys/param.h $@/sys/param.h \
	    "#define NULL        ((void *)0)" "#define NULL        0"
null_param_zero.header=	sys/param.h
NULL_MUTANTS=	null_string_zero null_param_zero

check-null-contract: ${NULL_MUTANTS:S/$/.mutant/}
.for header in ${NULL_HEADERS}
	${TARGET_CC} ${TARGET_FLAGS} ${${header}.null} \
	    "-DNULL_HEADER=<${header}>" -c -o null_contract.o null_contract.c
.endfor
.for mutant in ${NULL_MUTANTS}
	@if ${TARGET_CC} -I${mutant}.mutant ${TARGET_FLAGS} \
	    "-DNULL_HEADER=<${${mutant}.header}>" -c -o ${mutant}.o \
	    null_contract.c >${mutant}.log 2>&1; then \
		echo "c17 headers: mutant ${mutant} translated" >&2; exit 1; \
	fi
	@grep -q 'NULL has type void' ${mutant}.log || \
	    { cat ${mutant}.log >&2; exit 1; }
.endfor
	@echo "c17 headers: NULL is a pointer constant in ${NULL_HEADERS}; ${NULL_MUTANTS} rejected"

check-cross: check-header-contracts check-header-mutants check-imports \
    check-null-contract

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
host_${src}.o: ${STDLIB_DIR}/${src}.c ${STDLIB_DIR}/strtou64.h \
    ${STDLIB_DIR}/strtou64_scan.h
	${HOSTCC} ${HOST_FLAGS} -fsigned-char ${STRTOLL_RENAMES} -c -o $@ \
	    ${STDLIB_DIR}/${src}.c
arm_${src}.o: ${STDLIB_DIR}/${src}.c ${STDLIB_DIR}/strtou64.h \
    ${STDLIB_DIR}/strtou64_scan.h
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

# The C-locale multibyte functions and the wide conversions, with the
# tree's strtoll and strtoull as the oracle for the latter.  -fsigned-char
# on the host makes a byte that mbtowc fails to read as unsigned char
# visible.
WIDE_RENAMES=	-Dmblen=db_mblen -Dmbtowc=db_mbtowc -Dwctomb=db_wctomb \
		-Dmbstowcs=db_mbstowcs -Dwcstombs=db_wcstombs \
		-Dwcstoimax=db_wcstoimax -Dwcstoumax=db_wcstoumax
WIDE_SRCS=	mblen mbtowc wctomb mbstowcs wcstombs wcstoimax wcstoumax
WIDE_HOST_OBJS=	${WIDE_SRCS:S/^/host_/:S/$/.o/} host_strtoull.o host_strtoll.o
WIDE_ARM_OBJS=	${WIDE_SRCS:S/^/arm_/:S/$/.o/} arm_strtoull.o arm_strtoll.o

.for src in ${WIDE_SRCS}
host_${src}.o: ${STDLIB_DIR}/${src}.c ${STDLIB_DIR}/strtou64.h \
    ${STDLIB_DIR}/strtou64_scan.h
	${HOSTCC} ${HOST_FLAGS} -fsigned-char ${WIDE_RENAMES} -c -o $@ \
	    ${STDLIB_DIR}/${src}.c
arm_${src}.o: ${STDLIB_DIR}/${src}.c ${STDLIB_DIR}/strtou64.h \
    ${STDLIB_DIR}/strtou64_scan.h
	${QEMU_ARM_CC} ${HOST_FLAGS} -Os ${QEMU_ARM_CPUFLAGS} -fno-common \
	    ${WIDE_RENAMES} -c -o $@ ${STDLIB_DIR}/${src}.c
.endfor

wide_test: wide_test.c ${WIDE_HOST_OBJS}
	${HOSTCC} ${HOST_FLAGS} -o $@ wide_test.c ${WIDE_HOST_OBJS}

wide_test_arm: wide_test.c ${WIDE_ARM_OBJS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -o $@ wide_test.c \
	    ${WIDE_ARM_OBJS} ${QEMU_SUPPORT} ${QEMU_ARM_LIBS}

# The tree's <ctype.h> alone on the include path, as a copy in ctype_tree,
# so no other tree header reaches a hosted test.  ctype_rename.h gives the
# functions and the table db_ link names.
CTYPE_SRCS=	ctype_ isctype isblank tolower toupper isascii
CTYPE_FLAGS=	-include ${.CURDIR}/ctype_rename.h
CTYPE_HOST_OBJS= ${CTYPE_SRCS:S/^/host_ct_/:S/$/.o/}
CTYPE_ARM_OBJS=	${CTYPE_SRCS:S/^/arm_ct_/:S/$/.o/}

ctype_tree/ctype.h: ${TOPSRC}/include/ctype.h
	@mkdir -p ctype_tree
	cp ${TOPSRC}/include/ctype.h $@

.for src in ${CTYPE_SRCS}
host_ct_${src}.o: ${GEN_DIR}/${src}.c ctype_tree/ctype.h ctype_rename.h
	${HOSTCC} ${HOST_FLAGS} -Ictype_tree ${CTYPE_FLAGS} -c -o $@ \
	    ${GEN_DIR}/${src}.c
arm_ct_${src}.o: ${GEN_DIR}/${src}.c ctype_tree/ctype.h ctype_rename.h
	${QEMU_ARM_CC} ${HOST_FLAGS} -Os ${QEMU_ARM_CPUFLAGS} -fno-common \
	    -Ictype_tree ${CTYPE_FLAGS} -c -o $@ ${GEN_DIR}/${src}.c
.endfor

ctype_test: ctype_test.c ${CTYPE_HOST_OBJS}
	${HOSTCC} ${HOST_FLAGS} -Ictype_tree ${CTYPE_FLAGS} -o $@ \
	    ctype_test.c ${CTYPE_HOST_OBJS}

ctype_test_arm: ctype_test.c ${CTYPE_ARM_OBJS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -Ictype_tree ${CTYPE_FLAGS} -o $@ \
	    ctype_test.c ${CTYPE_ARM_OBJS} ${QEMU_SUPPORT} ${QEMU_ARM_LIBS}

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
#   mbtowc_sign_extend    mbtowc widens the byte without unsigned char
#   wctomb_signed_limit   wctomb rejects the values above SCHAR_MAX
#   wcstombs_no_range     wcstombs stores a value above UCHAR_MAX
#   wcstou64_narrowed     the wide scan reads each element as unsigned char
#   wcstoimax_no_min      the negative limit loses INTMAX_MIN's extra value
#   imaxdiv_rem_sign      the remainder takes the denominator's sign
#   imaxdiv_short_loop    the division stops one quotient bit early
#   toupper_raw_offset    toupper subtracts the case offset from every value
#   isblank_no_tab        isblank accepts the space alone
#   ctype_space_class     the table marks the space as not printable
#   ctype_double_eval     isalnum is a macro of two tests on its argument
# The 64-bit scan is the template strtou64_scan.h, which strtoull.c and
# wcstoumax.c reach through a quoted include.  A scan mutant is the mutated
# template in a directory named ahead of ${STDLIB_DIR} on the include path,
# beside an unchanged copy of the instantiating source.
strtoull_no_overflow.mutant.c: ${STDLIB_DIR}/strtoull.c \
    ${STDLIB_DIR}/strtou64_scan.h
	@rm -rf strtoull_no_overflow.mutant && mkdir strtoull_no_overflow.mutant
	${MUTATE} ${STDLIB_DIR}/strtou64_scan.h \
	    strtoull_no_overflow.mutant/strtou64_scan.h \
	    "result.overflow = 1;" "result.overflow = 0;"
	cp ${STDLIB_DIR}/strtoull.c $@
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
toupper_raw_offset.mutant.c: ${GEN_DIR}/toupper.c
	${MUTATE} ${GEN_DIR}/toupper.c $@ \
	    "c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c" "c - 'a' + 'A'"
isblank_no_tab.mutant.c: ${GEN_DIR}/isblank.c
	${MUTATE} ${GEN_DIR}/isblank.c $@ "c == ' ' || c == '\t'" "c == ' '"
ctype_space_class.mutant.c: ${GEN_DIR}/ctype_.c
	${MUTATE} ${GEN_DIR}/ctype_.c $@ "_S|_B," "_S,"
ctype_double_eval.mutant/ctype.h: ${TOPSRC}/include/ctype.h
	@mkdir -p ctype_double_eval.mutant
	${MUTATE} ${TOPSRC}/include/ctype.h $@ \
	    "((_ctype_+1)[(int)(c)]&(_U|_L|_N))" "(isalpha(c) || isdigit(c))"
exit_runs_handlers.mutant.c: ${STDLIB_DIR}/_Exit.c
	${MUTATE} ${STDLIB_DIR}/_Exit.c $@ "	_exit(status);" "	exit(status);"
mbtowc_sign_extend.mutant.c: ${STDLIB_DIR}/mbtowc.c
	${MUTATE} ${STDLIB_DIR}/mbtowc.c $@ \
	    "(wchar_t)(unsigned char)*s" "(wchar_t)*s"
wctomb_signed_limit.mutant.c: ${STDLIB_DIR}/wctomb.c
	${MUTATE} ${STDLIB_DIR}/wctomb.c $@ "> UCHAR_MAX" "> SCHAR_MAX"
wcstombs_no_range.mutant.c: ${STDLIB_DIR}/wcstombs.c
	${MUTATE} ${STDLIB_DIR}/wcstombs.c $@ \
	    "(unsigned long)pwcs[count] > UCHAR_MAX" "0"
wcstoimax_no_min.mutant.c: ${STDLIB_DIR}/wcstoimax.c
	${MUTATE} ${STDLIB_DIR}/wcstoimax.c $@ \
	    "(unsigned long long)INTMAX_MAX + result.negative" \
	    "(unsigned long long)INTMAX_MAX"
wcstou64_narrowed.mutant.c: ${STDLIB_DIR}/wcstoumax.c \
    ${STDLIB_DIR}/strtou64_scan.h
	@rm -rf wcstou64_narrowed.mutant && mkdir wcstou64_narrowed.mutant
	${MUTATE} ${STDLIB_DIR}/strtou64_scan.h \
	    wcstou64_narrowed.mutant/strtou64_scan.h \
	    "	STRTOU64_UNIT character;" "	unsigned char character;"
	cp ${STDLIB_DIR}/wcstoumax.c $@

# The renames reach the mutated source alone.  Applied to strtoll_test.c
# they would rename its oracle calls as well, and the test would compare
# the mutant with itself.
.for mutant in strtoull_no_overflow strtoll_no_llong_min
${mutant}.mutant.o: ${mutant}.mutant.c ${STDLIB_DIR}/strtou64.h
	${HOSTCC} ${HOST_FLAGS} -fsigned-char ${STRTOLL_RENAMES} \
	    -I${mutant}.mutant -I${STDLIB_DIR} -c -o $@ ${mutant}.mutant.c
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
toupper_raw_offset.member=	toupper
isblank_no_tab.member=		isblank
ctype_space_class.member=	ctype_
.for mutant in toupper_raw_offset isblank_no_tab ctype_space_class
${mutant}.mutant.o: ${mutant}.mutant.c ctype_tree/ctype.h
	${HOSTCC} ${HOST_FLAGS} -Ictype_tree ${CTYPE_FLAGS} -c -o $@ \
	    ${mutant}.mutant.c
${mutant}.mutant.test: ${mutant}.mutant.o ctype_test.c ${CTYPE_HOST_OBJS}
	${HOSTCC} ${HOST_FLAGS} -Ictype_tree ${CTYPE_FLAGS} -o $@ \
	    ctype_test.c ${mutant}.mutant.o \
	    ${CTYPE_HOST_OBJS:Nhost_ct_${${mutant}.member}.o}
.endfor
ctype_double_eval.mutant.test: ctype_double_eval.mutant/ctype.h \
    ctype_test.c ${CTYPE_HOST_OBJS}
	${HOSTCC} ${HOST_FLAGS} -Ictype_double_eval.mutant ${CTYPE_FLAGS} \
	    -o $@ ctype_test.c ${CTYPE_HOST_OBJS}
wide_member.mbtowc_sign_extend=		mbtowc
wide_member.wctomb_signed_limit=	wctomb
wide_member.wcstombs_no_range=		wcstombs
wide_member.wcstoimax_no_min=		wcstoimax
.for mutant in mbtowc_sign_extend wctomb_signed_limit wcstombs_no_range \
    wcstoimax_no_min
${mutant}.mutant.o: ${mutant}.mutant.c ${STDLIB_DIR}/strtou64.h
	${HOSTCC} ${HOST_FLAGS} -fsigned-char ${WIDE_RENAMES} -I${STDLIB_DIR} \
	    -c -o $@ ${mutant}.mutant.c
${mutant}.mutant.test: ${mutant}.mutant.o wide_test.c ${WIDE_HOST_OBJS}
	${HOSTCC} ${HOST_FLAGS} -o $@ wide_test.c ${mutant}.mutant.o \
	    ${WIDE_HOST_OBJS:Nhost_${wide_member.${mutant}}.o}
.endfor
wcstou64_narrowed.mutant.o: wcstou64_narrowed.mutant.c
	${HOSTCC} ${HOST_FLAGS} -fsigned-char ${WIDE_RENAMES} \
	    -Iwcstou64_narrowed.mutant -I${STDLIB_DIR} -c -o $@ \
	    wcstou64_narrowed.mutant.c
wcstou64_narrowed.mutant.test: wcstou64_narrowed.mutant.o wide_test.c \
    ${WIDE_HOST_OBJS}
	${HOSTCC} ${HOST_FLAGS} -o $@ wide_test.c wcstou64_narrowed.mutant.o \
	    ${WIDE_HOST_OBJS:Nhost_wcstoumax.o}
exit_runs_handlers.mutant.o: exit_runs_handlers.mutant.c
	${HOSTCC} ${HOST_FLAGS} -D_Exit=db_Exit -c -o $@ \
	    exit_runs_handlers.mutant.c
exit_runs_handlers.mutant.test: exit_probe.c exit_runs_handlers.mutant.o
	${HOSTCC} ${HOST_FLAGS} -o $@ exit_probe.c exit_runs_handlers.mutant.o

RUN_MUTANTS=	strtoull_no_overflow strtoll_no_llong_min aligned_leak \
		aligned_half imaxdiv_rem_sign imaxdiv_short_loop \
		toupper_raw_offset isblank_no_tab ctype_space_class \
		ctype_double_eval mbtowc_sign_extend wctomb_signed_limit \
		wcstombs_no_range wcstou64_narrowed wcstoimax_no_min
HOST_TESTS=	strtoll_test aligned_alloc_test inttypes_test ctype_test \
		wide_test

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
mblen.imports=
mbtowc.imports=
wctomb.imports=
mbstowcs.imports=	strlen
wcstombs.imports=
wcstoumax.imports=	errno
wcstoimax.imports=	errno __discobsd_wcstou64
IMPORT_STDLIB=	strtoull strtoll atoll _Exit imaxdiv imaxabs strtoimax \
		strtoumax mblen mbtowc wctomb mbstowcs wcstombs wcstoumax \
		wcstoimax
isctype.imports=	_ctype_
isblank.imports=
tolower.imports=
toupper.imports=
isascii.imports=
IMPORT_GEN=	aligned_alloc isctype isblank tolower toupper isascii
doscan_llong.imports=	_filbuf ungetc
IMPORT_STDIO=	doscan_llong

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
.for member in ${IMPORT_STDIO}
	${TARGET_CC} ${IMPORT_FLAGS} -c -o import_${member}.o \
	    ${STDIO_DIR}/${member}.c
	sh ${.CURDIR}/check_imports.sh ${GCCPREFIX}-nm import_${member}.o \
	    ${${member}.imports}
.endfor
	@echo "c17 headers: ${IMPORT_STDLIB} ${IMPORT_GEN} ${IMPORT_STDIO} import only their listed symbols"

clean:
	rm -rf *.mutant *.log *.o *.mutant.c *.mutant.test ctype_tree \
	    ${HOST_TESTS} ${HOST_TESTS:S/$/_arm/} exit_probe

.PHONY: check-cross check-header-contracts check-header-mutants \
	check-imports check-null-contract check-host check-qemu clean \
	${HEADER_MUTANTS:S/$/.mutant/} ${NULL_MUTANTS:S/$/.mutant/}
