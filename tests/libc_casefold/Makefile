TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
HOSTCC?= cc
ILP32_CC= ${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk
SOURCE= ${TOPSRC}/lib/libc/gen/strcasecmp.c
BASE_FLAGS= -std=c17 -pedantic-errors -Wall -Wextra -Werror \
    -Wold-style-definition -fno-builtin \
    -Dstrcasecmp=discobsd_strcasecmp -Dstrncasecmp=discobsd_strncasecmp
TARGET_C17_FLAGS= -std=c17 -pedantic-errors -Wall -Wextra -Werror \
    -Wold-style-definition \
    -Os -ffreestanding -fno-builtin -mcpu=cortex-m0plus -mthumb \
    -mfloat-abi=soft
SAN_FLAGS= -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer
SAN_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} ${SAN_FLAGS}
MUTATIONS= high_bytes signed_order count_early errno_loss
MUTANT_BINS= ${MUTATIONS:S/^/casefold_/:S/$/.mutant.test/}

check: check-mutants
check: check-signed
casefold_signed: casefold_test.c ${SOURCE}
	${HOSTCC} ${BASE_FLAGS} -fsigned-char -o $@ ${SOURCE} casefold_test.c

casefold_signed32: casefold_test.c ${SOURCE}
	${HOSTCC} -m32 ${BASE_FLAGS} -fsigned-char -o $@ ${SOURCE} casefold_test.c

casefold_signed_san: casefold_test.c ${SOURCE}
	${HOSTCC} ${BASE_FLAGS} ${SAN_FLAGS} -fsigned-char -o $@ ${SOURCE} casefold_test.c

check-signed: casefold_signed
	${TEST_EXECUTION} run libc.casefold.signed.native -- ./casefold_signed
.if ${ILP32_OK} == "yes"
	@${MAKE} casefold_signed32
	${TEST_EXECUTION} run libc.casefold.signed.ilp32 -- ./casefold_signed32
.else
	${TEST_EXECUTION} skip libc.casefold.signed.ilp32 "casefold: ILP32 runtime unavailable"
.endif
.if ${SAN_OK} == "yes"
	@${MAKE} casefold_signed_san
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run libc.casefold.signed.sanitized -- ./casefold_signed_san
.else
	${TEST_EXECUTION} skip libc.casefold.signed.sanitized "casefold: sanitizer runtime unavailable"
.endif
check: check-unsigned
casefold_unsigned: casefold_test.c ${SOURCE}
	${HOSTCC} ${BASE_FLAGS} -funsigned-char -o $@ ${SOURCE} casefold_test.c

casefold_unsigned32: casefold_test.c ${SOURCE}
	${HOSTCC} -m32 ${BASE_FLAGS} -funsigned-char -o $@ ${SOURCE} casefold_test.c

casefold_unsigned_san: casefold_test.c ${SOURCE}
	${HOSTCC} ${BASE_FLAGS} ${SAN_FLAGS} -funsigned-char -o $@ ${SOURCE} casefold_test.c

check-unsigned: casefold_unsigned
	${TEST_EXECUTION} run libc.casefold.unsigned.native -- ./casefold_unsigned
.if ${ILP32_OK} == "yes"
	@${MAKE} casefold_unsigned32
	${TEST_EXECUTION} run libc.casefold.unsigned.ilp32 -- ./casefold_unsigned32
.else
	${TEST_EXECUTION} skip libc.casefold.unsigned.ilp32 "casefold: ILP32 runtime unavailable"
.endif
.if ${SAN_OK} == "yes"
	@${MAKE} casefold_unsigned_san
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run libc.casefold.unsigned.sanitized -- ./casefold_unsigned_san
.else
	${TEST_EXECUTION} skip libc.casefold.unsigned.sanitized "casefold: sanitizer runtime unavailable"
.endif

casefold_mutants.stamp: casefold_mutate.py ${SOURCE}
	${PYTHON} casefold_mutate.py ${SOURCE} ${.CURDIR}
	touch $@
.for mutation in ${MUTATIONS}
casefold_${mutation}.mutant.test: casefold_mutants.stamp casefold_test.c
	${HOSTCC} ${BASE_FLAGS} -fsigned-char -o $@ \
	    casefold_${mutation}.mutant.c casefold_test.c
.endfor
check-mutants: ${MUTANT_BINS} check_mutants.sh
	${TEST_EXECUTION} run libc.casefold.negative -- sh ./check_mutants.sh

casefold_target.o: ${SOURCE}
	${GCCPREFIX}-gcc ${TARGET_C17_FLAGS} -nostdinc -I${TOPSRC}/include \
	    -c ${SOURCE} -o $@
check-cross-strict-negative: gnu_statement_expression.c.in
	@rm -f gnu_statement_expression.o gnu_statement_expression.log
	@if ${GCCPREFIX}-gcc ${TARGET_C17_FLAGS} -x c -c \
	    gnu_statement_expression.c.in -o gnu_statement_expression.o \
	    >gnu_statement_expression.log 2>&1; then \
		echo "target strict-C17 negative control compiled a GNU statement expression" >&2; \
		exit 1; \
	fi
	@grep -E 'braced-group|statement expression|pedantic' \
	    gnu_statement_expression.log >/dev/null || { \
		echo "target strict-C17 negative control lacked the expected diagnostic" >&2; \
		cat gnu_statement_expression.log >&2; \
		exit 1; \
	}
	@echo "target strict-C17 negative control rejected a GNU statement expression"
check-cross: check-cross-strict-negative casefold_target.o check_object.py
	${PYTHON} check_object.py --self-test
	${PYTHON} check_object.py ${GCCPREFIX}-nm ${GCCPREFIX}-size casefold_target.o

clean:
	rm -f casefold_signed casefold_unsigned casefold_signed32 casefold_unsigned32 \
	    casefold_signed_san casefold_unsigned_san casefold_target.o \
	    gnu_statement_expression.o gnu_statement_expression.log \
	    casefold_*.mutant.c casefold_*.mutant.test casefold_*.mutant.log \
	    casefold_mutants.stamp

all: check
.PHONY: all check check-signed check-unsigned check-mutants check-cross \
    check-cross-strict-negative clean
