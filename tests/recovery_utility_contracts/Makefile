TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PYTHON?=
HOSTCC?= cc
ARM_COMPILER?= ${GCCPREFIX}-gcc
include ${TOPSRC}/tools/test-execution.mk

SANITIZER_FLAGS= -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer
SANITIZER_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} \
	${SANITIZER_FLAGS}
TARGET_CFLAGS= -std=c17 -pedantic-errors -Wold-style-definition \
	-Wall -Wextra -Werror -fno-common \
	-mcpu=cortex-m0plus -mabi=aapcs -mlittle-endian -mthumb \
	-mfloat-abi=soft -nostdinc -I${TOPSRC}/include \
	-I${TOPSRC}/usr.bin/textbox -Os

check: check-native check-sanitized

check-native:
	: "$${PYTHON:?set PYTHON to the intended interpreter}"
	${TEST_EXECUTION} run utility.recovery.native -- \
	    sh ./run.sh native "${HOSTCC}" "${TOPSRC}"

check-sanitized:
.if ${SANITIZER_OK} == "yes"
	: "$${PYTHON:?set PYTHON to the intended interpreter}"
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run utility.recovery.sanitized -- \
	    sh ./run.sh sanitized "${HOSTCC}" "${TOPSRC}"
.else
	${TEST_EXECUTION} skip utility.recovery.sanitized \
	    "recovery utility contracts: sanitizer runtime unavailable"
.endif

check-cross: readlink_target.o realpath_target.o uudecode_target.o \
	check-realpath-stack

readlink_target.o: ${TOPSRC}/usr.bin/readlink/readlink.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${.ALLSRC}

realpath_target.o: ${TOPSRC}/usr.bin/realpath/realpath.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${.ALLSRC}

realpath_stack.o: ${TOPSRC}/usr.bin/realpath/realpath.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -fstack-usage -c -o $@ ${.ALLSRC}

known_bad_stack.o: known_bad_stack.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -fstack-usage -c -o $@ ${.ALLSRC}

check-realpath-stack: realpath_stack.o known_bad_stack.o check-stack-verifier
	: "$${PYTHON:?set PYTHON to the intended interpreter}"
	${PYTHON} ./check_stack_usage.py --maximum-bytes 704 \
	    --production realpath_stack.su --known-bad known_bad_stack.su

check-stack-verifier:
	: "$${PYTHON:?set PYTHON to the intended interpreter}"
	${PYTHON} ./check_stack_usage_test.py

uudecode_target.o: ${TOPSRC}/usr.bin/uudecode/uudecode.c
	${ARM_COMPILER} ${TARGET_CFLAGS} -c -o $@ ${.ALLSRC}

clean:
	rm -f readlink_target.o realpath_target.o realpath_stack.o \
	    realpath_stack.su uudecode_target.o known_bad_stack.o \
	    known_bad_stack.su

.PHONY: check check-native check-sanitized check-cross \
	check-realpath-stack check-stack-verifier clean
