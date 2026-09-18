#==========================================
#    Makefile: makefile for setty
#	Copyright 2012 Majenko Technolohies
#		(matt@majenko.co.uk
#	Last Modified: 29/01/2012
#==========================================

TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 4 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

OBJS            = setty.o
SRCS            = setty.c
LIBS            += -lcurses -ltermcap

all:            setty

setty:          ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o setty.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S setty.elf > setty.dis
		${SIZE} setty.elf
		${ELF2AOUT} setty.elf $@ && rm setty.elf

clean:
		-rm -f setty ${OBJS} setty.elf setty.dis

install:        all
		${INSTALL} setty ${DESTDIR}/usr/bin/
