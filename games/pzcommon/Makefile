# The text front end and the imported support code shared by the puzzles of
# games/puzzlebox, as one relocatable object. Every global stays visible so
# each puzzle's own object resolves its calls against it; games/puzzlebox
# and the standalone puzzles link it with -Wl,--gc-sections, which drops
# the support functions no puzzle reaches (-ffunction-sections gives each its
# own section).
#
# The host gate for all four puzzles lives here: `test` checks the imported
# sources against tatham.sha256, runs each puzzle's rule test, plays the host
# builds through a pty and checks that deliberately wrong copies of the
# sources are rejected.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -std=c17 -pedantic-errors -ffunction-sections

OBJS=		pzfront.o support.o
PUZZLES=	flood samegame unruly lightup
HOSTCC?=	cc

all:		pzcommon.o

pzcommon.o:	${OBJS}
		${LD} -r -o $@ ${OBJS}

pzfront.o support.o: pzfront.h ../gameterm.h ../gametty.h
support.o:	tatham/puzzles.h tatham/misc.c tatham/random.c tatham/malloc.c \
		tatham/combi.c

clean:
		rm -f *.o
		rm -f ${BUILD}/*
		-rmdir ${BUILD}

install:	all

# ---- host gate ------------------------------------------------------

include ${TOPSRC}/tools/host-test.mk

BUILD=		tests/build
HOSTWARN=	-std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1
.if ${SAN_OK} == "yes"
SAN=		${SAN_FLAGS}
.else
SAN=
.endif

require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

test-import:	require-python
		${PYTHON} tests/check_import.py .

# The support code and fatal() are compiled once for every rule test. The
# puzzle unit of a test is compiled with -fwhole-program so the collection's
# puzzle table is static and unreferenced and its drawing functions never
# reach the link; the support code is linked with section collection for the
# same reason.
${BUILD}/support.o: support.c tatham/puzzles.h tatham/misc.c tatham/random.c \
		tatham/malloc.c tatham/combi.c
		@mkdir -p ${BUILD}
		${HOSTCC} ${HOSTWARN} ${SAN} -ffunction-sections -c -o $@ support.c

${BUILD}/fatal.o: tests/pztest_fatal.c
		@mkdir -p ${BUILD}
		${HOSTCC} ${HOSTWARN} ${SAN} -c -o $@ tests/pztest_fatal.c

.for p in ${PUZZLES}
${BUILD}/logic_${p}: tests/logic_${p}.c tests/pztest.h tatham/${p}.c \
		tatham/puzzles.h ${BUILD}/support.o ${BUILD}/fatal.o
		${HOSTCC} ${HOSTWARN} ${SAN} -fwhole-program -c -o ${BUILD}/logic_${p}.o tests/logic_${p}.c
		${HOSTCC} ${SAN} -Wl,--gc-sections -o $@ ${BUILD}/logic_${p}.o \
		    ${BUILD}/support.o ${BUILD}/fatal.o -lm

# The unruly generator in the imported source leaks a blank game state on a
# rejected attempt (unruly.c, new_game_desc), so leak detection stays on for
# the other three puzzles only.
test-logic-${p}: ${BUILD}/logic_${p}
.if ${p} == "unruly"
		ASAN_OPTIONS=detect_leaks=0 ./${BUILD}/logic_${p}
.else
		./${BUILD}/logic_${p}
.endif
.endfor

test-logic:	${PUZZLES:S/^/test-logic-/}

host-builds:
.for p in ${PUZZLES}
		${MAKE} -C ../${p} MACHINE=${MACHINE} clean >/dev/null
		${MAKE} -C ../${p} MACHINE=${MACHINE} host
.endfor

test-play:	require-python host-builds
		${PYTHON} tests/play.py ..

# Each deliberately wrong copy of the sources or of the front end must be
# rejected by the rule tests or the pty test.
test-mutants:	require-python
		HOSTCC=${HOSTCC} ${PYTHON} tests/mutants.py .

test-play-mutants: require-python
		HOSTCC=${HOSTCC} ${PYTHON} tests/mutants.py --play .

# The rule tests depend on GCC's -fwhole-program and GNU ld's section
# collection to keep the collection's drawing functions out of the link, and
# the imported sources use sprintf, which the macOS SDK marks deprecated; a
# macOS host prints a SKIP and runs nothing, and the Linux hosts, including
# the Linux CI job, carry this gate's evidence.
HOST_OS!=	uname -s
.if ${HOST_OS} == "Darwin"
test:
		@echo "SKIP puzzlebox host gate: needs GCC -fwhole-program and GNU ld; no execution occurred"
.else
test:		test-import test-logic test-mutants test-play test-play-mutants
.endif

.PHONY:		all clean install require-python test test-import test-logic \
		test-play test-mutants test-play-mutants host-builds
