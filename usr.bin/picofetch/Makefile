#
# picofetch -- system and hardware readings beside a logo. See picofetch.c.
#
#   all     the target program
#   host    picofetch-host: the renderer over the fixture readings of
#           tests/hwinfo_fixture.c
#   test    the host tests of picofetch and ../picotop, then the mutants
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin
PROG=		picofetch
OBJS=		${PROG}.o pcdraw.o hwinfo_sysctl.o
HDRS=		hwinfo.h pcdraw.h
CFLAGS		+= -std=c17 -pedantic-errors

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	${HDRS}

HOSTCC?=	cc
HOSTBIN=	${PROG}-host
HOSTSRCS=	${PROG}.c pcdraw.c tests/hwinfo_fixture.c
HOST_CFLAGS=	-std=c17 -pedantic-errors -D_DEFAULT_SOURCE -DHOSTBUILD \
		-Wall -Wextra -Werror -Wshadow -Wconversion \
		-Wold-style-definition -O1 -I.

host:		${HOSTBIN}

${HOSTBIN}:	${HOSTSRCS} ${HDRS}
		${HOSTCC} ${HOST_CFLAGS} -o $@ ${HOSTSRCS}

require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

TOPHOST=	../picotop/picotop-host

${TOPHOST}:	../picotop/picotop.c pcdraw.c tests/hwinfo_fixture.c ${HDRS}
		${HOSTCC} ${HOST_CFLAGS} -o $@ ../picotop/picotop.c pcdraw.c \
		    tests/hwinfo_fixture.c

test-render:	require-python host ${TOPHOST}
		${PYTHON} -I tests/render.py ./${HOSTBIN} ${TOPHOST}

# The same tests as ILP32 programs: long is 32 bits there, as on the target,
# so arithmetic that fits a 64-bit long and overflows on the board fails here.
M32_OK!=	sh ${TOPSRC}/tools/compiler-probe.sh ilp32 ${HOSTCC} -m32

test-render32:	require-python
.if ${M32_OK} == "yes"
		${HOSTCC} ${HOST_CFLAGS} -m32 -o ${HOSTBIN}32 ${HOSTSRCS}
		${HOSTCC} ${HOST_CFLAGS} -m32 -o ${TOPHOST}32 ../picotop/picotop.c \
		    pcdraw.c tests/hwinfo_fixture.c
		${PYTHON} -I tests/render.py ./${HOSTBIN}32 ${TOPHOST}32
.else
		@echo "SKIP picofetch ILP32: ${HOSTCC} -m32 does not link and run; no 32-bit run occurred"
.endif

test-mutants:	require-python
.if ${M32_OK} == "yes"
		HOSTCC=${HOSTCC} PICOFETCH_M32=-m32 ${PYTHON} -I -u tests/mutants.py .
.else
		HOSTCC=${HOSTCC} ${PYTHON} -I -u tests/mutants.py .
.endif

test:		test-render test-render32 .WAIT test-mutants

clean:
		rm -f *.o *.elf *.dis ${PROG} ${HOSTBIN} ${TOPHOST} ${HOSTBIN}32 ${TOPHOST}32
		rm -rf tests/__pycache__

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		all host test test-render test-render32 test-mutants require-python clean install
