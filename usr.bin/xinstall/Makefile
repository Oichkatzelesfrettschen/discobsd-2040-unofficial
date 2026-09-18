#
# Public Domain.  1996/2/20 - Steven Schultz
#
#	@(#)Makefile	1.0 (2.11BSD GTE) 1996/2/20
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 2 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

PROG=	xinstall
SRCS=	xinstall.c stat_flags.c
OBJS=	xinstall.o stat_flags.o
MAN=	install.0
MANSRC=	install.1

all:	${PROG} ${MAN}

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
	${OBJDUMP} -S $@.elf > $@.dis
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

${MAN}:	${MANSRC}
	${MANROFF} ${MANSRC} > $@

clean:
	rm -f *.o *.elf ${MAN} ${PROG} *.dis tags *~

install: all
	${INSTALL} ${PROG} ${DESTDIR}/usr/bin/install
	cp ${MAN} ${DESTDIR}/usr/share/man/cat1/

stat_flags.o: ${TOPSRC}/bin/ls/stat_flags.c
	${CC} ${CFLAGS} -c ${TOPSRC}/bin/ls/stat_flags.c -o $@
