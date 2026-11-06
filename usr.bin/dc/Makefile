#
# dc -- the OpenBSD desk calculator, linked against lib/libbn. bc compiles
# these sources too (usr.bin/bc/Makefile), so a source list change here is a
# change there.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		dc

LIBBN=		${TOPSRC}/lib/libbn
SRCS=		main.c dc.c bcode.c inout.c mem.c stack.c
OBJS=		${SRCS:.c=.o}
CFLAGS		+= -std=c17 -pedantic-errors -I${LIBBN}
LIBS=		-lbn -lc

MAN=		${PROG}.0
MANSRC=		${PROG}.1

all:		${PROG} ${MAN}

${PROG}:	${OBJS} ${TOPSRC}/lib/libbn.a
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	compat.h extern.h bcode.h ${LIBBN}/bn.h

${TOPSRC}/lib/libbn.a:
		${MAKE} -C ${LIBBN} all

${MAN}:		${MANSRC}
		${MANROFF} ${MANSRC} > $@

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/

clean:
		rm -f *.o *.elf ${MAN} ${PROG} *.dis tags *~

.PHONY:		all install clean
