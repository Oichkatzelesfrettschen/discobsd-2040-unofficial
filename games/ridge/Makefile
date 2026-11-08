TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The image build accepts the dialect of the host build: strict C17, the
# later flag winning over the tree default of gnu17.
CFLAGS		+= -std=c17 -pedantic-errors

BINDIR=		/usr/games
PROG=		ridge
OBJS=		${PROG}.o
HOSTCC?=	cc
LOGICBIN=	tests/logic_test
RENDERBIN=	tests/render_test
SHIM=		${TOPSRC}/tests/picocalc/shim

all:		${PROG}

# tools/host-test.mk supplies SAN_FLAGS and the SAN_OK probe shared with
# the libcalc, libbn and calc host gates.
include ${TOPSRC}/tools/host-test.mk

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	${TOPSRC}/games/gametile.h

# The rules half alone: tests/logic_test.c includes ridge.c with RIDGE_LOGIC.
# <sys/lcdcons.h> reaches the host through the PicoCalc header shim and
# _DEFAULT_SOURCE supplies its BSD integer types.
${LOGICBIN}:	${PROG}.c tests/logic_test.c
.if ${SAN_OK} == "yes"
		${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1 \
		    -D_DEFAULT_SOURCE ${SAN_FLAGS} -I${SHIM} -o $@ tests/logic_test.c
.else
		@echo "SKIP ridge sanitizers: runtime unavailable; logic test runs unsanitized"
		${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1 \
		    -D_DEFAULT_SOURCE -I${SHIM} -o $@ tests/logic_test.c
.endif

# The render half and the rules, with the two device calls of gametile.h bound
# to a model of /dev/lcdgfx.
${RENDERBIN}:	${PROG}.c tests/render_test.c ${TOPSRC}/games/gametile.h
.if ${SAN_OK} == "yes"
		${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1 \
		    -D_DEFAULT_SOURCE ${SAN_FLAGS} -I${SHIM} -o $@ tests/render_test.c
.else
		@echo "SKIP ridge sanitizers: runtime unavailable; render test runs unsanitized"
		${HOSTCC} -std=c17 -pedantic-errors -Wall -Wextra -Werror -g -O1 \
		    -D_DEFAULT_SOURCE -I${SHIM} -o $@ tests/render_test.c
.endif

test-logic:	${LOGICBIN}
		./${LOGICBIN}

test-render:	${RENDERBIN}
		./${RENDERBIN}

# Each deliberately wrong copy of the rules must be rejected by the logic
# test, and each wrong copy of the renderer by the render test. The caller
# selects PYTHON, as for every Python-backed gate in the tree.
require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

test-mutants:	require-python
		HOSTCC=${HOSTCC} ${PYTHON} tests/mutants.py .

test-render-mutants: require-python
		HOSTCC=${HOSTCC} ${PYTHON} tests/mutants.py --render .

test:		test-logic test-render test-mutants test-render-mutants

clean:
		rm -f *.o *.elf ${PROG} ${LOGICBIN} ${RENDERBIN}

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		test test-logic test-render test-mutants test-render-mutants require-python
