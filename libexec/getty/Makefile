#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 62 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

OBJS		= main.o init.o subr.o gettytab.o get_date.o

all:            getty

getty:          ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o getty.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S getty.elf > getty.dis
		${SIZE} getty.elf
		${ELF2AOUT} getty.elf $@ && rm getty.elf

clean:
		rm -f *.o *.elf ${MAN} getty *.elf *.dis tags *~

install:        all
		${INSTALL} getty ${DESTDIR}/usr/libexec/getty

main.o: gettytab.h
init.o: gettytab.h
subr.o: gettytab.h
