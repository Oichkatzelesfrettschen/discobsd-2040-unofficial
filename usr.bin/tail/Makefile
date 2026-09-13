TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		tail

OBJS=		${PROG}.o
HOSTCC?=	cc
HOSTBIN=	${PROG}-host
PYTHON?=	python3

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# Same source, host compiler; tests/tailcheck.py models the output and
# tests/ringcheck.sh takes the spool file away.
host:		${PROG}.c
		${HOSTCC} -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -o ${HOSTBIN} ${PROG}.c

test:		host
		${PYTHON} tests/tailcheck.py ./${HOSTBIN}
		sh tests/ringcheck.sh ./${HOSTBIN}

clean:
		rm -f *.o *.elf ${PROG} ${HOSTBIN} *.dis tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
