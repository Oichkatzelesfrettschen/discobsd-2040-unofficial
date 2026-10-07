TOPSRC!=cd ../..; pwd

HOSTCC?=	cc
ILP32_CC=	${HOSTCC}
include ${TOPSRC}/tools/test-execution.mk

LIBTCL=		${TOPSRC}/lib/libtcl
LIBTCL_SOURCES!=	ls ${LIBTCL}/*.c ${LIBTCL}/*.h ${TOPSRC}/include/tcl/tcl.h

# An overflow past a heap block is a verdict only when something stops at
# the first byte, so the gate runs under the sanitizers or not at all.
SAN=		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-fno-sanitize-recover=all
SAN_OK!=	sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} ${SAN}

# The library keeps WARNLEVEL=legacy in lib/libtcl/Makefile, whose warning
# sites the warning-policy gates count; this gate measures its behavior,
# with the configuration lib/libtcl/Makefile builds. Var keeps its value in
# the trailing union member string[4] and NewVar() allocates valueSpace
# bytes behind it, so clang's bounds check, unlike GCC's, reports every
# value longer than three bytes. The library is built without that check;
# the address sanitizer still bounds each access by its real allocation.
LIB_CFLAGS=	-std=c17 -D_DEFAULT_SOURCE -w -g -O1 ${SAN} \
		-fno-sanitize=bounds -DTCL_FILE_CMDS -DTCL_ENV_CMDS
TEST_CFLAGS=	-std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
		-g -O1 ${SAN}

# Each negative control restores one narrow width or sentinel the library
# carried, and must fail the case named after it.
NEGATIVES=	var_extent append_extent parse_extent expr_extent list_end \
		regexp_backtrack regexp_program
MUTATE.var_extent= \
	internal.h 's/^    int valueLength;/    unsigned short valueLength;/' \
	internal.h 's/^    int valueSpace;/    unsigned short valueSpace;/'
MUTATE.append_extent= \
	internal.h 's/^    int appendAvl;/    unsigned short appendAvl;/' \
	internal.h 's/^    int appendUsed;/    unsigned short appendUsed;/'
MUTATE.parse_extent= \
	internal.h 's/int needed)/unsigned short needed)/g' \
	tclparse.c 's/int needed)/unsigned short needed)/'
MUTATE.expr_extent= \
	tclexpr.c 's/int length, space;/unsigned short length, space;/'
MUTATE.list_end= \
	tclutil.c 's/indexPtr = INT_MAX;/indexPtr = 32767;/' \
	tclcmdil.c 's/last = INT_MAX;/last = 30000;/'
MUTATE.regexp_backtrack= \
	regexp.c 's/^				int no, min;/				unsigned short no, min;/'
MUTATE.regexp_program= \
	regexp.c 's/unsigned int	size;/unsigned short	size;/' \
	regexp.c 's/x.size > REGEXP_MAX_PROGRAM/0/'

BAD_PROGRAMS=	${NEGATIVES:S/^/libtcl_/:S/$/_bad_test/}

check:
.if ${SAN_OK} == "yes"
	@${MAKE} libtcl_contract_test ${BAD_PROGRAMS}
	${TEST_EXECUTION} run libtcl.contracts.sanitized -- ./libtcl_contract_test
	${TEST_EXECUTION} run libtcl.var-extent.negative -- \
	    ./libtcl_var_extent_bad_test var-extent
	${TEST_EXECUTION} run libtcl.append-extent.negative -- \
	    ./libtcl_append_extent_bad_test append-extent
	${TEST_EXECUTION} run libtcl.parse-extent.negative -- \
	    ./libtcl_parse_extent_bad_test parse-extent
	${TEST_EXECUTION} run libtcl.expr-extent.negative -- \
	    ./libtcl_expr_extent_bad_test expr-extent
	${TEST_EXECUTION} run libtcl.list-end.negative -- \
	    ./libtcl_list_end_bad_test list-end
	${TEST_EXECUTION} run libtcl.regexp-backtrack.negative -- \
	    ./libtcl_regexp_backtrack_bad_test regexp-backtrack
	${TEST_EXECUTION} run libtcl.regexp-program.negative -- \
	    ./libtcl_regexp_program_bad_test regexp-program
.else
.for variant in libtcl.contracts.sanitized libtcl.var-extent.negative \
	libtcl.append-extent.negative libtcl.parse-extent.negative \
	libtcl.expr-extent.negative libtcl.list-end.negative \
	libtcl.regexp-backtrack.negative libtcl.regexp-program.negative
	${TEST_EXECUTION} skip ${variant} "libtcl_contracts: ${HOSTCC} has no address and undefined sanitizer runtime"
.endfor
.endif

libtcl_contract_test: libtcl_contract_test.c lib-good/libtcl.a
	${HOSTCC} ${TEST_CFLAGS} -Ilib-good/include -Ilib-good/src -o $@ \
	    libtcl_contract_test.c lib-good/libtcl.a

lib-good/libtcl.a: ${LIBTCL_SOURCES} build-libtcl.sh
	sh ./build-libtcl.sh lib-good ${TOPSRC} ${HOSTCC:Q} ${LIB_CFLAGS:Q}

.for n in ${NEGATIVES}
libtcl_${n}_bad_test: libtcl_contract_test.c ${LIBTCL_SOURCES} build-libtcl.sh
	sh ./build-libtcl.sh lib-${n} ${TOPSRC} ${HOSTCC:Q} ${LIB_CFLAGS:Q} \
	    ${MUTATE.${n}}
	${HOSTCC} ${TEST_CFLAGS} -Ilib-${n}/include -Ilib-${n}/src -o $@ \
	    libtcl_contract_test.c lib-${n}/libtcl.a
.endfor

clean:
	rm -rf libtcl_contract_test ${BAD_PROGRAMS} lib-good ${NEGATIVES:S/^/lib-/}

all: check

.PHONY: all check clean
