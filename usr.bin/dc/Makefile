TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 6 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		dc

SRCS=		${PROG}.c
OBJS=		${PROG}.o

MAN=		${PROG}.0
MANSRC=		${PROG}.1

all:		${PROG} ${MAN}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${MAN}:		${MANSRC}
		${MANROFF} ${MANSRC} > $@

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/

clean:
		rm -f *.o *.elf ${MAN} ${PROG} *.dis tags *~
