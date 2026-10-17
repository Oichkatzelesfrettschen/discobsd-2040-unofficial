TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?= cc
ARM_COMPILER?= ${GCCPREFIX}-gcc
ILP32_CC= ${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk
TARGET_SRC= ${TOPSRC}/usr.bin/printf/printf.c
HOST_CFLAGS= -std=c17 -D_POSIX_C_SOURCE=200809L -DNOFPU \
	-pedantic-errors -Wold-style-definition -Wall -Wextra -Werror
TARGET_CFLAGS= -std=c17 -DNOFPU -pedantic-errors -Wold-style-definition \
	-Wall -Wextra -Werror -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -nostdinc -I${TOPSRC}/include -Os
SANITIZER_FLAGS= -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer
SANITIZER_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} \
	${SANITIZER_FLAGS}

check: printf_host check-sanitized check-oracle
	PYTHON=${PYTHON:Q} ${TEST_EXECUTION} run utility.printf.native -- \
	    sh ${.CURDIR}/run_behavior_test.sh ${.CURDIR}/printf_host

check-oracle:
	@if PYTHON=${PYTHON:Q} sh ${.CURDIR}/run_behavior_test.sh \
	    ${.CURDIR}/known_bad.sh >/dev/null 2>&1; then \
		echo "printf contracts: known-bad fixture passed" >&2; \
		exit 1; \
	fi

check-ilp32:
.if ${ILP32_OK} == "yes"
	@${MAKE} printf_host32
	PYTHON=${PYTHON:Q} ${TEST_EXECUTION} run utility.printf.ilp32 -- \
	    sh ${.CURDIR}/run_behavior_test.sh ${.CURDIR}/printf_host32
.else
	${TEST_EXECUTION} skip utility.printf.ilp32 \
	    "printf contracts: ILP32 runtime unavailable"
.endif

check-sanitized:
.if ${SANITIZER_OK} == "yes"
	@${MAKE} printf_host_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    PYTHON=${PYTHON:Q} ${TEST_EXECUTION} run utility.printf.sanitized -- \
	    sh ${.CURDIR}/run_behavior_test.sh ${.CURDIR}/printf_host_sanitized
.else
	${TEST_EXECUTION} skip utility.printf.sanitized \
	    "printf contracts: sanitizer runtime unavailable"
.endif

printf_host: ${TARGET_SRC}
	${HOSTCC} ${HOST_CFLAGS} -o $@ ${TARGET_SRC}

printf_host32: ${TARGET_SRC}
	${HOSTCC} -m32 ${HOST_CFLAGS} -o $@ ${TARGET_SRC}

printf_host_sanitized: ${TARGET_SRC}
	${HOSTCC} ${HOST_CFLAGS} ${SANITIZER_FLAGS} -o $@ ${TARGET_SRC}

check-cross: printf_target.o

printf_target.o: ${TARGET_SRC}
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${TARGET_SRC}

clean:
	rm -f printf_host printf_host32 printf_host_sanitized printf_target.o

all: check

.PHONY: all check check-ilp32 check-sanitized check-oracle check-cross clean
