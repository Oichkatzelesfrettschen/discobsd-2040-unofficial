TOPSRC!= cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
ARM_COMPILER?= ${GCCPREFIX}-gcc
TARGET_MACHINE?= rp2040
PROG= libc_contracts_test
TARGET_DIR= ../../lib/libc/gen
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

# The target's size_t, int and pointers are four bytes; -m32 on an x86-64
# host is that width, so the same test runs there when the compiler can.
ILP32=		-m32
ILP32_OK!=	printf 'int main(void){return 0;}\n' > /tmp/ilp32probe$$$$.c; \
		if ${HOSTCC} -m32 -o /dev/null /tmp/ilp32probe$$$$.c 2>/dev/null; \
		then echo yes; else echo no; fi; \
		rm -f /tmp/ilp32probe$$$$.c

check: ${PROG} header_contract.o check-malloc check-printf check-scanf \
	check-syslog check-vis check-qsort check-strtox check-aout
	./${PROG}

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
		printf_flsbuf.o
PRINTF_OBJS32=	printf_snprintf32.o printf_vsnprintf32.o printf_doprnt32.o \
		printf_flsbuf32.o

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
SYSLOG_FORMAT_OBJS=	printf_doprnt.o printf_flsbuf.o

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

check-vis: vis_contract_test
	./vis_contract_test

vis_contract_test: vis_contract_test.c vis_vis.o
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -fsigned-char -o $@ \
	    vis_contract_test.c vis_vis.o

vis_vis.o: ${TARGET_DIR}/vis.c
	${HOSTCC} ${VIS_CFLAGS} -c -o $@ ${TARGET_DIR}/vis.c

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
	${HOSTCC} ${SCANF_CFLAGS} -Wno-deprecated-non-prototype \
	    -c -o $@ ${SCANF_DIR}/filbuf.c

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
	${HOSTCC} ${ILP32} ${SCANF_CFLAGS} -Wno-deprecated-non-prototype \
	    -c -o $@ ${SCANF_DIR}/filbuf.c

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
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -Wno-deprecated-non-prototype \
	    -c -o $@ ${SCANF_DIR}/filbuf.c

scanf_strtod_asan.o: ${SCANF_STDLIB_DIR}/strtod.c
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_STDLIB_DIR}/strtod.c

scanf_ctype_asan.o: ${SCANF_GEN_DIR}/ctype_.c
	${HOSTCC} ${ASAN} ${SCANF_CFLAGS} -c -o $@ ${SCANF_GEN_DIR}/ctype_.c

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
	${MAKE} -C ${TOPSRC} MACHINE=${TARGET_MACHINE} cleantools
	${MAKE} -C ${TOPSRC} MACHINE=${TARGET_MACHINE} symlinks tools
	${MAKE} -C ${TOPSRC}/lib/libc_aout/libc MACHINE=${TARGET_MACHINE}
	${MAKE} -C ${TOPSRC} MACHINE=${TARGET_MACHINE} \
	    ${TOPSRC}/distrib/obj/boardlibc.${TARGET_MACHINE}/libc.a
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
	rm -f malloc_test malloc_test32 ${MALLOC_OBJS} ${MALLOC_OBJS32}
	rm -f printf_contract_test printf_contract_test32 ${PRINTF_OBJS} ${PRINTF_OBJS32}
	rm -f syslog_contract_test syslog_syslog.o
	rm -f vis_contract_test vis_vis.o
	rm -f qsort_test qsort_test32 qsort_sort.o qsort_sort32.o
	rm -f strtox_test strtox_test32 ${STRTOX_OBJS} ${STRTOX_OBJS32}
	rm -f scanf_contract_test scanf_contract_test32 scanf_contract_test_asan \
		${SCANF_OBJS} ${SCANF_OBJS32} ${SCANF_OBJS_ASAN}

all: check

.PHONY: all check check-aout check-malloc check-printf check-qsort check-scanf \
	check-strtox check-syslog check-vis clean
