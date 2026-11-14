TOPSRC!= cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
ARM_COMPILER?= ${GCCPREFIX}-gcc
TARGET_MACHINE?= rp2040
PROG= libc_contracts_test
TARGET_DIR= ../../lib/libc/gen
STRING_DIR= ../../lib/libc/string
STDLIB_DIR= ../../lib/libc/stdlib
STDIO_DIR= ../../lib/libc/stdio
CFLAGS= -std=c99 -Wall -Wextra -Werror

# The arena allocator compiled from the tree over an sbrk() the test owns.
# The four allocation names move aside on the command line so the host's
# libc keeps its own. sbrk is renamed inside malloc_arena.c instead, after
# the host headers, because the host's unistd.h declares sbrk(intptr_t) and
# a command-line rename would turn that declaration into test_sbrk's, with
# the host's width and Darwin's deprecation attached. sbrk.c itself compiles
# against the tree's unistd.h, whose sbrk takes int, over the test's _brk().
# The kernel tier's old-style-definition excuse applies here too.
MALLOC_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror -Wno-deprecated-non-prototype
MALLOC_RENAMES=	-Dmalloc=db_malloc -Dfree=db_free -Drealloc=db_realloc \
		-Dcalloc=db_calloc -Dcfree=db_cfree
MALLOC_ARENA=	malloc_arena.c ${TARGET_DIR}/malloc.c
# sbrk.c starts the break at _end, the linker's end of bss on the target
# and on ELF hosts, which Mach-O never defines; the test supplies the
# initial break as well as the _brk() beneath it.
SBRK_RENAMES=	-Dsbrk=port_sbrk -Dbrk=port_brk -D_brk=test_brk -D_end=test_end
SBRK_SRC=	${TOPSRC}/lib/libc/arm/sys/sbrk.c
MALLOC_OBJS=	malloc_arena.o malloc_calloc.o malloc_sbrk.o
MALLOC_OBJS32=	malloc_arena32.o malloc_calloc32.o malloc_sbrk32.o

# The sort compiled from the tree under another name, host width and -m32,
# where size_t is the target's four bytes.
QSORT_RENAMES=	-Dqsort=db_qsort

RANDOM_RENAMES=	-Drandom=db_random -Dsrandom=db_srandom \
		-Dinitstate=db_initstate -Dsetstate=db_setstate \
		-Derrno=random_test_errno
RANDOM_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wstrict-prototypes \
		-Wmissing-prototypes -Wold-style-definition -nostdinc \
		-isystem ${TOPSRC}/include ${RANDOM_RENAMES}
RANDOM_TARGET_CFLAGS=	${RANDOM_CFLAGS} -fno-common -mcpu=cortex-m0plus \
		-mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft \
		-ffreestanding -Os
RANDOM_UBSAN=	-fsanitize=undefined -fno-sanitize-recover=all

# The target's size_t, int and pointers are four bytes; -m32 on an x86-64
# host is that width, so the same test runs there when the compiler can.
ILP32=		-m32
ILP32_CC= ${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk
include ${TOPSRC}/tools/qemu-arm.mk

check-host: ${PROG} header_contract.o check-ctime check-zone check-stdio-bounds \
	check-unistd-prototypes check-alarm-contract \
	check-waitpid-contract \
	check-mktime check-calendar64 check-difftime check-sleep \
	check-runtime-limits check-malloc check-random check-printf \
	check-printf-float check-scanf check-scanf-llong check-rwmode \
	check-ansi check-syslog \
	check-vis check-printf-g check-huge-val \
	check-qsort check-strtox check-string-security check-array-memory-line \
	check-getgrent check-bounded-string check-exit-lifecycle check-quick-exit \
	check-qsort-context check-dbm-open check-absolute-value
	${TEST_EXECUTION} run libc.base.native -- ./${PROG}

ARRAY_MEMORY_CFLAGS= -std=c17 -Wall -Wextra -Werror
ARRAY_MEMORY_RENAMES= -Dreallocarray=db_reallocarray \
	-Drealloc=array_test_realloc
MEMMEM_RENAMES= -Dmemmem=db_memmem
ARRAY_MEMORY_SAN_FLAGS= -fsanitize=address,undefined \
	-fno-sanitize-recover=all -fno-omit-frame-pointer
ARRAY_MEMORY_SAN_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native \
	${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS}
ARRAY_MEMORY_SAN32_OK!= sh ${TOPSRC}/tools/compiler-probe.sh ilp32 \
	${HOSTCC} ${ILP32} ${ARRAY_MEMORY_SAN_FLAGS}

BOUNDED_STRING_INPUTS= bounded_string.py bounded_string_test.c \
	bounded_string_overflow_test.c ${TARGET_DIR}/strnlen.c \
	${TARGET_DIR}/strndup.c ${TARGET_DIR}/stpcpy.c ${TARGET_DIR}/stpncpy.c

check-bounded-string: bounded_string_test
	${TEST_EXECUTION} run libc.bounded-string.native -- ./bounded_string_test matrix
.if ${ILP32_OK} == "yes"
	@${MAKE} bounded_string_test32
	${TEST_EXECUTION} run libc.bounded-string.ilp32 -- ./bounded_string_test32 matrix
.else
	${TEST_EXECUTION} skip libc.bounded-string.ilp32 "check-libc-bounded-string: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${ARRAY_MEMORY_SAN_OK} == "yes"
	@${MAKE} bounded_string_test_san
	${TEST_EXECUTION} run libc.bounded-string.sanitized -- ./bounded_string_test_san matrix
.else
	${TEST_EXECUTION} skip libc.bounded-string.sanitized "check-libc-bounded-string: sanitizer runtime unavailable"
.endif

bounded_string_test: ${BOUNDED_STRING_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/bounded_string.py ${TOPSRC} ${HOSTCC:Q} $@

bounded_string_test32: ${BOUNDED_STRING_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/bounded_string.py ${TOPSRC} ${HOSTCC:Q}\ -m32 $@

bounded_string_test_san: ${BOUNDED_STRING_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/bounded_string.py ${TOPSRC} "${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS}" $@

.PHONY: check-bounded-string

ABSOLUTE_VALUE_INPUTS= absolute_value.py absolute_value_test.c \
	${STDLIB_DIR}/abs.c ${STDLIB_DIR}/labs.c

check-absolute-value: absolute_value_test
	${TEST_EXECUTION} run libc.absolute-value.native -- ./absolute_value_test
.if ${ILP32_OK} == "yes"
	@${MAKE} absolute_value_test32
	${TEST_EXECUTION} run libc.absolute-value.ilp32 -- ./absolute_value_test32
.else
	${TEST_EXECUTION} skip libc.absolute-value.ilp32 "check-libc-absolute-value: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${ARRAY_MEMORY_SAN_OK} == "yes"
	@${MAKE} absolute_value_test_san
	${TEST_EXECUTION} run libc.absolute-value.sanitized -- ./absolute_value_test_san
.else
	${TEST_EXECUTION} skip libc.absolute-value.sanitized "check-libc-absolute-value: sanitizer runtime unavailable"
.endif

absolute_value_test: ${ABSOLUTE_VALUE_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/absolute_value.py ${TOPSRC} ${HOSTCC:Q} $@

absolute_value_test32: ${ABSOLUTE_VALUE_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/absolute_value.py ${TOPSRC} ${HOSTCC:Q}\ -m32 $@

absolute_value_test_san: ${ABSOLUTE_VALUE_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/absolute_value.py ${TOPSRC} "${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS}" $@

.PHONY: check-absolute-value

QSORT_CONTEXT_INPUTS= qsort_context.py qsort_context_test.c \
	${TARGET_DIR}/qsort.c ${TARGET_DIR}/qsort_r.c

check-qsort-context: qsort_context_test
	${TEST_EXECUTION} run libc.qsort-context.native -- ./qsort_context_test
.if ${ILP32_OK} == "yes"
	@${MAKE} qsort_context_test32
	${TEST_EXECUTION} run libc.qsort-context.ilp32 -- ./qsort_context_test32
.else
	${TEST_EXECUTION} skip libc.qsort-context.ilp32 "check-libc-qsort-context: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${ARRAY_MEMORY_SAN_OK} == "yes"
	@${MAKE} qsort_context_test_san
	${TEST_EXECUTION} run libc.qsort-context.sanitized -- ./qsort_context_test_san
.else
	${TEST_EXECUTION} skip libc.qsort-context.sanitized "check-libc-qsort-context: sanitizer runtime unavailable"
.endif

qsort_context_test: ${QSORT_CONTEXT_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/qsort_context.py ${TOPSRC} ${HOSTCC:Q} $@

qsort_context_test32: ${QSORT_CONTEXT_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/qsort_context.py ${TOPSRC} ${HOSTCC:Q}\ -m32 $@

qsort_context_test_san: ${QSORT_CONTEXT_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/qsort_context.py ${TOPSRC} "${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS}" $@

.PHONY: check-qsort-context

check-qsort-context-cross:
	${PYTHON} ${.CURDIR}/qsort_context_target.py ${TOPSRC} ${ARM_COMPILER} --host ${HOSTCC:Q}

DBM_OPEN_INPUTS= dbm_open.py dbm_open_test.c ${TOPSRC}/include/ndbm.h \
	${TARGET_DIR}/ndbm.c

check-dbm-open: dbm_open_test
	${TEST_EXECUTION} run libc.dbm-open.native -- ./dbm_open_test
.if ${ILP32_OK} == "yes"
	@${MAKE} dbm_open_test32
	${TEST_EXECUTION} run libc.dbm-open.ilp32 -- ./dbm_open_test32
.else
	${TEST_EXECUTION} skip libc.dbm-open.ilp32 "check-libc-dbm-open: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

dbm_open_test: ${DBM_OPEN_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/dbm_open.py ${TOPSRC} ${HOSTCC:Q} $@

dbm_open_test32: ${DBM_OPEN_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/dbm_open.py ${TOPSRC} ${HOSTCC:Q}\ -m32 $@

.PHONY: check-dbm-open

check-array-memory-line: reallocarray_contract_test memmem_contract_test \
	line_reading_contract_test
	${TEST_EXECUTION} run libc.reallocarray.native -- ./reallocarray_contract_test
	${TEST_EXECUTION} run libc.memmem.native -- ./memmem_contract_test
	${TEST_EXECUTION} run libc.line-reading.native -- ./line_reading_contract_test
.if ${ARRAY_MEMORY_SAN_OK} == "yes"
	@${MAKE} reallocarray_contract_test_san memmem_contract_test_san \
	    line_reading_contract_test_san
	${TEST_EXECUTION} run libc.reallocarray.sanitized -- \
	    ./reallocarray_contract_test_san
	${TEST_EXECUTION} run libc.memmem.sanitized -- ./memmem_contract_test_san
	${TEST_EXECUTION} run libc.line-reading.sanitized -- \
	    ./line_reading_contract_test_san
.else
	${TEST_EXECUTION} skip libc.reallocarray.sanitized \
	    "check-array-memory-line: ${HOSTCC} cannot execute native address/undefined sanitizers"
	${TEST_EXECUTION} skip libc.memmem.sanitized \
	    "check-array-memory-line: ${HOSTCC} cannot execute native address/undefined sanitizers"
	${TEST_EXECUTION} skip libc.line-reading.sanitized \
	    "check-array-memory-line: ${HOSTCC} cannot execute native address/undefined sanitizers"
.endif
.if ${ARRAY_MEMORY_SAN32_OK} == "yes"
	@${MAKE} reallocarray_contract_test32_san memmem_contract_test32_san \
	    line_reading_contract_test32_san
	${TEST_EXECUTION} run libc.reallocarray.sanitized-ilp32 -- \
	    ./reallocarray_contract_test32_san
	${TEST_EXECUTION} run libc.memmem.sanitized-ilp32 -- \
	    ./memmem_contract_test32_san
	${TEST_EXECUTION} run libc.line-reading.sanitized-ilp32 -- \
	    ./line_reading_contract_test32_san
.else
	${TEST_EXECUTION} skip libc.reallocarray.sanitized-ilp32 \
	    "check-array-memory-line: ${HOSTCC} cannot execute ILP32 address/undefined sanitizers"
	${TEST_EXECUTION} skip libc.memmem.sanitized-ilp32 \
	    "check-array-memory-line: ${HOSTCC} cannot execute ILP32 address/undefined sanitizers"
	${TEST_EXECUTION} skip libc.line-reading.sanitized-ilp32 \
	    "check-array-memory-line: ${HOSTCC} cannot execute ILP32 address/undefined sanitizers"
.endif

check-array-memory-line-cross:
	sh ${.CURDIR}/array_memory_archive.sh ${TOPSRC} ${ARM_COMPILER} \
	    ${TARGET_MACHINE} yes
	sh ${.CURDIR}/array_memory_consumer_maps.sh ${TOPSRC} ${MAKE} \
	    ${TARGET_MACHINE}

reallocarray_contract_test: reallocarray_contract_test.c array_reallocarray.o
	${HOSTCC} ${ARRAY_MEMORY_CFLAGS} -o $@ reallocarray_contract_test.c \
	    array_reallocarray.o

array_reallocarray.o: ${STDLIB_DIR}/reallocarray.c
	${HOSTCC} ${ARRAY_MEMORY_CFLAGS} ${ARRAY_MEMORY_RENAMES} -c -o $@ \
	    ${STDLIB_DIR}/reallocarray.c

memmem_contract_test: memmem_contract_test.c string_memmem.o
	${HOSTCC} ${ARRAY_MEMORY_CFLAGS} -o $@ memmem_contract_test.c string_memmem.o

line_reading_contract_test: ${TOPSRC}/usr.bin/textbox/tests/getline_test.c \
    ${STDIO_DIR}/getdelim.c ${STDIO_DIR}/getline.c
	${HOSTCC} ${ARRAY_MEMORY_CFLAGS} -o $@ \
	    ${TOPSRC}/usr.bin/textbox/tests/getline_test.c

string_memmem.o: ${STRING_DIR}/memmem.c
	${HOSTCC} ${ARRAY_MEMORY_CFLAGS} ${MEMMEM_RENAMES} -c -o $@ \
	    ${STRING_DIR}/memmem.c

reallocarray_contract_test_san: reallocarray_contract_test.c \
    array_reallocarray_san.o
	${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} -o $@ \
	    reallocarray_contract_test.c array_reallocarray_san.o

array_reallocarray_san.o: ${STDLIB_DIR}/reallocarray.c
	${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} \
	    ${ARRAY_MEMORY_RENAMES} -c -o $@ ${STDLIB_DIR}/reallocarray.c

memmem_contract_test_san: memmem_contract_test.c string_memmem_san.o
	${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} -o $@ \
	    memmem_contract_test.c string_memmem_san.o

string_memmem_san.o: ${STRING_DIR}/memmem.c
	${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} \
	    ${MEMMEM_RENAMES} -c -o $@ ${STRING_DIR}/memmem.c

line_reading_contract_test_san: \
    ${TOPSRC}/usr.bin/textbox/tests/getline_test.c \
    ${STDIO_DIR}/getdelim.c ${STDIO_DIR}/getline.c
	${HOSTCC} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} -o $@ \
	    ${TOPSRC}/usr.bin/textbox/tests/getline_test.c

reallocarray_contract_test32_san: reallocarray_contract_test.c \
    array_reallocarray32_san.o
	${HOSTCC} ${ILP32} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} \
	    -o $@ reallocarray_contract_test.c array_reallocarray32_san.o

array_reallocarray32_san.o: ${STDLIB_DIR}/reallocarray.c
	${HOSTCC} ${ILP32} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} \
	    ${ARRAY_MEMORY_RENAMES} -c -o $@ ${STDLIB_DIR}/reallocarray.c

memmem_contract_test32_san: memmem_contract_test.c string_memmem32_san.o
	${HOSTCC} ${ILP32} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} \
	    -o $@ memmem_contract_test.c string_memmem32_san.o

string_memmem32_san.o: ${STRING_DIR}/memmem.c
	${HOSTCC} ${ILP32} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} \
	    ${MEMMEM_RENAMES} -c -o $@ ${STRING_DIR}/memmem.c

line_reading_contract_test32_san: \
    ${TOPSRC}/usr.bin/textbox/tests/getline_test.c \
    ${STDIO_DIR}/getdelim.c ${STDIO_DIR}/getline.c
	${HOSTCC} ${ILP32} ${ARRAY_MEMORY_SAN_FLAGS} ${ARRAY_MEMORY_CFLAGS} \
	    -DHK_ILP32 -o $@ ${TOPSRC}/usr.bin/textbox/tests/getline_test.c

check: check-host check-aout

# The fixture checks exact function-pointer types and zero-argument calls.
# Negative compile probes reject the wrong arity for every migrated API.
check-unistd-prototypes:
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -Wstrict-prototypes \
	    -nostdinc -isystem ${TOPSRC}/include -fsyntax-only \
	    ${.CURDIR}/unistd_prototypes.c
	@set -e; for function in fork getegid geteuid getgid getlogin getpgrp getpid sync \
	    getppid getuid getusershell endusershell gethostid setusershell vfork; do \
	    if ${HOSTCC} -std=c17 -Wall -Wextra -Werror -Wstrict-prototypes \
		-nostdinc -isystem ${TOPSRC}/include -fsyntax-only \
		-DUNISTD_TEST_FUNCTION=$$function \
		${.CURDIR}/unistd_extra_argument.c >/dev/null 2>&1; then \
		echo "$$function accepted an extra argument" >&2; exit 1; \
	    fi; \
	done; \
	for function in access alarm lseek read sleep ttyname crypt getpass getwd psignal \
	    re_comp sethostid ualarm usleep; do \
	    if ${HOSTCC} -std=c17 -Wall -Wextra -Werror -Wstrict-prototypes \
		-nostdinc -isystem ${TOPSRC}/include -fsyntax-only \
		-DUNISTD_TEST_FUNCTION=$$function \
		${.CURDIR}/unistd_missing_argument.c >/dev/null 2>&1; then \
		echo "$$function accepted a missing argument" >&2; exit 1; \
	    fi; \
	done

ALARM_CFLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror \
		-Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes
ALARM_SOURCE_CFLAGS=	${ALARM_CFLAGS} -nostdinc \
		-isystem ${TOPSRC}/include -Dalarm=db_alarm \
		-Dsetitimer=test_setitimer
ALARM_TARGET_CFLAGS=	${ALARM_CFLAGS} -fno-common -mcpu=cortex-m0plus \
		-mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft \
		-ffreestanding -nostdinc -isystem ${TOPSRC}/include

check-alarm-contract: alarm_contract_test
	${TEST_EXECUTION} run libc.alarm.native -- ./alarm_contract_test
	${PYTHON} ${.CURDIR}/alarm_contract_mutate.py ${TOPSRC} \
	    ${HOSTCC:Q} ${.CURDIR}/alarm_contract_test.c

check-alarm-contract-cross:
	${ARM_COMPILER} ${ALARM_TARGET_CFLAGS} -fsyntax-only \
	    ${TARGET_DIR}/alarm.c

alarm_contract_test: alarm_contract_test.c alarm_contract.o
	${HOSTCC} ${ALARM_CFLAGS} -D_DEFAULT_SOURCE -o $@ \
	    alarm_contract_test.c alarm_contract.o

alarm_contract.o: ${TARGET_DIR}/alarm.c
	${HOSTCC} ${ALARM_SOURCE_CFLAGS} -c -o $@ ${TARGET_DIR}/alarm.c

WAITPID_CFLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror \
		-Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes
WAITPID_SOURCE_CFLAGS=	${WAITPID_CFLAGS} -nostdinc \
		-isystem ${TOPSRC}/include -Dwaitpid=db_waitpid \
		-Dwait4=test_wait4
WAITPID_TARGET_CFLAGS=	${WAITPID_CFLAGS} -fno-common -mcpu=cortex-m0plus \
		-mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft \
		-ffreestanding -nostdinc -isystem ${TOPSRC}/include

check-waitpid-contract: waitpid_contract_test
	${TEST_EXECUTION} run libc.waitpid.native -- ./waitpid_contract_test
	${PYTHON} ${.CURDIR}/waitpid_contract_mutate.py ${TOPSRC} \
	    ${HOSTCC:Q} ${.CURDIR}/waitpid_contract_test.c

check-waitpid-contract-cross:
	${ARM_COMPILER} ${WAITPID_TARGET_CFLAGS} -fsyntax-only \
	    ${TARGET_DIR}/waitpid.c

waitpid_contract_test: waitpid_contract_test.c waitpid_contract.o
	${HOSTCC} ${WAITPID_CFLAGS} -o $@ waitpid_contract_test.c \
	    waitpid_contract.o

waitpid_contract.o: ${TARGET_DIR}/waitpid.c
	${HOSTCC} ${WAITPID_SOURCE_CFLAGS} -c -o $@ ${TARGET_DIR}/waitpid.c

# <stdio.h> declares no gets() and neither libc builds one, so a caller fails
# at its implicit declaration rather than storing a whole line into a fixed
# buffer. stdio_bounds.sh carries the reasoning and its own controls.
check-stdio-bounds:
	sh ${.CURDIR}/stdio_bounds.sh ${TOPSRC} ${HOSTCC:Q} stdio_bounds_work

# lib/libc.a is the ELF archive the cross build leaves behind, which nm can
# read; the copy under distrib/obj is the a.out conversion and cannot be
# named by symbol.
check-stdio-bounds-cross:
	sh ${.CURDIR}/stdio_bounds.sh ${TOPSRC} ${HOSTCC:Q} stdio_bounds_work \
	    ${TOPSRC}/lib/libc.a ${GCCPREFIX}-nm

# The string-to-integer functions compiled from the tree under names that
# leave the host libc available to the test process.  -fsigned-char makes the
# source contract independent of the host default and exposes an uncast input
# byte before the ctype shim can index outside its 0..UCHAR_MAX domain.
STRTOX_DIR=	${TOPSRC}/lib/libc/stdlib
STRTOX_CFLAGS=	-std=c17 -Wall -Wextra -Werror -fsigned-char \
		-Istrtox_include
STRTOX_OBJS=	strtox_strtol.o strtox_strtoul.o
STRTOX_OBJS32=	strtox_strtol32.o strtox_strtoul32.o

# The bounded printf functions and their exact string-stream output path.
# Tree headers expose the FILE layout to the tree sources; the test itself
# uses write(2), so the host stdio ABI never crosses that boundary.
PRINTF_DIR=	${TOPSRC}/lib/libc/stdio
PRINTF_CFLAGS=	-std=c17 -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include -DNO_DOPRNT_FLOATFMT \
		-Derrno=printf_test_errno
PRINTF_OBJS=	printf_snprintf.o printf_vsnprintf.o printf_doprnt.o \
		printf_flsbuf.o printf_doprnt_llong.o
PRINTF_OBJS32=	printf_snprintf32.o printf_vsnprintf32.o printf_doprnt32.o \
		printf_flsbuf32.o printf_doprnt_llong32.o

# The formatted input scanner and the string-stream path sscanf builds for
# it. The entry points move aside so the host's own scanf stays out of the
# link and every call in the gate reaches the tree source. getc() expands to
# _filbuf on underflow, so that rename covers the macro as well.
SCANF_DIR=	${TOPSRC}/lib/libc/stdio
SCANF_STDLIB_DIR=${TOPSRC}/lib/libc/stdlib
SCANF_GEN_DIR=	${TOPSRC}/lib/libc/gen
SCANF_RENAMES=	-Dscanf=db_scanf -Dfscanf=db_fscanf -Dsscanf=db_sscanf \
		-Dvscanf=db_vscanf -Dvfscanf=db_vfscanf -Dvsscanf=db_vsscanf \
		-D_doscan=db_doscan -D__doscan_cvt=db_doscan_cvt \
		-D__doscan_ll=db_doscan_ll \
		-Dungetc=db_ungetc -D_filbuf=db_filbuf -Dstrtod=db_strtod \
		-Derrno=scanf_test_errno
SCANF_CFLAGS=	-std=c17 -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include ${SCANF_RENAMES}
SCANF_OBJS=	scanf_doscan.o scanf_doscan_float.o scanf_scanf.o \
		scanf_ungetc.o scanf_filbuf.o scanf_strtod.o scanf_ctype.o
SCANF_OBJS32=	scanf_doscan32.o scanf_doscan_float32.o scanf_scanf32.o \
		scanf_ungetc32.o scanf_filbuf32.o scanf_strtod32.o scanf_ctype32.o
SCANF_OBJS_ASAN=scanf_doscan_asan.o scanf_doscan_float_asan.o \
		scanf_scanf_asan.o scanf_ungetc_asan.o scanf_filbuf_asan.o \
		scanf_strtod_asan.o scanf_ctype_asan.o

# The 64-bit integer member joins the scanner objects above for
# scanf_llong_test.c. scanf_llong_absent_test is the same source linked
# without it, where the weak __doscan_ll resolves to a null address.
SCANF_LLONG_TESTS=	scanf_llong_test scanf_llong_test32 \
			scanf_llong_test_asan scanf_llong_absent_test \
			scanf_llong_absent_test32
SCANF_LLONG_OBJS=	scanf_doscan_llong.o scanf_doscan_llong32.o \
			scanf_doscan_llong_asan.o
# Every link of the scanner without doscan_llong.o leaves the weak
# __doscan_ll undefined. GNU ld resolves it to a null address. Darwin's ld64
# refuses it unless -U names it, after which the reference stays a weak
# import and dyld binds it to null at load, the condition _doscan tests.
SCANF_WEAK_LDFLAGS!= if [ "$$(uname -s)" = Darwin ] ; then \
			echo -Wl,-U,_db_doscan_ll ; fi

# The address sanitizer is what detects the defect class this scanner was
# rewritten to remove: a staging buffer overrun lands inside the scanner's
# own frame, where a guard byte around the caller's destination sees
# nothing. The V7 scanner aborts this run; the replacement passes it.
ASAN=		-fsanitize=address -fno-omit-frame-pointer
ASAN_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} ${ASAN}

# syslog compiled from the tree over deterministic transport, clock and errno
# shims.  The formatter objects stay exact tree objects from check-printf.
SYSLOG_DIR=	${TOPSRC}/lib/libc/gen
SYSLOG_RENAMES=	-Dsyslog=db_syslog -Dvsyslog=db_vsyslog \
		-Dopenlog=db_openlog -Dcloselog=db_closelog \
		-Dsetlogmask=db_setlogmask -Dopen=test_open \
		-Dwrite=test_write -Dwritev=test_writev -Dfcntl=test_fcntl \
		-Dclose=test_close -Dtime=test_time -Dlocaltime=test_localtime \
		-Dstrftime=test_strftime -Dgetpid=test_getpid \
		-Dvfork=test_vfork -Dwaitpid=test_waitpid -D_exit=test_exit \
		-Dstrerror=test_strerror -Derrno=syslog_test_errno
SYSLOG_CFLAGS=	-std=c17 -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include ${SYSLOG_RENAMES}
# doprnt.o refers to the weak __doprnt_ll; Mach-O resolves a weak
# reference only against a definition, so the member is linked here too.
SYSLOG_FORMAT_OBJS=	syslog_doprnt.o printf_flsbuf.o printf_doprnt_llong.o

.PHONY: check-stdio-bounds check-stdio-bounds-cross

check-syslog: syslog_contract_test
	${TEST_EXECUTION} run libc.syslog.native -- ./syslog_contract_test

syslog_contract_test: syslog_contract_test.c syslog_syslog.o ${SYSLOG_FORMAT_OBJS}
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -nostdinc \
	    -isystem ${TOPSRC}/include -o $@ syslog_contract_test.c \
	    syslog_syslog.o ${SYSLOG_FORMAT_OBJS}

syslog_doprnt.o: ${PRINTF_DIR}/doprnt.c
	${HOSTCC} ${PRINTF_CFLAGS:S/printf_test_errno/syslog_test_errno/} -c -o $@ ${PRINTF_DIR}/doprnt.c

syslog_syslog.o: ${SYSLOG_DIR}/syslog.c
	${HOSTCC} ${SYSLOG_CFLAGS} -c -o $@ ${SYSLOG_DIR}/syslog.c

# The byte encoder compiled from the tree under names isolated from host libc.
VIS_RENAMES=	-Dvis=db_vis -Dnvis=db_nvis \
		-Dstrvis=db_strvis -Dstrnvis=db_strnvis \
		-Dstrvisx=db_strvisx -Dstrnvisx=db_strnvisx \
		-Derrno=vis_test_errno
VIS_CFLAGS=	-std=c17 -Wall -Wextra -Werror -fsigned-char -nostdinc \
		-isystem ${TOPSRC}/include ${VIS_RENAMES}

STRING_SECURITY_RENAMES=	-Dexplicit_bzero=db_explicit_bzero \
	-Dtimingsafe_bcmp=db_timingsafe_bcmp
STRING_SECURITY_CFLAGS=	-std=c17 -Wall -Wextra -Werror \
	${STRING_SECURITY_RENAMES}

# The exact fixed-width calendar formatter, isolated from the host libc. The
# negative control accepts out-of-range fields the shipping source rejects.
ASCTIME_RENAMES=	-Dasctime=db_asctime -Derrno=asctime_test_errno
ASCTIME_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wformat=2 -nostdinc \
	-isystem ${TOPSRC}/include ${ASCTIME_RENAMES}

# The libc members tested here are compiled as libc builds them
# (lib/libc/Makefile), with _TIME_BITS defined as 32. The host compiler
# already defaults to that view (sys/sys/_time_t.h); the cross compiler
# defaults to 64 bits, where each public time name binds to its __NAME64
# symbol, which no -D rename reaches and the test link does not define.
NARROW_TIME=	-D_TIME_BITS=32
CTIME_RENAMES=	-Dctime=db_ctime -Dlocaltime=db_localtime \
	-Dgmtime=db_gmtime -Dtzset=db_tzset -Dtzname=db_tzname \
	-Dtztab=db_tztab
CTIME_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
	-ffunction-sections -fdata-sections -nostdinc \
	-isystem ${TOPSRC}/include ${CTIME_RENAMES} ${NARROW_TIME}
CTIME_TARGET_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wformat=2 \
	-Wformat-overflow=2 -Wold-style-definition -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -ffreestanding -nostdinc -I${TOPSRC}/include -Os \
	${NARROW_TIME}
CTIME_TARGET_DIAGNOSTIC_CFLAGS=	${CTIME_TARGET_CFLAGS} \
	-Dsnprintf=__builtin_snprintf
CTIME_TARGET_BAD_CFLAGS=	${CTIME_TARGET_CFLAGS} \
	-Dsprintf=__builtin_sprintf
# The zone file reader, which is the one part of libc that decodes bytes a
# caller names: TZ may give an absolute path, so every count in the file
# bounds a walk over a fixed-size array here.
ZONE_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
	-ffunction-sections -fdata-sections -nostdinc \
	-isystem ${TOPSRC}/include ${CTIME_RENAMES} ${NARROW_TIME}
ZONE_TZ_WARN=	-Wno-old-style-definition -Wno-deprecated-non-prototype

# mktime inverts ctime.c's localtime, so the two compile and link together
# with one set of renames: mktime.c's own call to localtime moves with it.
# The zone case is the only one that exercises mktime's offset search, and it
# needs a zone to search, so this gate builds the reader in.
MKTIME_RENAMES=	-Dmktime=db_mktime -Dtimegm=db_timegm ${CTIME_RENAMES} \
		-DTZ_ZONEINFO
MKTIME_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
	-ffunction-sections -fdata-sections -nostdinc \
	-isystem ${TOPSRC}/include ${MKTIME_RENAMES} ${NARROW_TIME}

RUNTIME_LIMITS_RENAMES=	-Dsysconf=db_sysconf -Dconfstr=db_confstr \
	-Derrno=runtime_limits_errno
RUNTIME_LIMITS_CFLAGS=	-std=c17 -Wall -Wextra -Werror \
	-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
	-nostdinc -isystem ${TOPSRC}/include ${RUNTIME_LIMITS_RENAMES}
RUNTIME_LIMITS_TARGET_CFLAGS=	${RUNTIME_LIMITS_CFLAGS} -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -ffreestanding -Os
DIFFTIME_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wstrict-prototypes \
	-Wmissing-prototypes -Wold-style-definition -nostdinc \
	-isystem ${TOPSRC}/include -Ddifftime=db_difftime ${NARROW_TIME}
DIFFTIME_TARGET_CFLAGS=	${DIFFTIME_CFLAGS} -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -ffreestanding -Os
GC_SECTIONS!=	if [ "$$(uname -s)" = Darwin ] ; then echo -Wl,-dead_strip ; \
	else echo -Wl,--gc-sections ; fi

# The typecnt bound is a read past ttis[] whose effect otherwise depends on
# what happens to lie there, so the sanitizer run is what names it.
ZONE_SAN=	-fsanitize=address,undefined -fno-omit-frame-pointer \
		-fno-sanitize-recover=all
ZONE_SAN_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} ${ZONE_SAN}
# mktime's sums stay inside an int and a time_t by comparing first. A sum
# that leaves either wraps to a value the plain run can still accept or
# refuse for another reason, so the sanitized runs are what stop on one; the
# ILP32 run is the one where long and time_t are four bytes, as on the target.
MKTIME_SAN32_OK!= sh ${TOPSRC}/tools/compiler-probe.sh ilp32 ${HOSTCC} \
	${ILP32} ${ZONE_SAN}

check-zone: zone_contract_test zone_contract_test_fixed
	${TEST_EXECUTION} run libc.zone.native -- ./zone_contract_test
	${TEST_EXECUTION} run libc.zone-fixed.native -- ./zone_contract_test_fixed
.if ${ZONE_SAN_OK} == "yes"
	@${MAKE} zone_contract_test_san
	${TEST_EXECUTION} run libc.zone.sanitized -- ./zone_contract_test_san
.else
	${TEST_EXECUTION} skip libc.zone.sanitized "check-libc-zone: no sanitizer runtime"
.endif
.if ${ILP32_OK} == "yes"
	@${MAKE} zone_contract_test32
	${TEST_EXECUTION} run libc.zone.ilp32 -- ./zone_contract_test32
.else
	${TEST_EXECUTION} skip libc.zone.ilp32 "check-libc-zone: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

check-mktime: mktime_contract_test
	${TEST_EXECUTION} run libc.mktime.native -- ./mktime_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} mktime_contract_test32
	${TEST_EXECUTION} run libc.mktime.ilp32 -- ./mktime_contract_test32
.else
	${TEST_EXECUTION} skip libc.mktime.ilp32 "check-libc-mktime: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${ZONE_SAN_OK} == "yes"
	@${MAKE} mktime_contract_test_san
	${TEST_EXECUTION} run libc.mktime.sanitized -- ./mktime_contract_test_san
.else
	${TEST_EXECUTION} skip libc.mktime.sanitized "check-libc-mktime: no sanitizer runtime"
.endif
.if ${MKTIME_SAN32_OK} == "yes"
	@${MAKE} mktime_contract_test32_san
	${TEST_EXECUTION} run libc.mktime.sanitized-ilp32 -- ./mktime_contract_test32_san
.else
	${TEST_EXECUTION} skip libc.mktime.sanitized-ilp32 "check-libc-mktime: ${HOSTCC} cannot execute ILP32 address/undefined sanitizers"
.endif

# The calendar functions of the 64-bit view of time: ctime.c, mktime.c and
# difftime.c compiled with _TIME_BITS=64 beside the 32-bit tzset.c,
# timezone.c and asctime.c, run by tests/time64_calendar/calendar64_test.c
# against the fields its vectors.py derives with CPython's datetime. The
# 64-bit members bind to __NAME64 symbols, which no -D rename reaches; the
# renames keep the shared 32-bit members and errno apart from the host's,
# and tzset.c reads TZ and the kernel offset through that directory's
# zone_hooks.c. The ILP32 runs are the ones whose long is four bytes, as on
# the target; tests/time64_calendar runs the same test as Thumb-1.
CAL64_DIR=	${TOPSRC}/tests/time64_calendar
CAL64_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
	-nostdinc -isystem ${TOPSRC}/include -Dtzset=db_tzset \
	-Dtzname=db_tzname -Dtztab=db_tztab -Dasctime=db_asctime \
	-Derrno=calendar64_errno
CAL64_ZONE=	-Dgetenv=calendar_getenv -Dgettimeofday=calendar_zone_gettimeofday
CAL64_HEADERS=	${TARGET_DIR}/offtime.h ${TARGET_DIR}/tzstate.h \
	${TOPSRC}/include/time.h ${TOPSRC}/sys/sys/_time_t.h
CAL64_PROG.native=	calendar64_test
CAL64_PROG.ilp32=	calendar64_test32
CAL64_PROG.san=		calendar64_test_san
CAL64_PROG.ilp32san=	calendar64_test32_san
CAL64_FLAGS.native=
CAL64_FLAGS.ilp32=	${ILP32}
CAL64_FLAGS.san=	${ZONE_SAN}
CAL64_FLAGS.ilp32san=	${ILP32} ${ZONE_SAN}

check-calendar64: calendar64_test
	${TEST_EXECUTION} run libc.calendar64.native -- ./calendar64_test
.if ${ILP32_OK} == "yes"
	@${MAKE} calendar64_test32
	${TEST_EXECUTION} run libc.calendar64.ilp32 -- ./calendar64_test32
.else
	${TEST_EXECUTION} skip libc.calendar64.ilp32 "check-libc-calendar64: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${ZONE_SAN_OK} == "yes"
	@${MAKE} calendar64_test_san
	${TEST_EXECUTION} run libc.calendar64.sanitized -- ./calendar64_test_san
.else
	${TEST_EXECUTION} skip libc.calendar64.sanitized "check-libc-calendar64: no sanitizer runtime"
.endif
.if ${MKTIME_SAN32_OK} == "yes"
	@${MAKE} calendar64_test32_san
	${TEST_EXECUTION} run libc.calendar64.sanitized-ilp32 -- ./calendar64_test32_san
.else
	${TEST_EXECUTION} skip libc.calendar64.sanitized-ilp32 "check-libc-calendar64: ${HOSTCC} cannot execute ILP32 address/undefined sanitizers"
.endif

calendar64_vectors.h: ${CAL64_DIR}/vectors.py
	: "$${PYTHON:?set PYTHON to the intended interpreter}"
	"$${PYTHON}" ${CAL64_DIR}/vectors.py $@

.for variant in native ilp32 san ilp32san
CAL64_OBJS.${variant}=	cal64_${variant}_ctime64.o cal64_${variant}_mktime64.o \
	cal64_${variant}_difftime64.o cal64_${variant}_tzset.o \
	cal64_${variant}_timezone.o cal64_${variant}_asctime.o \
	cal64_${variant}_zone_hooks.o

.for member in ctime mktime difftime
cal64_${variant}_${member}64.o: ${TARGET_DIR}/${member}.c ${CAL64_HEADERS}
	${HOSTCC} ${CAL64_FLAGS.${variant}} ${CAL64_CFLAGS} -D_TIME_BITS=64 \
	    -c -o $@ ${TARGET_DIR}/${member}.c
.endfor

cal64_${variant}_tzset.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${CAL64_FLAGS.${variant}} ${CAL64_CFLAGS} ${CAL64_ZONE} \
	    ${NARROW_TIME} -c -o $@ ${TARGET_DIR}/tzset.c

cal64_${variant}_timezone.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${CAL64_FLAGS.${variant}} ${CAL64_CFLAGS} ${MKTIME_TZ_WARN} \
	    ${NARROW_TIME} -c -o $@ ${TARGET_DIR}/timezone.c

cal64_${variant}_asctime.o: ${TARGET_DIR}/asctime.c
	${HOSTCC} ${CAL64_FLAGS.${variant}} ${CAL64_CFLAGS} \
	    ${NARROW_TIME} -c -o $@ ${TARGET_DIR}/asctime.c

cal64_${variant}_zone_hooks.o: ${CAL64_DIR}/zone_hooks.c \
    ${CAL64_DIR}/zone_hooks.h
	${HOSTCC} ${CAL64_FLAGS.${variant}} ${CAL64_CFLAGS} -DDEFINE_ERRNO \
	    ${NARROW_TIME} -c -o $@ ${CAL64_DIR}/zone_hooks.c

${CAL64_PROG.${variant}}: ${CAL64_DIR}/calendar64_test.c calendar64_vectors.h \
    ${CAL64_DIR}/zone_hooks.h ${CAL64_OBJS.${variant}}
	${HOSTCC} ${CAL64_FLAGS.${variant}} ${CAL64_CFLAGS} -I. -o $@ \
	    ${CAL64_DIR}/calendar64_test.c ${CAL64_OBJS.${variant}}
.endfor

check-ctime: check-asctime ctime_contract_test
	${TEST_EXECUTION} run libc.ctime.native -- ./ctime_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} ctime_contract_test32
	${TEST_EXECUTION} run libc.ctime.ilp32 -- ./ctime_contract_test32
.else
	${TEST_EXECUTION} skip libc.ctime.ilp32 "check-libc-ctime: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

check-ctime-cross: asctime_target.o asctime_target_diagnostic.o ctime_target.o \
	tzset_target.o stdio_format_extensions.o
	@rm -f asctime_format_bad_target.o asctime_format_bad_target.log
	@if ${ARM_COMPILER} ${CTIME_TARGET_BAD_CFLAGS} -c \
	    -o asctime_format_bad_target.o asctime_format_bad.c \
	    2>asctime_format_bad_target.log; then \
		echo "ctime target format-overflow negative control passed" >&2; \
		rm -f asctime_format_bad_target.o asctime_format_bad_target.log; \
		exit 1; \
	fi
	@if ! grep -q -- 'format-overflow=' asctime_format_bad_target.log; then \
		cat asctime_format_bad_target.log >&2; \
		echo "ctime target negative control failed for an unexpected reason" >&2; \
		rm -f asctime_format_bad_target.o asctime_format_bad_target.log; \
		exit 1; \
	fi
	@rm -f asctime_format_bad_target.o asctime_format_bad_target.log
	@echo "ctime target builtin format-overflow negative control rejected"
	@echo "ctime Cortex-M0+ production and diagnostic C17 compiles passed"

check-runtime-limits: runtime_limits_test runtime_limits_bad_test
	${TEST_EXECUTION} run libc.runtime-limits.native -- ./runtime_limits_test
	${TEST_EXECUTION} run libc.runtime-limits.negative -- ./runtime_limits_bad_test
	${PYTHON} ${.CURDIR}/runtime_limits_mutate.py ${TOPSRC} ${HOSTCC:Q}
.if ${ILP32_OK} == "yes"
	@${MAKE} runtime_limits_test32
	${TEST_EXECUTION} run libc.runtime-limits.ilp32 -- ./runtime_limits_test32
.else
	${TEST_EXECUTION} skip libc.runtime-limits.ilp32 "check-libc-runtime-limits: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

check-runtime-limits-cross: runtime_sysconf_target.o runtime_confstr_target.o
	@test "$$(${GCCPREFIX}-nm -u runtime_sysconf_target.o | wc -l)" -eq 1
	@${GCCPREFIX}-nm -u runtime_sysconf_target.o | grep -q ' U runtime_limits_errno$$'
	@test "$$(${GCCPREFIX}-nm -u runtime_confstr_target.o | wc -l)" -eq 1
	@${GCCPREFIX}-nm -u runtime_confstr_target.o | grep -q ' U runtime_limits_errno$$'
	@echo "runtime limit objects use constant storage and import only errno"

runtime_limits_test: runtime_limits_contract_test.c runtime_sysconf.o \
	runtime_confstr.o
	${HOSTCC} ${RUNTIME_LIMITS_CFLAGS} -o $@ runtime_limits_contract_test.c \
	    runtime_sysconf.o runtime_confstr.o

runtime_limits_test32: runtime_limits_contract_test.c runtime_sysconf32.o \
	runtime_confstr32.o
	${HOSTCC} ${ILP32} ${RUNTIME_LIMITS_CFLAGS} -o $@ \
	    runtime_limits_contract_test.c \
	    runtime_sysconf32.o runtime_confstr32.o

runtime_limits_bad_test: runtime_limits_contract_test.c runtime_limits_bad.c
	${HOSTCC} ${RUNTIME_LIMITS_CFLAGS} -o $@ \
	    runtime_limits_contract_test.c runtime_limits_bad.c

runtime_sysconf.o: ${TARGET_DIR}/sysconf.c
	${HOSTCC} ${RUNTIME_LIMITS_CFLAGS} -c -o $@ ${TARGET_DIR}/sysconf.c

runtime_confstr.o: ${TARGET_DIR}/confstr.c
	${HOSTCC} ${RUNTIME_LIMITS_CFLAGS} -c -o $@ ${TARGET_DIR}/confstr.c

runtime_sysconf32.o: ${TARGET_DIR}/sysconf.c
	${HOSTCC} ${ILP32} ${RUNTIME_LIMITS_CFLAGS} -c -o $@ ${TARGET_DIR}/sysconf.c

runtime_confstr32.o: ${TARGET_DIR}/confstr.c
	${HOSTCC} ${ILP32} ${RUNTIME_LIMITS_CFLAGS} -c -o $@ ${TARGET_DIR}/confstr.c

runtime_sysconf_target.o: ${TARGET_DIR}/sysconf.c
	${ARM_COMPILER} ${RUNTIME_LIMITS_TARGET_CFLAGS} -c -o $@ \
	    ${TARGET_DIR}/sysconf.c

runtime_confstr_target.o: ${TARGET_DIR}/confstr.c
	${ARM_COMPILER} ${RUNTIME_LIMITS_TARGET_CFLAGS} -c -o $@ \
	    ${TARGET_DIR}/confstr.c

check-difftime: difftime_contract_test
	${TEST_EXECUTION} run libc.difftime.native -- ./difftime_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} difftime_contract_test32 difftime_bad_test32
	${TEST_EXECUTION} run libc.difftime.ilp32 -- ./difftime_contract_test32
	${TEST_EXECUTION} run libc.difftime.negative -- ./difftime_bad_test32
.else
	${TEST_EXECUTION} skip libc.difftime.ilp32 "check-libc-difftime: ${HOSTCC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip libc.difftime.negative "check-libc-difftime: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

# sleep() at the 32-bit view against two replayed clock readings
# (sleep_contract_test.c). The negative control compares the readings
# signed, which only a four-byte long exposes, so it runs as ILP32.
SLEEP_CFLAGS=	-std=c17 -Wall -Wextra -Werror -nostdinc \
	-isystem ${TOPSRC}/include -Dsleep=db_sleep \
	-Dgettimeofday=test_gettimeofday -Dselect=test_select ${NARROW_TIME}

check-sleep: sleep_contract_test
	${TEST_EXECUTION} run libc.sleep.native -- ./sleep_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} sleep_contract_test32 sleep_bad_test32
	${TEST_EXECUTION} run libc.sleep.ilp32 -- ./sleep_contract_test32
	${TEST_EXECUTION} run libc.sleep.negative -- ./sleep_bad_test32
.else
	${TEST_EXECUTION} skip libc.sleep.ilp32 "check-libc-sleep: ${HOSTCC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip libc.sleep.negative "check-libc-sleep: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

sleep_contract_test: sleep_contract_test.c ${TARGET_DIR}/sleep.c
	${HOSTCC} ${SLEEP_CFLAGS} -o $@ sleep_contract_test.c \
	    ${TARGET_DIR}/sleep.c

sleep_contract_test32: sleep_contract_test.c ${TARGET_DIR}/sleep.c
	${HOSTCC} ${ILP32} ${SLEEP_CFLAGS} -o $@ sleep_contract_test.c \
	    ${TARGET_DIR}/sleep.c

sleep_bad_test32: sleep_contract_test.c sleep_signed_bad.c
	${HOSTCC} ${ILP32} ${SLEEP_CFLAGS} -o $@ sleep_contract_test.c \
	    sleep_signed_bad.c

check-difftime-cross: difftime_target.o
	@test "$$(${GCCPREFIX}-nm -u difftime_target.o | wc -l)" -eq 2
	@${GCCPREFIX}-nm -u difftime_target.o | grep -q ' U __aeabi_ui2d$$'
	@${GCCPREFIX}-nm -u difftime_target.o | grep -q ' U __aeabi_dsub$$'
	@echo "difftime Cortex-M0+ object converts unsigned endpoints before ROM double subtraction"

difftime_contract_test: difftime_contract_test.c difftime_difftime.o
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -o $@ \
	    difftime_contract_test.c difftime_difftime.o

difftime_contract_test32: difftime_contract_test.c difftime_difftime32.o
	${HOSTCC} ${ILP32} -std=c17 -Wall -Wextra -Werror -o $@ \
	    difftime_contract_test.c difftime_difftime32.o

difftime_bad_test32: difftime_contract_test.c difftime_bad.c
	${HOSTCC} ${ILP32} -std=c17 -Wall -Wextra -Werror -o $@ \
	    difftime_contract_test.c difftime_bad.c

difftime_difftime.o: ${TARGET_DIR}/difftime.c
	${HOSTCC} ${DIFFTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/difftime.c

difftime_difftime32.o: ${TARGET_DIR}/difftime.c
	${HOSTCC} ${ILP32} ${DIFFTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/difftime.c

difftime_target.o: ${TARGET_DIR}/difftime.c
	${ARM_COMPILER} ${DIFFTIME_TARGET_CFLAGS} -c -o $@ ${TARGET_DIR}/difftime.c

check-asctime: asctime_contract_test asctime_contract_bad_test
	${TEST_EXECUTION} run libc.asctime.native -- ./asctime_contract_test
	${TEST_EXECUTION} run libc.asctime.negative -- ./asctime_contract_bad_test

asctime_contract_test: asctime_contract_test.c asctime_asctime.o
	${HOSTCC} ${ASCTIME_CFLAGS} -o $@ asctime_contract_test.c \
	    asctime_asctime.o

asctime_contract_bad_test: asctime_contract_test.c asctime_bad.o
	${HOSTCC} ${ASCTIME_CFLAGS} -o $@ asctime_contract_test.c asctime_bad.o

asctime_asctime.o: ${TARGET_DIR}/asctime.c
	${HOSTCC} ${ASCTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/asctime.c

asctime_bad.o: asctime_bad.c
	${HOSTCC} ${ASCTIME_CFLAGS} -c -o $@ asctime_bad.c

# The fields of the 32-bit view's counts by CPython's datetime, which the
# ctime and mktime gates check gmtime() and timegm() against.
calendar32_vectors.h: ${.CURDIR}/calendar32_vectors.py
	: "$${PYTHON:?set PYTHON to the intended interpreter}"
	"$${PYTHON}" ${.CURDIR}/calendar32_vectors.py $@

ctime_contract_test: ctime_contract_test.c calendar32_vectors.h ctime_ctime.o \
    ctime_tzset.o
	${HOSTCC} ${CTIME_CFLAGS} -I. ${GC_SECTIONS} -o $@ \
	    ctime_contract_test.c ctime_ctime.o ctime_tzset.o

ctime_contract_test32: ctime_contract_test.c calendar32_vectors.h \
    ctime_ctime32.o ctime_tzset32.o
	${HOSTCC} ${ILP32} ${CTIME_CFLAGS} -I. ${GC_SECTIONS} -o $@ \
	    ctime_contract_test.c ctime_ctime32.o ctime_tzset32.o

zone_contract_test: zone_contract_test.c zone_ctime.o zone_tzset.o \
    zone_timezone.o
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_ZI} ${GC_SECTIONS} -o $@ \
	    zone_contract_test.c zone_ctime.o zone_tzset.o zone_timezone.o

zone_contract_test32: zone_contract_test.c zone_ctime32.o zone_tzset32.o \
    zone_timezone32.o
	${HOSTCC} ${ILP32} ${ZONE_CFLAGS} ${ZONE_ZI} ${GC_SECTIONS} -o $@ \
	    zone_contract_test.c zone_ctime32.o zone_tzset32.o \
	    zone_timezone32.o

zone_contract_test_san: zone_contract_test.c zone_ctime_san.o \
    zone_tzset_san.o zone_timezone_san.o
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_ZI} ${ZONE_SAN} -o $@ \
	    zone_contract_test.c zone_ctime_san.o zone_tzset_san.o \
	    zone_timezone_san.o

zone_ctime_san.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_ZI} ${ZONE_SAN} -c -o $@ ${TARGET_DIR}/ctime.c

zone_tzset_san.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_ZI} ${ZONE_SAN} -c -o $@ ${TARGET_DIR}/tzset.c

zone_timezone_san.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_SAN} ${ZONE_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

# The zone reader is an option, so the gate measures both shapes: the one
# that reads a file and the one that answers from the kernel's single offset.
ZONE_ZI=	-DTZ_ZONEINFO

zone_contract_test_fixed: zone_contract_test.c zone_ctime_fixed.o \
    zone_tzset_fixed.o zone_timezone_fixed.o
	${HOSTCC} ${ZONE_CFLAGS} ${GC_SECTIONS} -o $@ \
	    zone_contract_test.c zone_ctime_fixed.o zone_tzset_fixed.o \
	    zone_timezone_fixed.o

zone_ctime_fixed.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ZONE_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

zone_tzset_fixed.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ZONE_CFLAGS} -c -o $@ ${TARGET_DIR}/tzset.c

zone_timezone_fixed.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

zone_ctime.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_ZI} -c -o $@ ${TARGET_DIR}/ctime.c

zone_tzset.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_ZI} -c -o $@ ${TARGET_DIR}/tzset.c

zone_ctime32.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ILP32} ${ZONE_CFLAGS} ${ZONE_ZI} -c -o $@ ${TARGET_DIR}/ctime.c

zone_tzset32.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ILP32} ${ZONE_CFLAGS} ${ZONE_ZI} -c -o $@ ${TARGET_DIR}/tzset.c

# tzset.c's tzsetkernel() names tztab(), which timezone.c defines and which
# the gate reaches whenever a file is refused and the fallback runs.
zone_timezone.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

zone_timezone32.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ILP32} ${ZONE_CFLAGS} ${ZONE_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

# tzset.c's tzsetkernel() names tztab(), which timezone.c defines, and the
# gate reaches it because no zone file is named and that is the path a bare
# environment takes.
mktime_contract_test: mktime_contract_test.c calendar32_vectors.h \
    mktime_mktime.o mktime_ctime.o mktime_tzset.o mktime_timezone.o
	${HOSTCC} ${MKTIME_CFLAGS} -I. ${GC_SECTIONS} -o $@ \
	    mktime_contract_test.c mktime_mktime.o mktime_ctime.o \
	    mktime_tzset.o mktime_timezone.o

mktime_contract_test32: mktime_contract_test.c calendar32_vectors.h \
    mktime_mktime32.o mktime_ctime32.o mktime_tzset32.o mktime_timezone32.o
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} -I. ${GC_SECTIONS} -o $@ \
	    mktime_contract_test.c mktime_mktime32.o mktime_ctime32.o \
	    mktime_tzset32.o mktime_timezone32.o

# timezone.c still defines its functions the old way, which this tier's set
# rejects and the arm-none-eabi gcc libc ships from does not.
MKTIME_TZ_WARN=	-Wno-old-style-definition -Wno-deprecated-non-prototype

mktime_timezone.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${MKTIME_CFLAGS} ${MKTIME_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

mktime_timezone32.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} ${MKTIME_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

mktime_contract_test_san: mktime_contract_test.c calendar32_vectors.h \
    mktime_mktime_san.o mktime_ctime_san.o mktime_tzset_san.o \
    mktime_timezone_san.o
	${HOSTCC} ${MKTIME_CFLAGS} -I. ${ZONE_SAN} -o $@ \
	    mktime_contract_test.c mktime_mktime_san.o mktime_ctime_san.o \
	    mktime_tzset_san.o mktime_timezone_san.o

mktime_contract_test32_san: mktime_contract_test.c calendar32_vectors.h \
    mktime_mktime32_san.o mktime_ctime32_san.o mktime_tzset32_san.o \
    mktime_timezone32_san.o
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} -I. ${ZONE_SAN} -o $@ \
	    mktime_contract_test.c mktime_mktime32_san.o \
	    mktime_ctime32_san.o mktime_tzset32_san.o mktime_timezone32_san.o

mktime_mktime_san.o: ${TARGET_DIR}/mktime.c ${TARGET_DIR}/offtime.h
	${HOSTCC} ${MKTIME_CFLAGS} ${ZONE_SAN} -c -o $@ ${TARGET_DIR}/mktime.c

mktime_mktime32_san.o: ${TARGET_DIR}/mktime.c ${TARGET_DIR}/offtime.h
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} ${ZONE_SAN} -c -o $@ \
	    ${TARGET_DIR}/mktime.c

mktime_ctime_san.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${MKTIME_CFLAGS} ${ZONE_SAN} -c -o $@ ${TARGET_DIR}/ctime.c

mktime_tzset_san.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${MKTIME_CFLAGS} ${ZONE_SAN} -c -o $@ ${TARGET_DIR}/tzset.c

mktime_ctime32_san.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} ${ZONE_SAN} -c -o $@ \
	    ${TARGET_DIR}/ctime.c

mktime_tzset32_san.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} ${ZONE_SAN} -c -o $@ \
	    ${TARGET_DIR}/tzset.c

mktime_timezone_san.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${MKTIME_CFLAGS} ${ZONE_SAN} ${MKTIME_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

mktime_timezone32_san.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} ${ZONE_SAN} ${MKTIME_TZ_WARN} \
	    -c -o $@ ${TARGET_DIR}/timezone.c

mktime_mktime.o: ${TARGET_DIR}/mktime.c ${TARGET_DIR}/offtime.h
	${HOSTCC} ${MKTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/mktime.c

mktime_mktime32.o: ${TARGET_DIR}/mktime.c ${TARGET_DIR}/offtime.h
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/mktime.c

mktime_ctime.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${MKTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

mktime_tzset.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${MKTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/tzset.c

mktime_ctime32.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

mktime_tzset32.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ILP32} ${MKTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/tzset.c

ctime_ctime.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${CTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

ctime_tzset.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${CTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/tzset.c

ctime_ctime32.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ILP32} ${CTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

ctime_tzset32.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${HOSTCC} ${ILP32} ${CTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/tzset.c

asctime_target.o: ${TARGET_DIR}/asctime.c
	${ARM_COMPILER} ${CTIME_TARGET_CFLAGS} -c -o $@ ${TARGET_DIR}/asctime.c

asctime_target_diagnostic.o: ${TARGET_DIR}/asctime.c
	${ARM_COMPILER} ${CTIME_TARGET_DIAGNOSTIC_CFLAGS} -c -o $@ \
	    ${TARGET_DIR}/asctime.c

ctime_target.o: ${TARGET_DIR}/ctime.c ${TARGET_DIR}/offtime.h \
    ${TARGET_DIR}/tzstate.h
	${ARM_COMPILER} ${CTIME_TARGET_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

tzset_target.o: ${TARGET_DIR}/tzset.c ${TARGET_DIR}/tzstate.h
	${ARM_COMPILER} ${CTIME_TARGET_CFLAGS} -c -o $@ ${TARGET_DIR}/tzset.c

stdio_format_extensions.o: stdio_format_extensions.c
	${ARM_COMPILER} ${CTIME_TARGET_CFLAGS} -c -o $@ \
	    stdio_format_extensions.c

check-string-security: string_security_test
	${TEST_EXECUTION} run libc.string-security.native -- ./string_security_test

string_security_test: string_security_test.c string_explicit_bzero.o \
	string_timingsafe_bcmp.o
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -o $@ \
	    string_security_test.c string_explicit_bzero.o \
	    string_timingsafe_bcmp.o

string_explicit_bzero.o: ${STRING_DIR}/explicit_bzero.c
	${HOSTCC} ${STRING_SECURITY_CFLAGS} -c -o $@ \
	    ${STRING_DIR}/explicit_bzero.c

string_timingsafe_bcmp.o: ${STRING_DIR}/timingsafe_bcmp.c
	${HOSTCC} ${STRING_SECURITY_CFLAGS} -c -o $@ \
	    ${STRING_DIR}/timingsafe_bcmp.c

check-string-security-cross: string_timingsafe_bcmp_target.o \
	string_timingsafe_bcmp_bad_target.o
	@if sh ./string_disassembly.sh ${GCCPREFIX}-objdump \
	    string_timingsafe_bcmp_bad_target.o >/dev/null 2>&1; then \
		echo "timingsafe_bcmp: early-return negative control passed" >&2; \
		exit 1; \
	fi
	@echo "timingsafe_bcmp disassembly negative control passed"
	sh ./string_disassembly.sh ${GCCPREFIX}-objdump \
	    string_timingsafe_bcmp_target.o

string_timingsafe_bcmp_target.o: ${STRING_DIR}/timingsafe_bcmp.c
	${ARM_COMPILER} -std=c17 -Wall -Wextra -Werror -fno-common \
	    -mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	    -mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os \
	    -ffreestanding -fno-builtin -c -o $@ \
	    ${STRING_DIR}/timingsafe_bcmp.c

string_timingsafe_bcmp_bad_target.o: timingsafe_bcmp_bad.c
	${ARM_COMPILER} -std=c17 -Wall -Wextra -Werror -fno-common \
	    -mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	    -mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os \
	    -ffreestanding -fno-builtin -c -o $@ timingsafe_bcmp_bad.c

check-vis: vis_contract_test
	${TEST_EXECUTION} run libc.vis.native -- ./vis_contract_test

# getgrent.c compiled from the tree under db_ names, with its malloc, free
# and fopen routed to the test. The mutation script repeats the contract
# against static-record, per-call, unchecked and released-record mutants
# and bounds the Cortex-M0+ object's bss, so each mutant fails an oracle.
GETGRENT_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror
GETGRENT_RENAMES=	-Dgetgrent=db_getgrent -Dsetgrent=db_setgrent \
		-Dendgrent=db_endgrent -Dmalloc=grent_test_malloc \
		-Dfree=grent_test_free -Dfopen=grent_test_fopen

check-getgrent: getgrent_contract_test
	${TEST_EXECUTION} run libc.getgrent.native -- ./getgrent_contract_test
	${PYTHON} ${.CURDIR}/getgrent_contract_mutate.py ${TOPSRC} \
	    ${HOSTCC:Q} - ${.CURDIR}/getgrent_contract_test.c

# The same oracle with the Cortex-M0+ bss bound, for tiers that carry the
# cross compiler.
check-getgrent-cross:
	${PYTHON} ${.CURDIR}/getgrent_contract_mutate.py ${TOPSRC} \
	    ${HOSTCC:Q} ${ARM_COMPILER:Q} ${.CURDIR}/getgrent_contract_test.c

getgrent_contract_test: getgrent_contract_test.c grent_getgrent.o
	${HOSTCC} ${GETGRENT_CFLAGS} -o $@ getgrent_contract_test.c \
	    grent_getgrent.o

grent_getgrent.o: ${TARGET_DIR}/getgrent.c
	${HOSTCC} ${GETGRENT_CFLAGS} ${GETGRENT_RENAMES} -c -o $@ \
	    ${TARGET_DIR}/getgrent.c

vis_contract_test: vis_contract_test.c vis_vis.o
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -fsigned-char -o $@ \
	    vis_contract_test.c vis_vis.o

vis_vis.o: ${TARGET_DIR}/vis.c
	${HOSTCC} ${VIS_CFLAGS} -c -o $@ ${TARGET_DIR}/vis.c

# The formatter with its float member linked, for %a and the decimal
# conversions; the host libm supplies modf.
PRINTF_FLOAT_CFLAGS=	-std=c17 -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include -Derrno=printf_test_errno \
		-DPRINTF_FLOAT_TEST
PRINTF_FLOAT_OBJS=	printf_snprintf.o printf_vsnprintf.o printf_flsbuf.o \
		printf_doprnt_fp.o printf_doprnt_float.o printf_doprnt_llong.o

check-printf-float: printf_float_contract_test
	${TEST_EXECUTION} run libc.printf-float.native -- ./printf_float_contract_test

printf_float_contract_test: printf_contract_test.c ${PRINTF_FLOAT_OBJS}
	${HOSTCC} ${PRINTF_FLOAT_CFLAGS} -o $@ printf_contract_test.c \
	    ${PRINTF_FLOAT_OBJS} -lm

printf_doprnt_fp.o: ${PRINTF_DIR}/doprnt.c
	${HOSTCC} ${PRINTF_FLOAT_CFLAGS} -c -o $@ ${PRINTF_DIR}/doprnt.c

printf_doprnt_float.o: ${PRINTF_DIR}/doprnt_float.c
	${HOSTCC} ${PRINTF_FLOAT_CFLAGS} -c -o $@ ${PRINTF_DIR}/doprnt_float.c

check-printf: printf_contract_test
	${TEST_EXECUTION} run libc.printf.native -- ./printf_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} printf_contract_test32
	${TEST_EXECUTION} run libc.printf.ilp32 -- ./printf_contract_test32
.else
	${TEST_EXECUTION} skip libc.printf.ilp32 "check-libc-printf: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

printf_contract_test: printf_contract_test.c ${PRINTF_OBJS}
	${HOSTCC} ${PRINTF_CFLAGS} -o $@ printf_contract_test.c ${PRINTF_OBJS}

printf_contract_test32: printf_contract_test.c ${PRINTF_OBJS32}
	${HOSTCC} ${ILP32} ${PRINTF_CFLAGS} -o $@ printf_contract_test.c ${PRINTF_OBJS32}

printf_snprintf.o: ${PRINTF_DIR}/snprintf.c
	${HOSTCC} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/snprintf.c

printf_vsnprintf.o: ${PRINTF_DIR}/vsnprintf.c
	${HOSTCC} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/vsnprintf.c

printf_doprnt.o: ${PRINTF_DIR}/doprnt.c
	${HOSTCC} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/doprnt.c

printf_doprnt_llong.o: ${PRINTF_DIR}/doprnt_llong.c
	${HOSTCC} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/doprnt_llong.c

printf_doprnt_llong32.o: ${PRINTF_DIR}/doprnt_llong.c
	${HOSTCC} ${ILP32} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/doprnt_llong.c

printf_flsbuf.o: ${PRINTF_DIR}/flsbuf.c
	${HOSTCC} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/flsbuf.c

printf_snprintf32.o: ${PRINTF_DIR}/snprintf.c
	${HOSTCC} ${ILP32} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/snprintf.c

printf_vsnprintf32.o: ${PRINTF_DIR}/vsnprintf.c
	${HOSTCC} ${ILP32} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/vsnprintf.c

printf_doprnt32.o: ${PRINTF_DIR}/doprnt.c
	${HOSTCC} ${ILP32} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/doprnt.c

printf_flsbuf32.o: ${PRINTF_DIR}/flsbuf.c
	${HOSTCC} ${ILP32} ${PRINTF_CFLAGS} -c -o $@ ${PRINTF_DIR}/flsbuf.c

check-scanf: scanf_contract_test
	${TEST_EXECUTION} run libc.scanf.native -- ./scanf_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} scanf_contract_test32
	${TEST_EXECUTION} run libc.scanf.ilp32 -- ./scanf_contract_test32
.else
	${TEST_EXECUTION} skip libc.scanf.ilp32 "check-libc-scanf: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${ASAN_OK} == "yes"
	@${MAKE} scanf_contract_test_asan
	${TEST_EXECUTION} run libc.scanf.sanitized -- ./scanf_contract_test_asan
.else
	${TEST_EXECUTION} skip libc.scanf.sanitized "check-libc-scanf: ${HOSTCC} has no address sanitizer"
.endif

check-scanf-llong: scanf_llong_test scanf_llong_absent_test
	${TEST_EXECUTION} run libc.scanf-llong.native -- ./scanf_llong_test
	${TEST_EXECUTION} run libc.scanf-llong.absent.native -- ./scanf_llong_absent_test
.if ${ILP32_OK} == "yes"
	@${MAKE} scanf_llong_test32 scanf_llong_absent_test32
	${TEST_EXECUTION} run libc.scanf-llong.ilp32 -- ./scanf_llong_test32
	${TEST_EXECUTION} run libc.scanf-llong.absent.ilp32 -- ./scanf_llong_absent_test32
.else
	${TEST_EXECUTION} skip libc.scanf-llong.ilp32 "check-libc-scanf-llong: ${HOSTCC} cannot compile and execute ILP32 binaries"
	${TEST_EXECUTION} skip libc.scanf-llong.absent.ilp32 "check-libc-scanf-llong: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${ASAN_OK} == "yes"
	@${MAKE} scanf_llong_test_asan
	${TEST_EXECUTION} run libc.scanf-llong.sanitized -- ./scanf_llong_test_asan
.else
	${TEST_EXECUTION} skip libc.scanf-llong.sanitized "check-libc-scanf-llong: ${HOSTCC} has no address sanitizer"
.endif

scanf_llong_test: scanf_llong_test.c ${SCANF_OBJS} scanf_doscan_llong.o
	${HOSTCC} ${SCANF_CFLAGS} -o $@ scanf_llong_test.c ${SCANF_OBJS} \
	    scanf_doscan_llong.o

scanf_llong_test32: scanf_llong_test.c ${SCANF_OBJS32} scanf_doscan_llong32.o
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -o $@ scanf_llong_test.c \
	    ${SCANF_OBJS32} scanf_doscan_llong32.o

scanf_llong_test_asan: scanf_llong_test.c ${SCANF_OBJS_ASAN} \
		scanf_doscan_llong_asan.o
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -o $@ scanf_llong_test.c \
	    ${SCANF_OBJS_ASAN} scanf_doscan_llong_asan.o

scanf_llong_absent_test: scanf_llong_test.c ${SCANF_OBJS}
	${HOSTCC} ${SCANF_CFLAGS} -DSCANF_LLONG_ABSENT -o $@ \
	    scanf_llong_test.c ${SCANF_OBJS} ${SCANF_WEAK_LDFLAGS}

scanf_llong_absent_test32: scanf_llong_test.c ${SCANF_OBJS32}
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -DSCANF_LLONG_ABSENT -o $@ \
	    scanf_llong_test.c ${SCANF_OBJS32} ${SCANF_WEAK_LDFLAGS}

scanf_doscan_llong.o: ${SCANF_DIR}/doscan_llong.c ${SCANF_DIR}/doscan.h
	${HOSTCC} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan_llong.c

scanf_doscan_llong32.o: ${SCANF_DIR}/doscan_llong.c ${SCANF_DIR}/doscan.h
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan_llong.c

scanf_doscan_llong_asan.o: ${SCANF_DIR}/doscan_llong.c ${SCANF_DIR}/doscan.h
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan_llong.c

scanf_contract_test: scanf_contract_test.c ${SCANF_OBJS}
	${HOSTCC} ${SCANF_CFLAGS} -o $@ scanf_contract_test.c ${SCANF_OBJS} ${SCANF_WEAK_LDFLAGS}

scanf_contract_test32: scanf_contract_test.c ${SCANF_OBJS32}
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -o $@ scanf_contract_test.c \
	    ${SCANF_OBJS32} ${SCANF_WEAK_LDFLAGS}

scanf_contract_test_asan: scanf_contract_test.c ${SCANF_OBJS_ASAN}
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -o $@ scanf_contract_test.c \
	    ${SCANF_OBJS_ASAN} ${SCANF_WEAK_LDFLAGS}

scanf_doscan.o: ${SCANF_DIR}/doscan.c ${SCANF_DIR}/doscan.h
	${HOSTCC} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan.c

scanf_doscan_float.o: ${SCANF_DIR}/doscan_float.c
	${HOSTCC} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan_float.c

scanf_scanf.o: ${SCANF_DIR}/scanf.c
	${HOSTCC} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/scanf.c

scanf_ungetc.o: ${SCANF_DIR}/ungetc.c
	${HOSTCC} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/ungetc.c

scanf_filbuf.o: ${SCANF_DIR}/filbuf.c
	${HOSTCC} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/filbuf.c

scanf_strtod.o: ${SCANF_STDLIB_DIR}/strtod.c
	${HOSTCC} ${SCANF_CFLAGS} -c -o $@ ${SCANF_STDLIB_DIR}/strtod.c

scanf_ctype.o: ${SCANF_GEN_DIR}/ctype_.c
	${HOSTCC} ${SCANF_CFLAGS} -c -o $@ ${SCANF_GEN_DIR}/ctype_.c

scanf_doscan32.o: ${SCANF_DIR}/doscan.c ${SCANF_DIR}/doscan.h
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan.c

scanf_doscan_float32.o: ${SCANF_DIR}/doscan_float.c
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan_float.c

scanf_scanf32.o: ${SCANF_DIR}/scanf.c
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/scanf.c

scanf_ungetc32.o: ${SCANF_DIR}/ungetc.c
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/ungetc.c

scanf_filbuf32.o: ${SCANF_DIR}/filbuf.c
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/filbuf.c

scanf_strtod32.o: ${SCANF_STDLIB_DIR}/strtod.c
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -c -o $@ ${SCANF_STDLIB_DIR}/strtod.c

scanf_ctype32.o: ${SCANF_GEN_DIR}/ctype_.c
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -c -o $@ ${SCANF_GEN_DIR}/ctype_.c

scanf_doscan_asan.o: ${SCANF_DIR}/doscan.c ${SCANF_DIR}/doscan.h
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan.c

scanf_doscan_float_asan.o: ${SCANF_DIR}/doscan_float.c
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/doscan_float.c

scanf_scanf_asan.o: ${SCANF_DIR}/scanf.c
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/scanf.c

scanf_ungetc_asan.o: ${SCANF_DIR}/ungetc.c
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/ungetc.c

scanf_filbuf_asan.o: ${SCANF_DIR}/filbuf.c
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_DIR}/filbuf.c

scanf_strtod_asan.o: ${SCANF_STDLIB_DIR}/strtod.c
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_STDLIB_DIR}/strtod.c

scanf_ctype_asan.o: ${SCANF_GEN_DIR}/ctype_.c
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_GEN_DIR}/ctype_.c

# The r+ mode switch in _filbuf and _flsbuf over a host temporary file.
# fflush stays the tree's so the free-count discipline under test is the
# one that ships; the buffer members are renamed so getc and putc reach them.
RWMODE_RENAMES=	-D_filbuf=db_filbuf -D_flsbuf=db_flsbuf -Dfflush=db_fflush \
		-Dfclose=db_fclose -Dungetc=db_ungetc -Dfstat=test_fstat \
		-Derrno=rwmode_test_errno
RWMODE_CFLAGS=	-std=c17 -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include ${RWMODE_RENAMES}
RWMODE_OBJS=	rwmode_filbuf.o rwmode_flsbuf.o rwmode_ungetc.o
RWMODE_OBJS32=	rwmode_filbuf32.o rwmode_flsbuf32.o rwmode_ungetc32.o

check-rwmode: rwmode_contract_test
	${TEST_EXECUTION} run libc.rwmode.native -- ./rwmode_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} rwmode_contract_test32
	${TEST_EXECUTION} run libc.rwmode.ilp32 -- ./rwmode_contract_test32
.else
	${TEST_EXECUTION} skip libc.rwmode.ilp32 "check-libc-rwmode: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

rwmode_contract_test: rwmode_contract_test.c ${RWMODE_OBJS}
	${HOSTCC} ${RWMODE_CFLAGS} -o $@ rwmode_contract_test.c ${RWMODE_OBJS}

rwmode_contract_test32: rwmode_contract_test.c ${RWMODE_OBJS32}
	${HOSTCC} ${ILP32} ${RWMODE_CFLAGS} -o $@ rwmode_contract_test.c ${RWMODE_OBJS32}

rwmode_filbuf.o: ${PRINTF_DIR}/filbuf.c
	${HOSTCC} ${RWMODE_CFLAGS} -c -o $@ ${PRINTF_DIR}/filbuf.c

rwmode_flsbuf.o: ${PRINTF_DIR}/flsbuf.c
	${HOSTCC} ${RWMODE_CFLAGS} -c -o $@ ${PRINTF_DIR}/flsbuf.c

rwmode_ungetc.o: ${PRINTF_DIR}/ungetc.c
	${HOSTCC} ${RWMODE_CFLAGS} -c -o $@ ${PRINTF_DIR}/ungetc.c

rwmode_ungetc32.o: ${PRINTF_DIR}/ungetc.c
	${HOSTCC} ${ILP32} ${RWMODE_CFLAGS} -c -o $@ ${PRINTF_DIR}/ungetc.c

rwmode_filbuf32.o: ${PRINTF_DIR}/filbuf.c
	${HOSTCC} ${ILP32} ${RWMODE_CFLAGS} -c -o $@ ${PRINTF_DIR}/filbuf.c

rwmode_flsbuf32.o: ${PRINTF_DIR}/flsbuf.c
	${HOSTCC} ${ILP32} ${RWMODE_CFLAGS} -c -o $@ ${PRINTF_DIR}/flsbuf.c

check-exit-lifecycle: exit_lifecycle_test
	${TEST_EXECUTION} run libc.exit.native -- ./exit_lifecycle_test nested
.if ${ILP32_OK} == "yes"
	@${MAKE} exit_lifecycle_test32
	${TEST_EXECUTION} run libc.exit.ilp32 -- ./exit_lifecycle_test32 nested
.else
	${TEST_EXECUTION} skip libc.exit.ilp32 "check-libc-exit-lifecycle: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

EXIT_LIFECYCLE_INPUTS= exit_lifecycle.py exit_lifecycle_test.c \
	${STDIO_DIR}/exit.c ${STDIO_DIR}/atexit.c ${STDIO_DIR}/atexit.h

exit_lifecycle_test: ${EXIT_LIFECYCLE_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/exit_lifecycle.py ${TOPSRC} ${HOSTCC:Q} $@

exit_lifecycle_test32: ${EXIT_LIFECYCLE_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/exit_lifecycle.py ${TOPSRC} ${HOSTCC:Q}\ -m32 $@

check-quick-exit: quick_exit_test
	${TEST_EXECUTION} run libc.quick-exit.native -- ./quick_exit_test nested
.if ${ILP32_OK} == "yes"
	@${MAKE} quick_exit_test32
	${TEST_EXECUTION} run libc.quick-exit.ilp32 -- ./quick_exit_test32 nested
.else
	${TEST_EXECUTION} skip libc.quick-exit.ilp32 "check-libc-quick-exit: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

check-quick-exit-cross:
	${PYTHON} ${.CURDIR}/quick_exit_target.py ${TOPSRC} ${ARM_COMPILER} --host ${HOSTCC:Q}

QUICK_EXIT_INPUTS= quick_exit.py quick_exit_test.c quick_exit_termination_test.c \
	${STDIO_DIR}/exit.c ${STDIO_DIR}/atexit.c ${STDIO_DIR}/atexit.h \
	${STDLIB_DIR}/at_quick_exit.c ${STDLIB_DIR}/quick_exit.c \
	${STDLIB_DIR}/quick_exit.h ${STDLIB_DIR}/iso_exit.c

quick_exit_test: ${QUICK_EXIT_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/quick_exit.py ${TOPSRC} ${HOSTCC:Q} $@

quick_exit_test32: ${QUICK_EXIT_INPUTS} .PHONY
	${PYTHON} ${.CURDIR}/quick_exit.py ${TOPSRC} ${HOSTCC:Q}\ -m32 $@

check-strtox: strtox_test
	${TEST_EXECUTION} run libc.strtox.native -- ./strtox_test
.if ${ILP32_OK} == "yes"
	@${MAKE} strtox_test32
	${TEST_EXECUTION} run libc.strtox.ilp32 -- ./strtox_test32
.else
	${TEST_EXECUTION} skip libc.strtox.ilp32 "check-libc-strtox: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

strtox_test: strtox_test.c ${STRTOX_OBJS}
	${HOSTCC} ${STRTOX_CFLAGS} -o $@ strtox_test.c ${STRTOX_OBJS}

strtox_test32: strtox_test.c ${STRTOX_OBJS32}
	${HOSTCC} ${ILP32} ${STRTOX_CFLAGS} -o $@ strtox_test.c ${STRTOX_OBJS32}

strtox_strtol.o: ${STRTOX_DIR}/strtol.c
	${HOSTCC} ${STRTOX_CFLAGS} -Dstrtol=db_strtol -c -o $@ ${STRTOX_DIR}/strtol.c

strtox_strtoul.o: ${STRTOX_DIR}/strtoul.c
	${HOSTCC} ${STRTOX_CFLAGS} -Dstrtoul=db_strtoul -c -o $@ ${STRTOX_DIR}/strtoul.c

strtox_strtol32.o: ${STRTOX_DIR}/strtol.c
	${HOSTCC} ${ILP32} ${STRTOX_CFLAGS} -Dstrtol=db_strtol -c -o $@ ${STRTOX_DIR}/strtol.c

strtox_strtoul32.o: ${STRTOX_DIR}/strtoul.c
	${HOSTCC} ${ILP32} ${STRTOX_CFLAGS} -Dstrtoul=db_strtoul -c -o $@ ${STRTOX_DIR}/strtoul.c

# The host allocator gate, which the root Makefile reaches as
# check-libc-malloc.
check-malloc: malloc_test
	${TEST_EXECUTION} run libc.malloc.native -- ./malloc_test
.if ${ILP32_OK} == "yes"
	@${MAKE} malloc_test32
	${TEST_EXECUTION} run libc.malloc.ilp32 -- ./malloc_test32
.else
	${TEST_EXECUTION} skip libc.malloc.ilp32 "check-libc-malloc: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

# The host sort gate, which the root Makefile reaches as check-libc-qsort.
check-qsort: qsort_test
	${TEST_EXECUTION} run libc.qsort.native -- ./qsort_test
.if ${ILP32_OK} == "yes"
	@${MAKE} qsort_test32
	${TEST_EXECUTION} run libc.qsort.ilp32 -- ./qsort_test32
.else
	${TEST_EXECUTION} skip libc.qsort.ilp32 "check-libc-qsort: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

qsort_test: qsort_test.c qsort_sort.o
	${HOSTCC} ${MALLOC_CFLAGS} -o $@ qsort_test.c qsort_sort.o

qsort_test32: qsort_test.c qsort_sort32.o
	${HOSTCC} ${ILP32} ${MALLOC_CFLAGS} -o $@ qsort_test.c qsort_sort32.o

qsort_sort.o: ${TARGET_DIR}/qsort.c
	${HOSTCC} ${MALLOC_CFLAGS} ${QSORT_RENAMES} -c -o $@ ${TARGET_DIR}/qsort.c

qsort_sort32.o: ${TARGET_DIR}/qsort.c
	${HOSTCC} ${ILP32} ${MALLOC_CFLAGS} ${QSORT_RENAMES} -c -o $@ ${TARGET_DIR}/qsort.c

check-random: random_contract_test random_contract_ubsan \
	random_metadata_test random_metadata_bad_test
	${TEST_EXECUTION} run libc.random.native -- ./random_contract_test
	${TEST_EXECUTION} run libc.random.ubsan -- ./random_contract_ubsan
	${TEST_EXECUTION} run libc.random-metadata.native -- ./random_metadata_test
	${TEST_EXECUTION} run libc.random-metadata.negative -- ./random_metadata_bad_test
.if ${ILP32_OK} == "yes"
	@${MAKE} random_contract_test32
	${TEST_EXECUTION} run libc.random.ilp32 -- ./random_contract_test32
.else
	${TEST_EXECUTION} skip libc.random.ilp32 "check-libc-random: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif

check-random-cross: random_target.o
	@if ${GCCPREFIX}-nm -u random_target.o | \
	    grep -Eq '__aeabi_.*div|fprintf|_iob'; then \
		${GCCPREFIX}-nm -u random_target.o >&2; \
		echo "random target object imports division or stdio" >&2; \
		exit 1; \
	fi
	@echo "random Cortex-M0+ object uses allocation-free local state only"

random_contract_test: random_contract_test.c random_random.o
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -o $@ \
	    random_contract_test.c random_random.o

random_contract_test32: random_contract_test.c random_random32.o
	${HOSTCC} ${ILP32} -std=c17 -Wall -Wextra -Werror -o $@ \
	    random_contract_test.c random_random32.o

random_contract_ubsan: random_contract_test.c random_random_ubsan.o
	${HOSTCC} ${RANDOM_UBSAN} -std=c17 -Wall -Wextra -Werror -o $@ \
	    random_contract_test.c random_random_ubsan.o

random_metadata_test: random_metadata_test.c random_random.o
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -o $@ \
	    random_metadata_test.c random_random.o

random_metadata_bad_test: random_metadata_test.c random_metadata_bad.c
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -o $@ \
	    random_metadata_test.c random_metadata_bad.c

random_random.o: ${TARGET_DIR}/random.c
	${HOSTCC} ${RANDOM_CFLAGS} -c -o $@ ${TARGET_DIR}/random.c

random_random32.o: ${TARGET_DIR}/random.c
	${HOSTCC} ${ILP32} ${RANDOM_CFLAGS} -c -o $@ ${TARGET_DIR}/random.c

random_random_ubsan.o: ${TARGET_DIR}/random.c
	${HOSTCC} ${RANDOM_UBSAN} ${RANDOM_CFLAGS} -c -o $@ ${TARGET_DIR}/random.c

random_target.o: ${TARGET_DIR}/random.c
	${ARM_COMPILER} ${RANDOM_TARGET_CFLAGS} -c -o $@ ${TARGET_DIR}/random.c

malloc_test: malloc_test.c ${MALLOC_OBJS}
	${HOSTCC} ${MALLOC_CFLAGS} -o $@ malloc_test.c ${MALLOC_OBJS}

malloc_test32: malloc_test.c ${MALLOC_OBJS32}
	${HOSTCC} ${ILP32} ${MALLOC_CFLAGS} -DHK_ILP32 -o $@ malloc_test.c ${MALLOC_OBJS32}

malloc_arena.o: ${MALLOC_ARENA}
	${HOSTCC} ${MALLOC_CFLAGS} ${MALLOC_RENAMES} -c -o $@ malloc_arena.c

malloc_calloc.o: ${TARGET_DIR}/calloc.c
	${HOSTCC} ${MALLOC_CFLAGS} ${MALLOC_RENAMES} -c -o $@ ${TARGET_DIR}/calloc.c

malloc_sbrk.o: ${SBRK_SRC}
	${HOSTCC} ${MALLOC_CFLAGS} -nostdinc -isystem ${TOPSRC}/include \
		${SBRK_RENAMES} -c -o $@ ${SBRK_SRC}

malloc_arena32.o: ${MALLOC_ARENA}
	${HOSTCC} ${ILP32} ${MALLOC_CFLAGS} ${MALLOC_RENAMES} -c -o $@ malloc_arena.c

malloc_calloc32.o: ${TARGET_DIR}/calloc.c
	${HOSTCC} ${ILP32} ${MALLOC_CFLAGS} ${MALLOC_RENAMES} -c -o $@ ${TARGET_DIR}/calloc.c

malloc_sbrk32.o: ${SBRK_SRC}
	${HOSTCC} ${ILP32} ${MALLOC_CFLAGS} -nostdinc -isystem ${TOPSRC}/include \
		${SBRK_RENAMES} -c -o $@ ${SBRK_SRC}

check-aout:
	sh ./aout_contracts.sh ${TOPSRC} ${ARM_COMPILER} ${TARGET_MACHINE}

${PROG}: libc_contracts_test.c raise.o ctermid.o
	${HOSTCC} ${CFLAGS} -DL_ctermid=9 -o $@ \
		libc_contracts_test.c raise.o ctermid.o

raise.o: ${TARGET_DIR}/raise.c test_shim.h
	${HOSTCC} ${CFLAGS} -Draise=discobsd_raise -Dkill=test_kill \
		-Dgetpid=test_getpid -include test_shim.h -c -o $@ ${TARGET_DIR}/raise.c

ctermid.o: ${TARGET_DIR}/ctermid.c
	${HOSTCC} ${CFLAGS} -DL_ctermid=9 -Dctermid=discobsd_ctermid \
		-c -o $@ ${TARGET_DIR}/ctermid.c

header_contract.o: header_contract.c ${TOPSRC}/include/stdio.h \
    ${TOPSRC}/include/stdlib.h ${TOPSRC}/include/string.h \
    ${TOPSRC}/include/strings.h ${TOPSRC}/include/time.h \
    ${TOPSRC}/include/vis.h ${TOPSRC}/include/_featuretest.h
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -ffreestanding -nostdinc \
	    -isystem ${TOPSRC}/include -c -o $@ header_contract.c

clean:
	rm -f absolute_value_test absolute_value_test32 absolute_value_test_san
	rm -f qsort_context_test qsort_context_test32 qsort_context_test_san
	rm -f bounded_string_test bounded_string_test32 bounded_string_test_san
	rm -f dbm_open_test dbm_open_test32
	rm -f ${PROG} raise.o ctermid.o header_contract.o
	rm -f exit_lifecycle_test exit_lifecycle_test32 quick_exit_test quick_exit_test32
	rm -f alarm_contract_test alarm_contract.o
	rm -f getgrent_contract_test grent_getgrent.o
	rm -f asctime_contract_test asctime_contract_bad_test \
		asctime_asctime.o asctime_bad.o
	rm -f calendar64_test calendar64_test32 calendar64_test_san \
		calendar64_test32_san calendar64_vectors.h cal64_*.o
	rm -f ctime_contract_test ctime_contract_test32 ctime_ctime.o ctime_tzset.o \
		ctime_ctime32.o ctime_tzset32.o calendar32_vectors.h
	rm -f sleep_contract_test sleep_contract_test32 sleep_bad_test32
	rm -f zone_contract_test zone_contract_test32 \
		zone_ctime.o zone_tzset.o zone_ctime32.o zone_tzset32.o \
		zone_timezone.o zone_timezone32.o
	rm -f zone_contract_test_san zone_ctime_san.o zone_tzset_san.o \
		zone_timezone_san.o
	rm -f zone_contract_test_fixed zone_ctime_fixed.o zone_tzset_fixed.o \
		zone_timezone_fixed.o
	rm -f mktime_contract_test mktime_contract_test32 \
		mktime_mktime.o mktime_mktime32.o mktime_ctime.o mktime_tzset.o \
		mktime_ctime32.o mktime_tzset32.o \
		mktime_timezone.o mktime_timezone32.o
	rm -f mktime_contract_test_san mktime_contract_test32_san \
		mktime_mktime_san.o mktime_mktime32_san.o mktime_ctime_san.o \
		mktime_tzset_san.o \
		mktime_ctime32_san.o mktime_tzset32_san.o mktime_timezone_san.o \
		mktime_timezone32_san.o
	rm -f asctime_target.o asctime_target_diagnostic.o ctime_target.o \
		tzset_target.o \
		asctime_format_bad_target.o asctime_format_bad_target.log \
		stdio_format_extensions.o
	rm -f runtime_limits_test runtime_limits_test32 runtime_limits_bad_test \
		runtime_sysconf.o runtime_confstr.o runtime_sysconf32.o \
		runtime_confstr32.o runtime_sysconf_target.o \
		runtime_confstr_target.o
	rm -f difftime_contract_test difftime_contract_test32 \
		difftime_bad_test32 difftime_difftime.o difftime_difftime32.o \
		difftime_target.o
	rm -f malloc_test malloc_test32 ${MALLOC_OBJS} ${MALLOC_OBJS32}
	rm -f printf_contract_test printf_contract_test32 ${PRINTF_OBJS} ${PRINTF_OBJS32}
	rm -f printf_float_contract_test printf_doprnt_fp.o printf_doprnt_float.o
	rm -f printf_g_contract_test printf_g_contract_test_arm pg_*.o
	rm -f huge_val_contract_test huge_val_contract_test_arm hv_*.o huge_val_header*.o
	rm -f printf_doprnt_llong.o printf_doprnt_llong32.o
	rm -f syslog_contract_test syslog_syslog.o syslog_doprnt.o
	rm -f vis_contract_test vis_vis.o
	rm -f string_security_test string_explicit_bzero.o \
		string_timingsafe_bcmp.o string_timingsafe_bcmp_target.o \
		string_timingsafe_bcmp_bad_target.o
	rm -f reallocarray_contract_test memmem_contract_test \
		line_reading_contract_test \
		array_reallocarray.o string_memmem.o \
		reallocarray_contract_test_san memmem_contract_test_san \
		line_reading_contract_test_san array_reallocarray_san.o \
		string_memmem_san.o reallocarray_contract_test32_san \
		memmem_contract_test32_san line_reading_contract_test32_san \
		array_reallocarray32_san.o string_memmem32_san.o
	rm -f qsort_test qsort_test32 qsort_sort.o qsort_sort32.o
	rm -f random_contract_test random_contract_test32 random_contract_ubsan \
		random_metadata_test random_metadata_bad_test random_random.o \
		random_random32.o random_random_ubsan.o random_target.o
	rm -f strtox_test strtox_test32 ${STRTOX_OBJS} ${STRTOX_OBJS32}
	rm -f rwmode_contract_test rwmode_contract_test32 ${RWMODE_OBJS} ${RWMODE_OBJS32}
	rm -f scanf_contract_test scanf_contract_test32 scanf_contract_test_asan \
		${SCANF_OBJS} ${SCANF_OBJS32} ${SCANF_OBJS_ASAN}
	rm -f ${SCANF_LLONG_TESTS} ${SCANF_LLONG_OBJS}

	rm -f ansi_contract_test ansi_contract_test32 ansi_contract_test32_san \
		${ANSI_OBJS} ${ANSI_OBJS32} ${ANSI_SAN32_OBJS} \
		ansi_shared_byte_bad.o ansi_shared_byte_bad_test ansi_shared_byte_bad.log
	rm -f waitpid_contract_test waitpid_contract.o
	rm -f ${QEMU_TESTS} ${QEMU_STRTOX_OBJS} ${QEMU_PRINTF_OBJS} ${QEMU_SCANF_OBJS} \
		random_random_arm.o qemu_support_direct.o qemu_stdio_shim.o

# Thumb-1 execution. The tree sources behind check-strtox, check-random and
# check-printf compile for Cortex-M0+ with the target's own char signedness
# (unsigned; the host variants force -fsigned-char) and run as a newlib
# program under qemu-arm on the ARMv6-M CPU model (tools/qemu-arm.mk), which
# faults on Thumb-2 and unaligned accesses. Each test is the host test
# unchanged. REQUIRE_QEMU makes a missing qemu-arm an error; otherwise the
# target prints SKIP and no ARM execution has occurred. The tests print to
# stdout or stderr through qemu_support.c, and the printf and 64-bit scanf
# tests link the tree's stdio over qemu_stdio_shim.c in place of newlib's.
# The scanf test's oracle there is the tree's strtoll and strtoull: newlib's
# reach _impure_ptr, whose stdio would collide with the tree's. The c17
# header gate holds that pair to glibc and to newlib, so this run shows
# the scanner agreeing with a verified conversion on Thumb-1.
# Each object is a libc member, compiled with the 32-bit view of time as
# lib/libc/Makefile compiles it (NARROW_TIME above).
QEMU_OBJCFLAGS=	-Os ${QEMU_ARM_CPUFLAGS} -fno-common ${NARROW_TIME}
QEMU_TESTS=	strtox_test_arm random_contract_test_arm printf_contract_test_arm \
		scanf_llong_test_arm
QEMU_STRTOX_OBJS= strtox_strtol_arm.o strtox_strtoul_arm.o
QEMU_PRINTF_OBJS= ${PRINTF_OBJS:S/^printf_/printf_arm_/}
QEMU_SCANF_OBJS= ${SCANF_OBJS:S/^scanf_/scanf_arm_/} scanf_arm_doscan_llong.o \
		scanf_arm_strtoll.o scanf_arm_strtoull.o

check-qemu:
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
	@echo "libc contracts qemu: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
	@echo "SKIP libc contracts qemu: qemu-arm unavailable; no ARM execution occurred"
.else
	@${MAKE} ${QEMU_TESTS}
	${QEMU_ARM_RUN} ./strtox_test_arm
	${QEMU_ARM_RUN} ./random_contract_test_arm
	${QEMU_ARM_RUN} ./printf_contract_test_arm
	${QEMU_ARM_RUN} ./scanf_llong_test_arm
	@${MAKE} printf_g_contract_test_arm huge_val_contract_test_arm
	${QEMU_ARM_RUN} ./printf_g_contract_test_arm
	${QEMU_ARM_RUN} ./huge_val_contract_test_arm
	${PYTHON} ${.CURDIR}/bounded_string.py ${TOPSRC} ${QEMU_ARM_CC:Q} --arm
	${PYTHON} ${.CURDIR}/quick_exit.py ${TOPSRC} ${QEMU_ARM_CC:Q} --arm
	${PYTHON} ${.CURDIR}/absolute_value.py ${TOPSRC} ${QEMU_ARM_CC:Q} --arm
	${PYTHON} ${.CURDIR}/qsort_context.py ${TOPSRC} ${QEMU_ARM_CC:Q} --arm
	${PYTHON} ${.CURDIR}/dbm_open.py ${TOPSRC} ${QEMU_ARM_CC:Q} --arm
.endif

qemu_support_direct.o: ${QEMU_SUPPORT}
	${QEMU_ARM_CC} -std=gnu17 -Wall -Wextra -Werror ${QEMU_OBJCFLAGS} \
	    -DQEMU_SUPPORT_EXIT_DIRECT -c -o $@ ${QEMU_SUPPORT}

qemu_stdio_shim.o: qemu_stdio_shim.c
	${QEMU_ARM_CC} -std=gnu17 -Wall -Wextra -Werror ${QEMU_OBJCFLAGS} \
	    -ffreestanding -fno-builtin -c -o $@ qemu_stdio_shim.c

strtox_strtol_arm.o: ${STRTOX_DIR}/strtol.c
	${QEMU_ARM_CC} ${STRTOX_CFLAGS:N-fsigned-char} ${QEMU_OBJCFLAGS} \
	    -Dstrtol=db_strtol -c -o $@ ${STRTOX_DIR}/strtol.c

strtox_strtoul_arm.o: ${STRTOX_DIR}/strtoul.c
	${QEMU_ARM_CC} ${STRTOX_CFLAGS:N-fsigned-char} ${QEMU_OBJCFLAGS} \
	    -Dstrtoul=db_strtoul -c -o $@ ${STRTOX_DIR}/strtoul.c

strtox_test_arm: strtox_test.c ${QEMU_STRTOX_OBJS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${STRTOX_CFLAGS:N-fsigned-char} ${QEMU_ARM_CFLAGS} \
	    -o $@ strtox_test.c ${QEMU_STRTOX_OBJS} ${QEMU_SUPPORT} \
	    ${QEMU_ARM_LIBS}

random_random_arm.o: ${TARGET_DIR}/random.c
	${QEMU_ARM_CC} ${RANDOM_CFLAGS} ${QEMU_OBJCFLAGS} -ffreestanding \
	    -c -o $@ ${TARGET_DIR}/random.c

random_contract_test_arm: random_contract_test.c random_random_arm.o \
    ${QEMU_SUPPORT}
	${QEMU_ARM_CC} -std=c17 ${QEMU_ARM_CFLAGS:N-std=*} \
	    -o $@ random_contract_test.c random_random_arm.o ${QEMU_SUPPORT} \
	    ${QEMU_ARM_LIBS}

.for _m in snprintf vsnprintf doprnt flsbuf doprnt_llong
printf_arm_${_m}.o: ${PRINTF_DIR}/${_m}.c
	${QEMU_ARM_CC} ${PRINTF_CFLAGS} ${QEMU_OBJCFLAGS} -c -o $@ \
	    ${PRINTF_DIR}/${_m}.c
.endfor

printf_contract_test_arm: printf_contract_test.c ${QEMU_PRINTF_OBJS} \
    qemu_support_direct.o qemu_stdio_shim.o
	${QEMU_ARM_CC} ${PRINTF_CFLAGS} ${QEMU_ARM_CFLAGS:N-std=*} \
	    -o $@ printf_contract_test.c ${QEMU_PRINTF_OBJS} \
	    qemu_support_direct.o qemu_stdio_shim.o ${QEMU_ARM_LIBS}

.for _m in doscan doscan_float doscan_llong scanf ungetc filbuf
scanf_arm_${_m}.o: ${SCANF_DIR}/${_m}.c ${SCANF_DIR}/doscan.h
	${QEMU_ARM_CC} ${SCANF_CFLAGS} ${QEMU_OBJCFLAGS} -c -o $@ \
	    ${SCANF_DIR}/${_m}.c
.endfor

.for _m in strtod strtoll strtoull
scanf_arm_${_m}.o: ${SCANF_STDLIB_DIR}/${_m}.c
	${QEMU_ARM_CC} ${SCANF_CFLAGS} ${QEMU_OBJCFLAGS} -c -o $@ \
	    ${SCANF_STDLIB_DIR}/${_m}.c
.endfor

scanf_arm_ctype.o: ${SCANF_GEN_DIR}/ctype_.c
	${QEMU_ARM_CC} ${SCANF_CFLAGS} ${QEMU_OBJCFLAGS} -c -o $@ \
	    ${SCANF_GEN_DIR}/ctype_.c

scanf_llong_test_arm: scanf_llong_test.c ${QEMU_SCANF_OBJS} \
    qemu_support_direct.o qemu_stdio_shim.o
	${QEMU_ARM_CC} ${SCANF_CFLAGS} ${QEMU_ARM_CFLAGS:N-std=*} \
	    -o $@ scanf_llong_test.c ${QEMU_SCANF_OBJS} \
	    qemu_support_direct.o qemu_stdio_shim.o ${QEMU_ARM_LIBS}

all: check

.PHONY: all check check-host check-aout check-asctime check-calendar64 check-ctime \
	check-ctime-cross check-difftime check-difftime-cross \
	check-alarm-contract check-alarm-contract-cross \
	check-waitpid-contract check-waitpid-contract-cross \
	check-runtime-limits check-runtime-limits-cross \
	check-malloc check-printf check-qsort check-array-memory-line \
	check-random check-random-cross \
	check-printf-float check-printf-g check-huge-val check-rwmode check-scanf \
	check-scanf-llong check-string-security \
	check-array-memory-line-cross check-string-security-cross check-strtox \
	check-syslog check-vis check-getgrent check-getgrent-cross check-exit-lifecycle \
	check-quick-exit check-quick-exit-cross check-qsort-context-cross check-dbm-open \
	check-qemu clean

# The C17 stdio surface outside the read and write path: the mode strings
# fopen accepts and the descriptor flags they produce, fflush over every
# stream, rewind as a seek, fgetpos and fsetpos, and setvbuf's argument
# checking. The tree sources compile against the tree's headers, so the FILE
# layout and the getc and putc macros are the target's; open(2) is renamed
# because the tree's O_APPEND and O_TRUNC are not the host's bit values.
#
# findiop.c joins only the ILP32 link: its static assertion pins sizeof(FILE)
# at the target's 24 bytes, which a 64-bit pointer cannot satisfy. At host
# width the stream table and the walk are the test's own and every other
# check is the same.
# The getc and putc macros expand in the test too, so the two members they
# call are renamed on both sides of the link; everything else moves aside for
# the tree sources alone, leaving the test with the host's open(2) to call.
ANSI_MACROS=	-D_filbuf=db_filbuf -D_flsbuf=db_flsbuf
ANSI_RENAMES=	${ANSI_MACROS} -Dfflush=db_fflush \
		-Dfclose=db_fclose -Dungetc=db_ungetc -Dfstat=test_fstat \
		-Dfseek=db_fseek -Dftell=db_ftell -Drewind=db_rewind \
		-Dsetvbuf=db_setvbuf -Dfopen=db_fopen -Dfdopen=db_fdopen \
		-Dfreopen=db_freopen -Dfgetpos=db_fgetpos -Dfsetpos=db_fsetpos \
		-Dopen=test_open -Derrno=ansi_test_errno \
		-Dgetdtablesize=test_getdtablesize -Dmalloc=ansi_malloc
ANSI_CFLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include
ANSI_MEMBERS=	flags fopen fdopen freopen filbuf flsbuf fseek ftell rew \
		setvbuf fgetpos ungetc
ANSI_OBJS=	${ANSI_MEMBERS:C/^/ansi_/:C/$/.o/} ansi_open_shim.o
ANSI_OBJS32=	${ANSI_MEMBERS:C/^/ansi32_/:C/$/.o/} ansi32_findiop.o \
		ansi32_open_shim.o
ANSI_SAN_FLAGS= -fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer
ANSI_SAN32_OK!= sh ${TOPSRC}/tools/compiler-probe.sh ilp32 ${HOSTCC} ${ILP32} ${ANSI_SAN_FLAGS}
ANSI_SAN32_OBJS= ${ANSI_MEMBERS:C/^/ansi32_san_/:C/$/.o/} \
		ansi32_san_findiop.o ansi32_san_open_shim.o

check-ansi: ansi_contract_test ansi_shared_byte_bad_test
	${TEST_EXECUTION} run libc.ansi.native -- ./ansi_contract_test
	${TEST_EXECUTION} run libc.ansi.shared-byte-negative -- \
	    ./ansi_shared_byte_bad_test > ansi_shared_byte_bad.log 2>&1
	@grep -Fq "a peer read preserves the first stream's pushback" ansi_shared_byte_bad.log
	@echo "ansi: shared-byte mutation rejected by independent pushback"
.if ${ILP32_OK} == "yes"
	@${MAKE} ansi_contract_test32
	${TEST_EXECUTION} run libc.ansi.ilp32 -- ./ansi_contract_test32
.else
	${TEST_EXECUTION} skip libc.ansi.ilp32 "check-libc-ansi: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${ANSI_SAN32_OK} == "yes"
	@${MAKE} ansi_contract_test32_san
	${TEST_EXECUTION} run libc.ansi.sanitized-ilp32 -- ./ansi_contract_test32_san
.else
	${TEST_EXECUTION} skip libc.ansi.sanitized-ilp32 "check-libc-ansi: ${HOSTCC} cannot execute ILP32 address/undefined sanitizers"
.endif

ansi_contract_test: ansi_contract_test.c ${ANSI_OBJS}
	${HOSTCC} ${ANSI_CFLAGS} ${ANSI_MACROS} -o $@ ansi_contract_test.c \
	    ${ANSI_OBJS}

ansi_contract_test32: ansi_contract_test.c ${ANSI_OBJS32}
	${HOSTCC} ${ILP32} ${ANSI_CFLAGS} ${ANSI_MACROS} -DANSI_REAL_FINDIOP \
	    -o $@ ansi_contract_test.c ${ANSI_OBJS32}

# The shim carries the host's <fcntl.h> alone, so it takes neither -nostdinc
# nor the tree's include path.
ansi_open_shim.o: ansi_open_shim.c
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -c -o $@ ansi_open_shim.c

ansi32_open_shim.o: ansi_open_shim.c
	${HOSTCC} ${ILP32} -std=c17 -Wall -Wextra -Werror -c -o $@ ansi_open_shim.c

ansi32_findiop.o: ${PRINTF_DIR}/findiop.c
	${HOSTCC} ${ILP32} ${ANSI_CFLAGS} ${ANSI_RENAMES} -Dcalloc=ansi_calloc \
	    -c -o $@ ${PRINTF_DIR}/findiop.c

.for _m in ${ANSI_MEMBERS}
ansi_${_m}.o: ${PRINTF_DIR}/${_m}.c
	${HOSTCC} ${ANSI_CFLAGS} ${ANSI_RENAMES} -c -o $@ ${PRINTF_DIR}/${_m}.c

ansi32_${_m}.o: ${PRINTF_DIR}/${_m}.c
	${HOSTCC} ${ILP32} ${ANSI_CFLAGS} ${ANSI_RENAMES} -c -o $@ \
	    ${PRINTF_DIR}/${_m}.c
.endfor

ansi_shared_byte_bad.o: ansi_shared_byte_bad.c ${PRINTF_DIR}/filbuf.c
	${HOSTCC} ${ANSI_CFLAGS} ${ANSI_RENAMES} -c -o $@ ansi_shared_byte_bad.c

ansi_shared_byte_bad_test: ansi_contract_test.c ${ANSI_OBJS} ansi_shared_byte_bad.o
	${HOSTCC} ${ANSI_CFLAGS} ${ANSI_MACROS} -o $@ ansi_contract_test.c \
	    ${ANSI_OBJS:Nansi_filbuf.o} ansi_shared_byte_bad.o

ansi_contract_test32_san: ansi_contract_test.c ${ANSI_SAN32_OBJS}
	${HOSTCC} ${ILP32} ${ANSI_SAN_FLAGS} ${ANSI_CFLAGS} ${ANSI_MACROS} \
	    -DANSI_REAL_FINDIOP -o $@ ansi_contract_test.c ${ANSI_SAN32_OBJS}

ansi32_san_findiop.o: ${PRINTF_DIR}/findiop.c
	${HOSTCC} ${ILP32} ${ANSI_SAN_FLAGS} ${ANSI_CFLAGS} ${ANSI_RENAMES} \
	    -Dcalloc=ansi_calloc -c -o $@ ${PRINTF_DIR}/findiop.c

ansi32_san_open_shim.o: ansi_open_shim.c
	${HOSTCC} ${ILP32} ${ANSI_SAN_FLAGS} -std=c17 -Wall -Wextra -Werror \
	    -c -o $@ ansi_open_shim.c

.for _m in ${ANSI_MEMBERS}
ansi32_san_${_m}.o: ${PRINTF_DIR}/${_m}.c
	${HOSTCC} ${ILP32} ${ANSI_SAN_FLAGS} ${ANSI_CFLAGS} ${ANSI_RENAMES} \
	    -c -o $@ ${PRINTF_DIR}/${_m}.c
.endfor

# The tree formatter with its float member against a host C library oracle.
# The stream and formatter names move aside (PRINTF_ORACLE_RENAMES) so the
# host snprintf stays the host's and the test reaches the tree's as
# tree_snprintf. check-printf-g runs the corpus natively; check-qemu runs the
# same test with the tree sources built for Cortex-M0+ (Thumb-1, soft
# float) under qemu-arm against newlib's snprintf; check-printf-g-mutants
# rebuilds the tree's float member with one deliberate defect at a time and
# requires the test to reject each.
PRINTF_ORACLE_RENAMES=	-Dsnprintf=tree_snprintf -Dvsnprintf=tree_vsnprintf \
		-D_doprnt=tree_doprnt -D_flsbuf=tree_flsbuf \
		-D_fwalk=tree_fwalk -D_iob=tree_iob \
		-Dfflush=tree_fflush -Dfclose=tree_fclose \
		-Derrno=printf_test_errno
PRINTF_ORACLE_CFLAGS=	-std=c17 -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include ${PRINTF_ORACLE_RENAMES}
PRINTF_ORACLE_MEMBERS=	snprintf vsnprintf flsbuf doprnt doprnt_float doprnt_llong
PRINTF_ORACLE_OBJS=	${PRINTF_ORACLE_MEMBERS:C/^/pg_/:C/$/.o/} pg_stubs.o
PRINTF_ORACLE_ARM_OBJS=	${PRINTF_ORACLE_MEMBERS:C/^/pg_arm_/:C/$/.o/} pg_arm_stubs.o \
		pg_arm_isnan.o pg_arm_isinf.o

check-printf-g: printf_g_contract_test check-printf-g-mutants
	${TEST_EXECUTION} run libc.printf-g.native -- ./printf_g_contract_test

check-printf-g-mutants: ${PRINTF_ORACLE_OBJS:Npg_doprnt_float.o:Npg_doprnt.o} printf_g_contract_test.c
	${PYTHON} ${.CURDIR}/printf_g_mutate.py ${TOPSRC} ${HOSTCC:Q} \
	    ${PRINTF_ORACLE_CFLAGS:Q} printf_g_contract_test.c \
	    ${PRINTF_ORACLE_OBJS:Npg_doprnt_float.o:Npg_doprnt.o}

printf_g_contract_test: printf_g_contract_test.c ${PRINTF_ORACLE_OBJS}
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -o $@ printf_g_contract_test.c \
	    ${PRINTF_ORACLE_OBJS} -lm

.for _m in ${PRINTF_ORACLE_MEMBERS}
pg_${_m}.o: ${PRINTF_DIR}/${_m}.c
	${HOSTCC} ${PRINTF_ORACLE_CFLAGS} -c -o $@ ${PRINTF_DIR}/${_m}.c
.endfor

pg_stubs.o: printf_oracle_stubs.c
	${HOSTCC} ${PRINTF_ORACLE_CFLAGS} -c -o $@ printf_oracle_stubs.c

# tools/host-test.mk supplies the qemu-arm newlib link; it is included last
# because its tl_*.o rules would otherwise come before the default target.
include ${TOPSRC}/tools/host-test.mk

# On the target the formatter classifies infinities and NaNs with the tree's
# isinf and isnan, which read the double as two 32-bit words; at host width
# they would read it as one long, so the host run keeps the host's.
PRINTF_ORACLE_ARM_CFLAGS=	-std=gnu17 -Wall -Wextra -Werror -Os \
		-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -ffreestanding \
		-nostdinc -isystem ${TOPSRC}/include ${PRINTF_ORACLE_RENAMES} \
		-Disnan=tree_isnan -Disinf=tree_isinf ${NARROW_TIME}

.for _m in ${PRINTF_ORACLE_MEMBERS}
pg_arm_${_m}.o: ${PRINTF_DIR}/${_m}.c
	${QEMU_ARM_CC} ${PRINTF_ORACLE_ARM_CFLAGS} -c -o $@ ${PRINTF_DIR}/${_m}.c
.endfor

pg_arm_stubs.o: printf_oracle_stubs.c
	${QEMU_ARM_CC} ${PRINTF_ORACLE_ARM_CFLAGS} -c -o $@ printf_oracle_stubs.c

.for _m in isnan isinf
pg_arm_${_m}.o: ${TOPSRC}/lib/libc/gen/${_m}.c
	${QEMU_ARM_CC} ${PRINTF_ORACLE_ARM_CFLAGS} -fno-strict-aliasing -c -o $@ \
	    ${TOPSRC}/lib/libc/gen/${_m}.c
.endfor

printf_g_contract_test_arm: printf_g_contract_test.c ${PRINTF_ORACLE_ARM_OBJS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -DPRINTF_G_ITERATIONS_DIVISOR=${PRINTF_G_ARM_DIVISOR} \
	    -DHOST_G_TIE_BUG \
	    -o $@ printf_g_contract_test.c ${PRINTF_ORACLE_ARM_OBJS} \
	    ${QEMU_SUPPORT} -lm ${QEMU_ARM_LIBS}

# The ARM run executes the corpus under software emulation of a software
# floating-point library, so it draws every PRINTF_G_ARM_DIVISOR-th value.
PRINTF_G_ARM_DIVISOR?=	8

.PHONY: check-printf-g-mutants check-huge-val check-huge-val-mutants check-qemu

# HUGE_VAL and the overflow returns of the tree libm, libc ldexp, frexp, atof
# and strtod, compiled from the tree under tree_ names (the sources are K&R
# C, so the dialect and warning policy follow tools/host-test.mk), with the
# header constants compiled as strict C17. The formatter objects are the
# printf-g ones. check-huge-val-mutants restores one defect at a time and
# requires the test to reject each.
HV_NAMES=	exp log pow sinh cosh tan sin cos floor ceil ldexp frexp modf \
		atof strtod j0 j1 jn y0 y1 yn sqrt atan atan2 asin acos tanh \
		fabs hypot fmod erf erfc log10
HV_RENAMES=	${HV_NAMES:@n@-D${n}=tree_${n}@} -Derrno=tree_errno
HV_LIBM=	asin atan erf exp fabs floor fmod hypot j0 j1 jn log pow sin \
		sinh sqrt tan tanh
HV_GEN=		ldexp frexp modf atof
HV_STDLIB=	strtod
HV_CFLAGS=	-std=gnu89 -w -O1 -fno-builtin -nostdinc -Istrtox_include \
		-isystem ${TOPSRC}/include ${HV_RENAMES}
HV_OBJS=	${HV_LIBM:C/^/hv_/:C/$/.o/} ${HV_GEN:C/^/hv_/:C/$/.o/} \
		${HV_STDLIB:C/^/hv_/:C/$/.o/}
HV_ARM_OBJS=	${HV_LIBM:C/^/hv_arm_/:C/$/.o/} ${HV_GEN:C/^/hv_arm_/:C/$/.o/} \
		${HV_STDLIB:C/^/hv_arm_/:C/$/.o/}
HV_HEADER_CFLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include

check-huge-val: huge_val_contract_test check-huge-val-mutants
	${TEST_EXECUTION} run libc.huge-val.native -- ./huge_val_contract_test

check-huge-val-mutants: huge_val_header.c huge_val_stubs.c huge_val_contract_test.c \
		${PRINTF_ORACLE_OBJS}
	${PYTHON} ${.CURDIR}/huge_val_mutate.py ${TOPSRC} ${HOSTCC:Q} \
	    ${HV_CFLAGS:Q} ${HV_HEADER_CFLAGS:Q} \
	    ${PRINTF_ORACLE_OBJS} -- \
	    ${HV_LIBM:C,^,lib/libm/,:C,$,.c,} ${HV_GEN:C,^,lib/libc/gen/,:C,$,.c,} \
	    ${HV_STDLIB:C,^,lib/libc/stdlib/,:C,$,.c,}

huge_val_contract_test: huge_val_contract_test.c huge_val_header.o huge_val_stubs.c \
		${HV_OBJS} ${PRINTF_ORACLE_OBJS}
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -o $@ huge_val_contract_test.c \
	    huge_val_header.o huge_val_stubs.c ${HV_OBJS} ${PRINTF_ORACLE_OBJS} -lm

huge_val_header.o: huge_val_header.c ${TOPSRC}/include/math.h
	${HOSTCC} ${HV_HEADER_CFLAGS} -c -o $@ huge_val_header.c

.for _m in ${HV_LIBM}
hv_${_m}.o: ${TOPSRC}/lib/libm/${_m}.c ${TOPSRC}/include/math.h
	${HOSTCC} ${HV_CFLAGS} -c -o $@ ${TOPSRC}/lib/libm/${_m}.c
.endfor
.for _m in ${HV_GEN}
hv_${_m}.o: ${TOPSRC}/lib/libc/gen/${_m}.c ${TOPSRC}/include/math.h
	${HOSTCC} ${HV_CFLAGS} -c -o $@ ${TOPSRC}/lib/libc/gen/${_m}.c
.endfor
.for _m in ${HV_STDLIB}
hv_${_m}.o: ${TOPSRC}/lib/libc/stdlib/${_m}.c ${TOPSRC}/include/math.h
	${HOSTCC} ${HV_CFLAGS} -c -o $@ ${TOPSRC}/lib/libc/stdlib/${_m}.c
.endfor

HV_ARM_CFLAGS=	-std=gnu89 -w -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft \
		-ffreestanding -fno-builtin -nostdinc -Istrtox_include \
		-isystem ${TOPSRC}/include ${HV_RENAMES} ${NARROW_TIME}

.for _m in ${HV_LIBM}
hv_arm_${_m}.o: ${TOPSRC}/lib/libm/${_m}.c ${TOPSRC}/include/math.h
	${QEMU_ARM_CC} ${HV_ARM_CFLAGS} -c -o $@ ${TOPSRC}/lib/libm/${_m}.c
.endfor
.for _m in ${HV_GEN}
hv_arm_${_m}.o: ${TOPSRC}/lib/libc/gen/${_m}.c ${TOPSRC}/include/math.h
	${QEMU_ARM_CC} ${HV_ARM_CFLAGS} -c -o $@ ${TOPSRC}/lib/libc/gen/${_m}.c
.endfor
.for _m in ${HV_STDLIB}
hv_arm_${_m}.o: ${TOPSRC}/lib/libc/stdlib/${_m}.c ${TOPSRC}/include/math.h
	${QEMU_ARM_CC} ${HV_ARM_CFLAGS} -c -o $@ ${TOPSRC}/lib/libc/stdlib/${_m}.c
.endfor

huge_val_header_arm.o: huge_val_header.c ${TOPSRC}/include/math.h
	${QEMU_ARM_CC} ${HV_HEADER_CFLAGS} -mcpu=cortex-m0plus -mthumb \
	    -mfloat-abi=soft -ffreestanding -Os -c -o $@ huge_val_header.c

huge_val_contract_test_arm: huge_val_contract_test.c huge_val_header_arm.o \
		huge_val_stubs.c ${HV_ARM_OBJS} ${PRINTF_ORACLE_ARM_OBJS} ${QEMU_SUPPORT}
	${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -o $@ huge_val_contract_test.c \
	    huge_val_header_arm.o huge_val_stubs.c ${HV_ARM_OBJS} \
	    ${PRINTF_ORACLE_ARM_OBJS} ${QEMU_SUPPORT} -lm ${QEMU_ARM_LIBS}
