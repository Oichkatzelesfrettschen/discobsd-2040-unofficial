#
# picotop -- a full-screen system monitor. See picotop.c. The readings and
# the drawing tiers are the sources of ../picofetch.
#
#   all     the target program
#   host    picotop-host: the renderer over the fixture readings of
#           ../picofetch/tests/hwinfo_fixture.c
#
# usr.bin/picofetch's test target runs the host tests of both programs.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin
PROG=		picotop
SHARED=		${TOPSRC}/usr.bin/picofetch
OBJS=		${PROG}.o pcdraw.o hwinfo_sysctl.o
HDRS=		${SHARED}/hwinfo.h ${SHARED}/pcdraw.h
CFLAGS		+= -std=c17 -pedantic-errors -I${SHARED}

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	${HDRS}

pcdraw.o:	${SHARED}/pcdraw.c
		${CC} ${CFLAGS} -c -o $@ ${SHARED}/pcdraw.c

hwinfo_sysctl.o: ${SHARED}/hwinfo_sysctl.c
		${CC} ${CFLAGS} -c -o $@ ${SHARED}/hwinfo_sysctl.c

HOSTCC?=	cc
HOSTBIN=	${PROG}-host
HOSTSRCS=	${PROG}.c ${SHARED}/pcdraw.c ${SHARED}/tests/hwinfo_fixture.c
HOST_CFLAGS=	-std=c17 -pedantic-errors -D_DEFAULT_SOURCE -DHOSTBUILD \
		-Wall -Wextra -Werror -Wshadow -Wconversion \
		-Wold-style-definition -O1 -I${SHARED}

host:		${HOSTBIN}

${HOSTBIN}:	${HOSTSRCS} ${HDRS}
		${HOSTCC} ${HOST_CFLAGS} -o $@ ${HOSTSRCS}

clean:
		rm -f *.o *.elf *.dis ${PROG} ${HOSTBIN}

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		all host clean install
