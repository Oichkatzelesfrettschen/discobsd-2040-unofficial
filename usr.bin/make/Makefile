# Description file for the Make command
# Makefile 4.5 94/1/3
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 5 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall -I../ar
YFLAGS		=

OBJS            = main.o doname.o misc.o files.o dosys.o gram.o

all:            xmake

xmake:          ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o make.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S make.elf > make.dis
		${SIZE} make.elf
		${ELF2AOUT} make.elf $@ && rm make.elf

clean:
		rm -f *.o *.elf xmake *.elf *.dis tags *~ gram.c y.tab.[ch]

install:        all
		${INSTALL} xmake ${DESTDIR}/usr/bin/make

gram.c:         gram.y
		${YACC} ${YFLAGS} gram.y
		grep -F -v "static char yaccpar_sccsid" y.tab.c >gram.c
		rm -f y.tab.c
