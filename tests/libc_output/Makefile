TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
HOSTCC?= cc
ILP32_CC= ${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk
SAN_FLAGS= -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer
SAN_OK!= sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} ${SAN_FLAGS}
SOURCE= ${TOPSRC}/lib/libc/stdio
SOURCES= ${SOURCE}/fprintf.c ${SOURCE}/vfprintf.c ${SOURCE}/fputs.c \
    ${SOURCE}/doprnt.c ${SOURCE}/doprnt_llong.c ${SOURCE}/flsbuf.c ${SOURCE}/printf.c \
    ${SOURCE}/vprintf.c ${SOURCE}/sprintf.c ${SOURCE}/vsprintf.c
BUILD= ${PYTHON} build_fixture.py ${TOPSRC}

output_native: build_fixture.py output_test.c ${SOURCES}
	${BUILD} native --binary output_native --compiler ${HOSTCC:Q}
output_ilp32: build_fixture.py output_test.c ${SOURCES}
	${BUILD} ilp32 --binary output_ilp32 --compiler ${HOSTCC:Q} --flags=-m32
output_sanitized: build_fixture.py output_test.c ${SOURCES}
	${BUILD} sanitized --binary output_sanitized --compiler ${HOSTCC:Q} --flags=${SAN_FLAGS:Q}

check: output_native check-mutants check-arithmetic
	${TEST_EXECUTION} run libc.output.native -- ./output_native
.if ${ILP32_OK} == "yes"
	@${MAKE} output_ilp32
	${TEST_EXECUTION} run libc.output.ilp32 -- ./output_ilp32
.else
	${TEST_EXECUTION} skip libc.output.ilp32 "output: ILP32 runtime unavailable"
.endif
.if ${SAN_OK} == "yes"
	@${MAKE} output_sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST_EXECUTION} run libc.output.sanitized -- ./output_sanitized
.else
	${TEST_EXECUTION} skip libc.output.sanitized "output: sanitizer runtime unavailable"
.endif

MUTANTS= count capacity pointer flush continue
.for mutation in ${MUTANTS}
mutant_${mutation}/test: build_fixture.py output_test.c ${SOURCES}
	${BUILD} mutant_${mutation} --compiler ${HOSTCC:Q} --mutation ${mutation}
.endfor
check-mutants: ${MUTANTS:S/^/mutant_/:S/$/\/test/} check_mutants.sh
	${TEST_EXECUTION} run libc.output.negative -- sh ./check_mutants.sh

check-cross:
.for member in fprintf vfprintf fputs printf vprintf sprintf vsprintf doprnt
	${GCCPREFIX}-gcc -std=c17 -Wall -Wextra -Werror -Wold-style-definition \
	    -Os -ffreestanding -fno-builtin -mcpu=cortex-m0plus -mthumb \
	    -mfloat-abi=soft -nostdinc -I${TOPSRC}/include \
	    -c ${SOURCE}/${member}.c -o ${member}_target.o
.endfor

.PHONY: check check-mutants check-cross

check-arithmetic: check_arithmetic.py check_arithmetic.sh
.if ${SAN_OK} == "yes"
	PYTHON=${PYTHON:Q} HOSTCC=${HOSTCC:Q} ${TEST_EXECUTION} run libc.output.arithmetic -- sh ./check_arithmetic.sh
.else
	${TEST_EXECUTION} skip libc.output.arithmetic "output: sanitizer runtime unavailable"
.endif
.PHONY: check-arithmetic
