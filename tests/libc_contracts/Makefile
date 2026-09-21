TOPSRC!= cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
ARM_COMPILER?= ${GCCPREFIX}-gcc
TARGET_MACHINE?= rp2040
PROG= libc_contracts_test
TARGET_DIR= ../../lib/libc/gen
STRING_DIR= ../../lib/libc/string
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
ILP32_OK!=	printf 'int main(void){return 0;}\n' > /tmp/ilp32probe$$$$.c; \
		if ${HOSTCC} -m32 -o /dev/null /tmp/ilp32probe$$$$.c 2>/dev/null; \
		then echo yes; else echo no; fi; \
		rm -f /tmp/ilp32probe$$$$.c

check-host: ${PROG} header_contract.o check-ctime check-zone check-difftime \
	check-runtime-limits check-malloc check-random check-printf \
	check-printf-float check-scanf check-rwmode check-syslog check-vis \
	check-qsort check-strtox check-string-security
	./${PROG}

check: check-host check-aout

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
		-D_doscan=db_doscan -D__doscan_cvt=db_doscan_cvt \
		-Dungetc=db_ungetc -D_filbuf=db_filbuf -Dstrtod=db_strtod
SCANF_CFLAGS=	-std=c17 -Wall -Wextra -Werror -nostdinc \
		-isystem ${TOPSRC}/include ${SCANF_RENAMES}
SCANF_OBJS=	scanf_doscan.o scanf_doscan_float.o scanf_scanf.o \
		scanf_ungetc.o scanf_filbuf.o scanf_strtod.o scanf_ctype.o
SCANF_OBJS32=	scanf_doscan32.o scanf_doscan_float32.o scanf_scanf32.o \
		scanf_ungetc32.o scanf_filbuf32.o scanf_strtod32.o scanf_ctype32.o
SCANF_OBJS_ASAN=scanf_doscan_asan.o scanf_doscan_float_asan.o \
		scanf_scanf_asan.o scanf_ungetc_asan.o scanf_filbuf_asan.o \
		scanf_strtod_asan.o scanf_ctype_asan.o

# The address sanitizer is what detects the defect class this scanner was
# rewritten to remove: a staging buffer overrun lands inside the scanner's
# own frame, where a guard byte around the caller's destination sees
# nothing. The V7 scanner aborts this run; the replacement passes it.
ASAN=		-fsanitize=address -fno-omit-frame-pointer
ASAN_OK!=	printf 'int main(void){return 0;}\n' > /tmp/asanprobe$$$$.c; \
		if ${HOSTCC} -fsanitize=address -o /dev/null \
		    /tmp/asanprobe$$$$.c 2>/dev/null; \
		then echo yes; else echo no; fi; \
		rm -f /tmp/asanprobe$$$$.c

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
SYSLOG_FORMAT_OBJS=	printf_doprnt.o printf_flsbuf.o printf_doprnt_llong.o

check-syslog: syslog_contract_test
	./syslog_contract_test

syslog_contract_test: syslog_contract_test.c syslog_syslog.o ${SYSLOG_FORMAT_OBJS}
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -nostdinc \
	    -isystem ${TOPSRC}/include -o $@ syslog_contract_test.c \
	    syslog_syslog.o ${SYSLOG_FORMAT_OBJS}

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

CTIME_RENAMES=	-Dctime=db_ctime -Dlocaltime=db_localtime \
	-Dgmtime=db_gmtime -Dtzset=db_tzset -Dtzname=db_tzname \
	-Dtztab=db_tztab
CTIME_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
	-ffunction-sections -fdata-sections -nostdinc \
	-isystem ${TOPSRC}/include ${CTIME_RENAMES}
CTIME_TARGET_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wformat=2 \
	-Wformat-overflow=2 -Wold-style-definition -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -ffreestanding -nostdinc -I${TOPSRC}/include -Os
CTIME_TARGET_DIAGNOSTIC_CFLAGS=	${CTIME_TARGET_CFLAGS} \
	-Dsnprintf=__builtin_snprintf
CTIME_TARGET_BAD_CFLAGS=	${CTIME_TARGET_CFLAGS} \
	-Dsprintf=__builtin_sprintf
# The zone file reader, which is the one part of libc that decodes bytes a
# caller names: TZ may give an absolute path, so every count in the file
# bounds a walk over a fixed-size array here.
ZONE_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
	-ffunction-sections -fdata-sections -nostdinc \
	-isystem ${TOPSRC}/include ${CTIME_RENAMES}
ZONE_TZ_WARN=	-Wno-old-style-definition -Wno-deprecated-non-prototype

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
	-isystem ${TOPSRC}/include -Ddifftime=db_difftime
DIFFTIME_TARGET_CFLAGS=	${DIFFTIME_CFLAGS} -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -ffreestanding -Os
GC_SECTIONS!=	if [ "$$(uname -s)" = Darwin ] ; then echo -Wl,-dead_strip ; \
	else echo -Wl,--gc-sections ; fi

# The typecnt bound is a read past ttis[] whose effect otherwise depends on
# what happens to lie there, so the sanitizer run is what names it.
ZONE_SAN=	-fsanitize=address,undefined -fno-omit-frame-pointer
ZONE_SAN_OK!=	printf 'int main(void){return 0;}\n' > /tmp/zsan$$$$.c; \
		if ${HOSTCC} -fsanitize=address,undefined -o /dev/null \
		    /tmp/zsan$$$$.c 2>/dev/null; then echo yes; else echo no; fi; \
		rm -f /tmp/zsan$$$$.c

check-zone: zone_contract_test
	./zone_contract_test
.if ${ZONE_SAN_OK} == "yes"
	@${MAKE} zone_contract_test_san
	./zone_contract_test_san
.else
	@echo "check-libc-zone: no sanitizer runtime; that run skipped" >&2
.endif
.if ${ILP32_OK} == "yes"
	@${MAKE} zone_contract_test32
	./zone_contract_test32
.else
	@echo "check-libc-zone: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
.endif

check-ctime: check-asctime ctime_contract_test
	./ctime_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} ctime_contract_test32
	./ctime_contract_test32
.else
	@echo "check-libc-ctime: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
.endif

check-ctime-cross: asctime_target.o asctime_target_diagnostic.o ctime_target.o \
	stdio_format_extensions.o
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
	./runtime_limits_test
	@if ./runtime_limits_bad_test >/dev/null 2>&1; then \
		echo "runtime limits permissive-selector negative control passed" >&2; \
		exit 1; \
	fi
	@echo "runtime limits permissive-selector negative control rejected"
.if ${ILP32_OK} == "yes"
	@${MAKE} runtime_limits_test32
	./runtime_limits_test32
.else
	@echo "check-libc-runtime-limits: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
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
	./difftime_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} difftime_contract_test32 difftime_bad_test32
	./difftime_contract_test32
	@if ./difftime_bad_test32 >/dev/null 2>&1; then \
		echo "difftime subtract-first negative control passed" >&2; \
		exit 1; \
	fi
	@echo "difftime subtract-first negative control rejected"
.else
	@echo "check-libc-difftime: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
.endif

check-difftime-cross: difftime_target.o
	@test "$$(${GCCPREFIX}-nm -u difftime_target.o | wc -l)" -eq 2
	@${GCCPREFIX}-nm -u difftime_target.o | grep -q ' U __aeabi_i2d$$'
	@${GCCPREFIX}-nm -u difftime_target.o | grep -q ' U __aeabi_dsub$$'
	@echo "difftime Cortex-M0+ object converts endpoints before ROM double subtraction"

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
	./asctime_contract_test
	@if ./asctime_contract_bad_test >/dev/null 2>&1; then \
		echo "asctime contract negative control passed" >&2; \
		exit 1; \
	fi
	@echo "asctime contract negative control rejected"

asctime_contract_test: asctime_contract_test.c asctime_asctime.o
	${HOSTCC} ${ASCTIME_CFLAGS} -o $@ asctime_contract_test.c \
	    asctime_asctime.o

asctime_contract_bad_test: asctime_contract_test.c asctime_bad.o
	${HOSTCC} ${ASCTIME_CFLAGS} -o $@ asctime_contract_test.c asctime_bad.o

asctime_asctime.o: ${TARGET_DIR}/asctime.c
	${HOSTCC} ${ASCTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/asctime.c

asctime_bad.o: asctime_bad.c
	${HOSTCC} ${ASCTIME_CFLAGS} -c -o $@ asctime_bad.c

ctime_contract_test: ctime_contract_test.c ctime_ctime.o
	${HOSTCC} ${CTIME_CFLAGS} ${GC_SECTIONS} -o $@ \
	    ctime_contract_test.c ctime_ctime.o

ctime_contract_test32: ctime_contract_test.c ctime_ctime32.o
	${HOSTCC} ${ILP32} ${CTIME_CFLAGS} ${GC_SECTIONS} -o $@ \
	    ctime_contract_test.c ctime_ctime32.o

zone_contract_test: zone_contract_test.c zone_ctime.o zone_timezone.o
	${HOSTCC} ${ZONE_CFLAGS} ${GC_SECTIONS} -o $@ \
	    zone_contract_test.c zone_ctime.o zone_timezone.o

zone_contract_test32: zone_contract_test.c zone_ctime32.o zone_timezone32.o
	${HOSTCC} ${ILP32} ${ZONE_CFLAGS} ${GC_SECTIONS} -o $@ \
	    zone_contract_test.c zone_ctime32.o zone_timezone32.o

zone_contract_test_san: zone_contract_test.c zone_ctime_san.o \
    zone_timezone_san.o
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_SAN} -o $@ \
	    zone_contract_test.c zone_ctime_san.o zone_timezone_san.o

zone_ctime_san.o: ${TARGET_DIR}/ctime.c
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_SAN} -c -o $@ ${TARGET_DIR}/ctime.c

zone_timezone_san.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_SAN} ${ZONE_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

zone_ctime.o: ${TARGET_DIR}/ctime.c
	${HOSTCC} ${ZONE_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

zone_ctime32.o: ${TARGET_DIR}/ctime.c
	${HOSTCC} ${ILP32} ${ZONE_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

# ctime.c's tzsetkernel() names tztab(), which timezone.c defines and which
# the gate reaches whenever a file is refused and the fallback runs.
zone_timezone.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ZONE_CFLAGS} ${ZONE_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

zone_timezone32.o: ${TARGET_DIR}/timezone.c
	${HOSTCC} ${ILP32} ${ZONE_CFLAGS} ${ZONE_TZ_WARN} -c -o $@ \
	    ${TARGET_DIR}/timezone.c

ctime_ctime.o: ${TARGET_DIR}/ctime.c
	${HOSTCC} ${CTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

ctime_ctime32.o: ${TARGET_DIR}/ctime.c
	${HOSTCC} ${ILP32} ${CTIME_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

asctime_target.o: ${TARGET_DIR}/asctime.c
	${ARM_COMPILER} ${CTIME_TARGET_CFLAGS} -c -o $@ ${TARGET_DIR}/asctime.c

asctime_target_diagnostic.o: ${TARGET_DIR}/asctime.c
	${ARM_COMPILER} ${CTIME_TARGET_DIAGNOSTIC_CFLAGS} -c -o $@ \
	    ${TARGET_DIR}/asctime.c

ctime_target.o: ${TARGET_DIR}/ctime.c
	${ARM_COMPILER} ${CTIME_TARGET_CFLAGS} -c -o $@ ${TARGET_DIR}/ctime.c

stdio_format_extensions.o: stdio_format_extensions.c
	${ARM_COMPILER} ${CTIME_TARGET_CFLAGS} -c -o $@ \
	    stdio_format_extensions.c

check-string-security: string_security_test
	./string_security_test

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
	./vis_contract_test

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
	./printf_float_contract_test

printf_float_contract_test: printf_contract_test.c ${PRINTF_FLOAT_OBJS}
	${HOSTCC} ${PRINTF_FLOAT_CFLAGS} -o $@ printf_contract_test.c \
	    ${PRINTF_FLOAT_OBJS} -lm

printf_doprnt_fp.o: ${PRINTF_DIR}/doprnt.c
	${HOSTCC} ${PRINTF_FLOAT_CFLAGS} -c -o $@ ${PRINTF_DIR}/doprnt.c

printf_doprnt_float.o: ${PRINTF_DIR}/doprnt_float.c
	${HOSTCC} ${PRINTF_FLOAT_CFLAGS} -c -o $@ ${PRINTF_DIR}/doprnt_float.c

check-printf: printf_contract_test
	./printf_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} printf_contract_test32
	./printf_contract_test32
.else
	@echo "check-libc-printf: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
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
	./scanf_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} scanf_contract_test32
	./scanf_contract_test32
.else
	@echo "check-libc-scanf: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
.endif
.if ${ASAN_OK} == "yes"
	@${MAKE} scanf_contract_test_asan
	./scanf_contract_test_asan
.else
	@echo "check-libc-scanf: ${HOSTCC} has no address sanitizer; overrun run skipped" >&2
.endif

scanf_contract_test: scanf_contract_test.c ${SCANF_OBJS}
	${HOSTCC} ${SCANF_CFLAGS} -o $@ scanf_contract_test.c ${SCANF_OBJS}

scanf_contract_test32: scanf_contract_test.c ${SCANF_OBJS32}
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -o $@ scanf_contract_test.c \
	    ${SCANF_OBJS32}

scanf_contract_test_asan: scanf_contract_test.c ${SCANF_OBJS_ASAN}
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -o $@ scanf_contract_test.c \
	    ${SCANF_OBJS_ASAN}

scanf_doscan.o: ${SCANF_DIR}/doscan.c
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

scanf_doscan32.o: ${SCANF_DIR}/doscan.c
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

scanf_doscan_asan.o: ${SCANF_DIR}/doscan.c
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
	./rwmode_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} rwmode_contract_test32
	./rwmode_contract_test32
.else
	@echo "check-libc-rwmode: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
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

check-strtox: strtox_test
	./strtox_test
.if ${ILP32_OK} == "yes"
	@${MAKE} strtox_test32
	./strtox_test32
.else
	@echo "check-libc-strtox: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
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
	./malloc_test
.if ${ILP32_OK} == "yes"
	@${MAKE} malloc_test32
	./malloc_test32
.else
	@echo "check-libc-malloc: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
.endif

# The host sort gate, which the root Makefile reaches as check-libc-qsort.
check-qsort: qsort_test
	./qsort_test
.if ${ILP32_OK} == "yes"
	@${MAKE} qsort_test32
	./qsort_test32
.else
	@echo "check-libc-qsort: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
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
	./random_contract_test
	./random_contract_ubsan
	./random_metadata_test
	@if ./random_metadata_bad_test >/dev/null 2>&1; then \
		echo "random metadata negative control passed" >&2; \
		exit 1; \
	fi
	@echo "random metadata negative control rejected"
.if ${ILP32_OK} == "yes"
	@${MAKE} random_contract_test32
	./random_contract_test32
.else
	@echo "check-libc-random: ${HOSTCC} cannot build 32-bit binaries; ILP32 run skipped" >&2
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

header_contract.o: header_contract.c
	${HOSTCC} ${CFLAGS} -ffreestanding -c -o $@ header_contract.c

clean:
	rm -f ${PROG} raise.o ctermid.o header_contract.o
	rm -f asctime_contract_test asctime_contract_bad_test \
		asctime_asctime.o asctime_bad.o
	rm -f ctime_contract_test ctime_contract_test32 ctime_ctime.o ctime_ctime32.o
	rm -f zone_contract_test zone_contract_test32 \
		zone_ctime.o zone_ctime32.o zone_timezone.o zone_timezone32.o
	rm -f zone_contract_test_san zone_ctime_san.o zone_timezone_san.o
	rm -f asctime_target.o asctime_target_diagnostic.o ctime_target.o \
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
	rm -f printf_doprnt_llong.o printf_doprnt_llong32.o
	rm -f syslog_contract_test syslog_syslog.o
	rm -f vis_contract_test vis_vis.o
	rm -f string_security_test string_explicit_bzero.o \
		string_timingsafe_bcmp.o string_timingsafe_bcmp_target.o \
		string_timingsafe_bcmp_bad_target.o
	rm -f qsort_test qsort_test32 qsort_sort.o qsort_sort32.o
	rm -f random_contract_test random_contract_test32 random_contract_ubsan \
		random_metadata_test random_metadata_bad_test random_random.o \
		random_random32.o random_random_ubsan.o random_target.o
	rm -f strtox_test strtox_test32 ${STRTOX_OBJS} ${STRTOX_OBJS32}
	rm -f rwmode_contract_test rwmode_contract_test32 ${RWMODE_OBJS} ${RWMODE_OBJS32}
	rm -f scanf_contract_test scanf_contract_test32 scanf_contract_test_asan \
		${SCANF_OBJS} ${SCANF_OBJS32} ${SCANF_OBJS_ASAN}

all: check

.PHONY: all check check-host check-aout check-asctime check-ctime \
	check-ctime-cross check-difftime check-difftime-cross \
	check-runtime-limits check-runtime-limits-cross \
	check-malloc check-printf check-qsort \
	check-random check-random-cross \
	check-printf-float check-rwmode check-scanf check-string-security \
	check-string-security-cross check-strtox check-syslog check-vis clean
