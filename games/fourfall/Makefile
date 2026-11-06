TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The image build accepts the dialect of the host build: strict C17, the
# later flag winning over the tree default of gnu17.
CFLAGS		+= -std=c17 -pedantic-errors

BINDIR=		/usr/games
PROG=		fourfall
OBJS=		${PROG}.o
HOSTCC?=	cc
HOSTBIN=	${PROG}-host
LOGICBIN=	tests/logic_test
SANITIZE=	-fsanitize=address,undefined -fno-sanitize-recover=undefined

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# Same source, host compiler; -DHOSTBUILD picks the termios branch of
# ../gametty.h and _DEFAULT_SOURCE exposes select, gettimeofday and
# FIONREAD under strict C17 on a glibc host.
host:		${PROG}.c ${TOPSRC}/games/gametty.h
		${HOSTCC} -std=c17 -pedantic-errors -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
		    -DHOSTBUILD -o ${HOSTBIN} ${PROG}.c

# The rules half alone: tests/logic_test.c includes fourfall.c with
# FOURFALL_LOGIC and runs every line of tests/vectors.txt plus the
# property, gravity, lock-timer and queue checks, under strict C17
# with AddressSanitizer and UndefinedBehaviorSanitizer when
# tools/compiler-probe.sh builds and runs a program with them. A host
# without the runtime prints a SKIP line and runs the test unsanitized.
${LOGICBIN}:	${PROG}.c tests/logic_test.c
		@san=; \
		if [ "$$(sh ${TOPSRC}/tools/compiler-probe.sh native ${HOSTCC} ${SANITIZE})" = yes ]; then \
			san="${SANITIZE}"; \
		else \
			echo "SKIP fourfall sanitizers: runtime unavailable; logic test runs unsanitized"; \
		fi; \
		echo "${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1 $$san -o $@ tests/logic_test.c"; \
		${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1 \
		    $$san -o $@ tests/logic_test.c

test-logic:	${LOGICBIN}
		./${LOGICBIN} tests/vectors.txt

# Each deliberately wrong copy of the rules must be rejected by the logic
# test, and each wrong copy of the front end by the pty tests.
# The caller selects PYTHON, as for every Python-backed gate in the tree.
require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

test-mutants:	require-python ${LOGICBIN}
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
