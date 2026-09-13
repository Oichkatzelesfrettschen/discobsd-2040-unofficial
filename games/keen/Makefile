TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/games
PROG=		keen
OBJS=		${PROG}.o
HOSTCC?=	cc
HOSTBIN=	${PROG}-host
PYTHON?=	python3

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# Same source, host compiler; see games/fifteen/Makefile.
host:		${PROG}.c ${TOPSRC}/games/gametty.h
		${HOSTCC} -std=c99 -Wall -Wextra -DHOSTBUILD -o ${HOSTBIN} ${PROG}.c

test:		host
		${PYTHON} tests/play.py ./${HOSTBIN}
		${PYTHON} tests/uniqueness.py ./${HOSTBIN}

clean:
		rm -f *.o *.elf ${PROG} ${HOSTBIN}

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
