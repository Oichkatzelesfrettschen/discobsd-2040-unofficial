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

# Each negative control restores one defect the library carried, a narrow
# width, a sentinel, the command separator or the inverted regexp result,
# and must fail the case named after it. No control restores the -nocase
# copy's early free: the arithmetic on that freed pointer is undefined but
# returns the same offsets, so the regexp-result case covers the path only.
NEGATIVES=	command_separator regexp_result regsub_empty \
		regsub_nocase_tail regsub_bol var_extent append_extent \
		parse_extent expr_extent list_end upvar_count pattern_cache \
		regexp_backtrack regexp_program regexp_substitute
MUTATE.command_separator= \
	tclbasic.c "s/case ';':/case ':':/"
MUTATE.regexp_result= \
	tclcmdmz.c 's/if (!match) {/if (match) {/'
MUTATE.regsub_empty= \
	tclcmdmz.c 's/ \&\& (\*p != 0)) {/ \&\& (*p != 0) \&\& 0) {/'
MUTATE.regsub_nocase_tail= \
	tclcmdmz.c '/the original, not a -nocase copy/d'
MUTATE.regsub_bol= \
	tclcmdmz.c 's/regexp_execute_at (regexpPtr, p, string)/regexp_execute (regexpPtr, p)/'
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
MUTATE.upvar_count= \
	internal.h 's/^    int upvarUses;/    unsigned short upvarUses;/'
MUTATE.pattern_cache= \
	internal.h 's/^    int patLengths /    unsigned short patLengths /'
MUTATE.regexp_substitute= \
	regsub.c 's/^	size_t len;/	unsigned short len;/'
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
	${TEST_EXECUTION} run libtcl.command-separator.negative -- \
	    ./libtcl_command_separator_bad_test command-separator
	${TEST_EXECUTION} run libtcl.regexp-result.negative -- \
	    ./libtcl_regexp_result_bad_test regexp-result
	${TEST_EXECUTION} run libtcl.regsub-empty.negative -- \
	    ./libtcl_regsub_empty_bad_test regsub-cases
	${TEST_EXECUTION} run libtcl.regsub-nocase-tail.negative -- \
	    ./libtcl_regsub_nocase_tail_bad_test regsub-cases
	${TEST_EXECUTION} run libtcl.regsub-bol.negative -- \
	    ./libtcl_regsub_bol_bad_test regsub-cases
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
	${TEST_EXECUTION} run libtcl.upvar-count.negative -- \
	    ./libtcl_upvar_count_bad_test upvar-count
	${TEST_EXECUTION} run libtcl.pattern-cache.negative -- \
	    ./libtcl_pattern_cache_bad_test pattern-cache
	${TEST_EXECUTION} run libtcl.regexp-substitute.negative -- \
	    ./libtcl_regexp_substitute_bad_test regexp-substitute
	${TEST_EXECUTION} run libtcl.regexp-backtrack.negative -- \
	    ./libtcl_regexp_backtrack_bad_test regexp-backtrack
	${TEST_EXECUTION} run libtcl.regexp-program.negative -- \
	    ./libtcl_regexp_program_bad_test regexp-program
.else
.for variant in libtcl.contracts.sanitized \
	libtcl.command-separator.negative libtcl.regexp-result.negative \
	libtcl.regsub-empty.negative \
	libtcl.regsub-nocase-tail.negative libtcl.regsub-bol.negative \
	libtcl.var-extent.negative \
	libtcl.append-extent.negative libtcl.parse-extent.negative \
	libtcl.expr-extent.negative libtcl.list-end.negative \
	libtcl.upvar-count.negative libtcl.pattern-cache.negative \
	libtcl.regexp-substitute.negative \
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
