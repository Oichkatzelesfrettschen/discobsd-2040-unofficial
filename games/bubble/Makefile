TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The image build accepts the dialect of the host build: strict C17, the
# later flag winning over the tree default of gnu17.
CFLAGS		+= -std=c17 -pedantic-errors

BINDIR=		/usr/games
PROG=		bubble
OBJS=		${PROG}.o
HOSTCC?=	cc
HOSTBIN=	${PROG}-host
LOGICBIN=	tests/hex_test

all:		${PROG}

# tools/host-test.mk supplies SAN_FLAGS and the SAN_OK probe shared with
# the libcalc, libbn and calc host gates.
include ${TOPSRC}/tools/host-test.mk

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# Same source, host compiler; -DHOSTBUILD picks the termios branch of
# ../gametty.h and _DEFAULT_SOURCE exposes it under strict C17 on a glibc
# host.
host:		${PROG}.c ${TOPSRC}/games/gametty.h
		${HOSTCC} -std=c17 -pedantic-errors -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
		    ${HOST_FORTIFY} -DHOSTBUILD -o ${HOSTBIN} ${PROG}.c

# The rules half alone: tests/hex_test.c includes bubble.c with BUBBLE_LOGIC
# and runs every line of tests/vectors.txt plus property checks over random
# boards, under strict C17 with AddressSanitizer and
# UndefinedBehaviorSanitizer when SAN_OK says the host builds and runs a
# program with them.
${LOGICBIN}:	${PROG}.c tests/hex_test.c
.if ${SAN_OK} == "yes"
		${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1 \
		    ${SAN_FLAGS} -o $@ tests/hex_test.c
.else
		@echo "SKIP bubble sanitizers: runtime unavailable; logic test runs unsanitized"
		${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1 \
		    -o $@ tests/hex_test.c
.endif

test-logic:	${LOGICBIN}
		./${LOGICBIN} tests/vectors.txt

# Each deliberately wrong copy of the rules must be rejected by the logic
# test, and each wrong copy of the front end by the pty tests. The caller
# selects PYTHON, as for every Python-backed gate in the tree.
require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

test-mutants:	require-python
		HOSTCC=${HOSTCC} ${PYTHON} tests/mutants.py .

test-play-mutants: require-python
		HOSTCC=${HOSTCC} ${PYTHON} tests/mutants.py --play .

test-play:	require-python host
		${PYTHON} tests/play.py ./${HOSTBIN}

test:		test-logic test-mutants test-play test-play-mutants

clean:
		rm -f *.o *.elf ${PROG} ${HOSTBIN} ${LOGICBIN}

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		test test-logic test-mutants test-play test-play-mutants require-python
