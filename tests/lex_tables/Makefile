TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

HOSTCC?=	cc
LEXDIR=		${TOPSRC}/usr.bin/lex
LEXSRC=		${LEXDIR}/parser.y ${LEXDIR}/lmain.c ${LEXDIR}/sub1.c \
		${LEXDIR}/sub2.c ${LEXDIR}/header.c ${LEXDIR}/ldefs.h
EXAMPLES=	--examples ${TOPSRC}/share/examples/lex \
		--examples ${TOPSRC}/games/atc
# SMALL selects the table sizes the target program is built with. The host
# <paths.h> has no _PATH_SHARE; the test runs lex in a directory that holds
# share/misc/ncform. The sources are pre-C17 lex; their diagnostics belong to
# usr.bin/lex, where the legacy warning level governs them.
LEX_CFLAGS=	-O1 -g -w -DSMALL -D_PATH_SHARE='"share/"' -I${LEXDIR}
TEST=		${PYTHON} -I lex_tables_test.py

all: check

# tools/host-test.mk supplies the sanitizer probe.
include ${TOPSRC}/tools/host-test.mk

lex_host.y.c lex_san.y.c: ${LEXDIR}/parser.y
	${YACC} -o $@ ${LEXDIR}/parser.y

lex_host: lex_host.y.c ${LEXSRC}
	${HOSTCC} ${LEX_CFLAGS} -o $@ lex_host.y.c ${LEXDIR}/lmain.c \
	    ${LEXDIR}/sub1.c ${LEXDIR}/sub2.c ${LEXDIR}/header.c

lex_san: lex_san.y.c ${LEXSRC}
	${HOSTCC} ${LEX_CFLAGS} ${SAN_FLAGS} -o $@ lex_san.y.c ${LEXDIR}/lmain.c \
	    ${LEXDIR}/sub1.c ${LEXDIR}/sub2.c ${LEXDIR}/header.c

check: check-native check-sanitized

check-native: lex_host
	${TEST} run --lex ./lex_host --source ${LEXDIR} ${EXAMPLES} \
	    --work work/native

# detect_leaks is off because lex exits without releasing its tables and
# LeakSanitizer needs ptrace, which some sandboxes withhold.
check-sanitized:
.if ${SAN_OK} == "yes"
	@${MAKE} lex_san
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST} run --lex ./lex_san --source ${LEXDIR} ${EXAMPLES} \
	    --work work/sanitized
	ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
	    ${TEST} mutants --source ${LEXDIR} ${EXAMPLES} --work work \
	    --yacc ${YACC} -- ${HOSTCC} ${LEX_CFLAGS} ${SAN_FLAGS}
.else
	@echo "SKIP lex-tables sanitized run and mutants: sanitizer runtime unavailable"
.endif

clean:
	rm -f lex_host lex_san lex_host.y.c lex_san.y.c
	rm -rf work lex_host.dSYM lex_san.dSYM

.PHONY: all check check-native check-sanitized clean
