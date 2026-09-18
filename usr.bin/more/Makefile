#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 40 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

SRCS		= more.c
OBJS		= more.o

all:            more

more:        	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o more.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S more.elf > more.dis
		${SIZE} more.elf
		${ELF2AOUT} more.elf $@ && rm more.elf

clean:
		rm -f *.o *.elf ${MAN} more *.elf *.dis tags *~

install:        all more.help
		${INSTALL} more ${DESTDIR}/usr/bin/more
		@-rm -f ${DESTDIR}/usr/share/misc/more.help
		cp more.help ${DESTDIR}/usr/share/misc/more.help
