TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
ARM_COMPILER?=	${GCCPREFIX}-gcc
TARGET_DIR=	${TOPSRC}/lib/libc/gen
STREAM_RENAMES=	-Dopendir=discobsd_opendir -Dreaddir=discobsd_readdir \
		-Dclosedir=discobsd_closedir -Dseekdir=discobsd_seekdir \
		-Dtelldir=discobsd_telldir -Dopen=test_open \
		-Dclose=test_close -Dread=test_read -Dlseek=test_lseek \
		-Dfstat=test_fstat -Dfcntl=test_fcntl \
		-Dmalloc=test_malloc -Dfree=test_free \
		-Derrno=dirent_test_errno
HOST_BASE_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
		-fno-builtin -nostdinc -isystem ${TOPSRC}/include
HOST_CFLAGS=	${HOST_BASE_CFLAGS} ${STREAM_RENAMES}
TARGET_CFLAGS=	-std=c17 -Wall -Wextra -Werror -Wold-style-definition \
		-fno-common -mcpu=cortex-m0plus \
		-mabi=aapcs -mlittle-endian -mthumb -mfloat-abi=soft \
		-nostdinc -I${TOPSRC}/include -Os -ffreestanding \
		-fno-builtin
HOST_OBJS=	dirent_opendir.o dirent_readdir.o dirent_closedir.o \
		dirent_seekdir.o dirent_telldir.o
TARGET_OBJS=	dirent_opendir_target.o dirent_readdir_target.o \
		dirent_closedir_target.o dirent_seekdir_target.o \
		dirent_telldir_target.o dirent_layout_target.o

ILP32=		-m32
ILP32_CC=	${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk

SCANDIR_RENAMES=	-Dscandir=discobsd_scandir \
		-Dalphasort=discobsd_alphasort \
		-Dopendir=test_opendir -Dreaddir=test_readdir \
		-Dclosedir=test_closedir -Dfstat=test_fstat \
		-Dmalloc=test_malloc -Drealloc=test_realloc -Dfree=test_free \
		-Derrno=dirent_test_errno
SCANDIR_CFLAGS=	${HOST_BASE_CFLAGS} ${SCANDIR_RENAMES}
SCANDIR_TEST_CFLAGS=	${HOST_BASE_CFLAGS}
SCANDIR_SAN_FLAGS=	-fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-omit-frame-pointer
SCANDIR_SAN_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native \
		${HOSTCC} ${SCANDIR_SAN_FLAGS}
SCANDIR_MUTATIONS=	read_error_loss cleanup_errno_loss leak_record \
		metadata_growth source_reclen shared_comparator
SCANDIR_MUTANT_BINS=	${SCANDIR_MUTATIONS:S/^/scandir_/:S/$/.mutant.test/}

check: check-stream check-scandir

STREAM_OBJS32=	${HOST_OBJS:S/.o/32.o/}
STREAM_OBJS_SAN=	${HOST_OBJS:S/.o/_san.o/}
OPENDIR_MUTATIONS=	directory_type close_on_exec fcntl_error_loss \
		descriptor_cleanup cleanup_errno_loss
OPENDIR_MUTANT_BINS=	${OPENDIR_MUTATIONS:S/^/opendir_/:S/$/.mutant.test/}
OPENDIR_MUTANT_STREAM_OBJS=	${HOST_OBJS:Ndirent_opendir.o}

check-stream: dirent_contract_test check-opendir-mutants check-closedir-mutants
	${TEST_EXECUTION} run libc.dirent.native -- ./dirent_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} dirent_contract_test32
	${TEST_EXECUTION} run libc.dirent.ilp32 -- ./dirent_contract_test32
.else
	${TEST_EXECUTION} skip libc.dirent.ilp32 \
	    "check-dirent-contracts: ${HOSTCC} cannot execute ILP32 binaries"
.endif
.if ${SCANDIR_SAN_OK} == "yes"
	@${MAKE} dirent_contract_test_san
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	    UBSAN_OPTIONS=halt_on_error=1 ${TEST_EXECUTION} \
	    run libc.dirent.sanitized -- ./dirent_contract_test_san
.else
	${TEST_EXECUTION} skip libc.dirent.sanitized \
	    "check-dirent-contracts: ${HOSTCC} cannot execute address/undefined sanitizers"
.endif

dirent_contract_test32: dirent_contract_test.c ${STREAM_OBJS32}
	${HOSTCC} ${ILP32} ${HOST_CFLAGS} -o $@ dirent_contract_test.c ${STREAM_OBJS32}

dirent_contract_test_san: dirent_contract_test.c ${STREAM_OBJS_SAN}
	${HOSTCC} ${SCANDIR_SAN_FLAGS} ${HOST_CFLAGS} -o $@ \
	    dirent_contract_test.c ${STREAM_OBJS_SAN}

.for source_name in opendir readdir closedir seekdir telldir
dirent_${source_name}32.o: ${TARGET_DIR}/${source_name}.c
	${HOSTCC} ${ILP32} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/${source_name}.c

dirent_${source_name}_san.o: ${TARGET_DIR}/${source_name}.c
	${HOSTCC} ${SCANDIR_SAN_FLAGS} ${HOST_CFLAGS} -c -o $@ \
	    ${TARGET_DIR}/${source_name}.c
.endfor

opendir_mutants.stamp: opendir_mutate.py ${TARGET_DIR}/opendir.c
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for opendir mutations" >&2; \
		exit 2; \
	fi
	${PYTHON} opendir_mutate.py ${TARGET_DIR}/opendir.c ${.CURDIR}
	@touch $@

.for mutation in ${OPENDIR_MUTATIONS}
opendir_${mutation}.mutant.o: opendir_mutants.stamp
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ opendir_${mutation}.mutant.c

opendir_${mutation}.mutant.test: dirent_contract_test.c \
    opendir_${mutation}.mutant.o ${OPENDIR_MUTANT_STREAM_OBJS}
	${HOSTCC} ${HOST_CFLAGS} -o $@ dirent_contract_test.c \
	    opendir_${mutation}.mutant.o ${OPENDIR_MUTANT_STREAM_OBJS}
.endfor

check-opendir-mutants: ${OPENDIR_MUTANT_BINS} check_opendir_mutants.sh
	${TEST_EXECUTION} run libc.dirent.negative -- sh ./check_opendir_mutants.sh

dirent_contract_test: dirent_contract_test.c ${HOST_OBJS}
	${HOSTCC} ${HOST_CFLAGS} -o $@ dirent_contract_test.c ${HOST_OBJS}

dirent_opendir.o: ${TARGET_DIR}/opendir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/opendir.c

dirent_readdir.o: ${TARGET_DIR}/readdir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/readdir.c

dirent_closedir.o: ${TARGET_DIR}/closedir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/closedir.c

dirent_seekdir.o: ${TARGET_DIR}/seekdir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/seekdir.c

dirent_telldir.o: ${TARGET_DIR}/telldir.c
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ ${TARGET_DIR}/telldir.c

check-cross: ${TARGET_OBJS} check_dirent_imports.sh
	sh ./check_dirent_imports.sh --self-test
	sh ./check_dirent_imports.sh ${GCCPREFIX}-nm \
	    dirent_opendir_target.o dirent_telldir_target.o

dirent_layout_target.o: dirent_layout.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ dirent_layout.c

.for source_name in opendir readdir closedir seekdir telldir
dirent_${source_name}_target.o: ${TARGET_DIR}/${source_name}.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ \
	    ${TARGET_DIR}/${source_name}.c
.endfor

check-scandir: scandir_contract_test check-scandir-mutants
	${TEST_EXECUTION} run libc.scandir.native -- ./scandir_contract_test
.if ${ILP32_OK} == "yes"
	@${MAKE} scandir_contract_test32
	${TEST_EXECUTION} run libc.scandir.ilp32 -- ./scandir_contract_test32
.else
	${TEST_EXECUTION} skip libc.scandir.ilp32 \
	    "check-scandir-contracts: ${HOSTCC} cannot compile and execute ILP32 binaries"
.endif
.if ${SCANDIR_SAN_OK} == "yes"
	@${MAKE} scandir_contract_test_san
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=0 \
	    UBSAN_OPTIONS=halt_on_error=1 ${TEST_EXECUTION} \
	    run libc.scandir.sanitized -- ./scandir_contract_test_san
.else
	${TEST_EXECUTION} skip libc.scandir.sanitized \
	    "check-scandir-contracts: ${HOSTCC} cannot execute address/undefined sanitizers"
.endif

check-scandir-mutants: ${SCANDIR_MUTANT_BINS} check_scandir_mutants.sh
	${TEST_EXECUTION} run libc.scandir.negative -- sh ./check_scandir_mutants.sh

scandir_contract_test: scandir_contract_test.c scandir_scandir.o \
    scandir_alloc_shim.o
	${HOSTCC} ${SCANDIR_TEST_CFLAGS} -o $@ scandir_contract_test.c \
	    scandir_scandir.o scandir_alloc_shim.o

scandir_contract_test32: scandir_contract_test.c scandir_scandir32.o \
    scandir_alloc_shim32.o
	${HOSTCC} ${ILP32} ${SCANDIR_TEST_CFLAGS} -o $@ \
	    scandir_contract_test.c scandir_scandir32.o scandir_alloc_shim32.o

scandir_contract_test_san: scandir_contract_test.c scandir_scandir_san.o \
    scandir_alloc_shim_san.o
	${HOSTCC} ${SCANDIR_SAN_FLAGS} ${SCANDIR_TEST_CFLAGS} -o $@ \
	    scandir_contract_test.c scandir_scandir_san.o scandir_alloc_shim_san.o

scandir_scandir.o: ${TARGET_DIR}/scandir.c
	${HOSTCC} ${SCANDIR_CFLAGS} -c -o $@ ${TARGET_DIR}/scandir.c

scandir_scandir32.o: ${TARGET_DIR}/scandir.c
	${HOSTCC} ${ILP32} ${SCANDIR_CFLAGS} -c -o $@ ${TARGET_DIR}/scandir.c

scandir_scandir_san.o: ${TARGET_DIR}/scandir.c
	${HOSTCC} ${SCANDIR_SAN_FLAGS} ${SCANDIR_CFLAGS} -c -o $@ \
	    ${TARGET_DIR}/scandir.c

scandir_alloc_shim.o: scandir_alloc_shim.c scandir_alloc_shim.h
	${HOSTCC} -std=c17 -Wall -Wextra -Werror -c -o $@ scandir_alloc_shim.c

scandir_alloc_shim32.o: scandir_alloc_shim.c scandir_alloc_shim.h
	${HOSTCC} ${ILP32} -std=c17 -Wall -Wextra -Werror -c -o $@ \
	    scandir_alloc_shim.c

scandir_alloc_shim_san.o: scandir_alloc_shim.c scandir_alloc_shim.h
	${HOSTCC} ${SCANDIR_SAN_FLAGS} -std=c17 -Wall -Wextra -Werror \
	    -c -o $@ scandir_alloc_shim.c

scandir_mutants.stamp: scandir_mutate.py ${TARGET_DIR}/scandir.c
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for scandir mutations" >&2; \
		exit 2; \
	fi
	${PYTHON} scandir_mutate.py ${TARGET_DIR}/scandir.c ${.CURDIR}
	@touch $@

.for mutation in ${SCANDIR_MUTATIONS}
scandir_${mutation}.mutant.o: scandir_mutants.stamp
	${HOSTCC} ${SCANDIR_CFLAGS} -c -o $@ \
	    scandir_${mutation}.mutant.c

scandir_${mutation}.mutant.test: scandir_contract_test.c \
    scandir_${mutation}.mutant.o scandir_alloc_shim.o
	${HOSTCC} ${SCANDIR_TEST_CFLAGS} -o $@ scandir_contract_test.c \
	    scandir_${mutation}.mutant.o scandir_alloc_shim.o
.endfor

check-scandir-cross: scandir_scandir_target.o check_scandir_imports.sh
	sh ./check_scandir_imports.sh --self-test
	sh ./check_scandir_imports.sh ${GCCPREFIX}-nm scandir_scandir_target.o

scandir_scandir_target.o: ${TARGET_DIR}/scandir.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${TARGET_DIR}/scandir.c

clean:
	rm -f ${CLOSEDIR_MUTANT_BINS} closedir_*.mutant.c \
	    closedir_*.mutant.o closedir_*.mutant.log closedir_mutants.stamp
	rm -f dirent_contract_test ${HOST_OBJS} ${TARGET_OBJS}
	rm -f dirent_contract_test32 dirent_contract_test_san \
	    ${STREAM_OBJS32} ${STREAM_OBJS_SAN} ${OPENDIR_MUTANT_BINS}
	rm -f opendir_*.mutant.c opendir_*.mutant.o opendir_*.mutant.log \
	    opendir_mutants.stamp
	rm -f scandir_contract_test scandir_contract_test32 \
		scandir_contract_test_san scandir_scandir.o scandir_scandir32.o \
		scandir_scandir_san.o scandir_scandir_target.o \
		scandir_alloc_shim.o scandir_alloc_shim32.o \
		scandir_alloc_shim_san.o ${SCANDIR_MUTANT_BINS}
	rm -f scandir_*.mutant.c scandir_*.mutant.o scandir_*.mutant.log \
		scandir_mutants.stamp

all: check

.PHONY: all check check-stream check-opendir-mutants check-cross check-scandir \
	check-scandir-mutants check-scandir-cross clean

CLOSEDIR_MUTATIONS= result_loss errno_loss free_loss close_repeat
CLOSEDIR_MUTANT_BINS= ${CLOSEDIR_MUTATIONS:S/^/closedir_/:S/$/.mutant.test/}
CLOSEDIR_MUTANT_STREAM_OBJS= ${HOST_OBJS:Ndirent_closedir.o}

closedir_mutants.stamp: closedir_mutate.py ${TARGET_DIR}/closedir.c
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for closedir mutations" >&2; \
		exit 2; \
	fi
	${PYTHON} closedir_mutate.py ${TARGET_DIR}/closedir.c ${.CURDIR}
	@touch $@

.for mutation in ${CLOSEDIR_MUTATIONS}
closedir_${mutation}.mutant.o: closedir_mutants.stamp
	${HOSTCC} ${HOST_CFLAGS} -c -o $@ closedir_${mutation}.mutant.c

closedir_${mutation}.mutant.test: dirent_contract_test.c \
    closedir_${mutation}.mutant.o ${CLOSEDIR_MUTANT_STREAM_OBJS}
	${HOSTCC} ${HOST_CFLAGS} -o $@ dirent_contract_test.c \
	    closedir_${mutation}.mutant.o ${CLOSEDIR_MUTANT_STREAM_OBJS}
.endfor

check-closedir-mutants: ${CLOSEDIR_MUTANT_BINS} check_closedir_mutants.sh
	${TEST_EXECUTION} run libc.closedir.negative -- sh ./check_closedir_mutants.sh

.PHONY: check-closedir-mutants
