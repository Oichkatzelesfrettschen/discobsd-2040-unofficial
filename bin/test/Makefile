#	Makefile for test
#
#	1.1 	Robin Birch	11 march 1995
#	1.2 	Steven Schultz	27 october 1996
#
#	Version 1.2
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 2 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

SRCS            = test.c operators.c
OBJS            = test.o operators.o
MAN             = test.0
MANSRC          = test.1

all:            test $(MAN)

test:        	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o test.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S test.elf > test.dis
		${SIZE} test.elf
		${ELF2AOUT} test.elf $@ && rm test.elf

$(MAN):         ${MANSRC}
		${MANROFF} ${MANSRC} > $@

clean:
		rm -f *.o *.elf ${MAN} test *.elf *.dis tags *~

install:        all
		${INSTALL} test ${DESTDIR}/bin/
		@rm -f ${DESTDIR}/bin/[
		ln ${DESTDIR}/bin/test ${DESTDIR}/bin/[
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/
