#
# mille bourne game makefile
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 9 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS  	+= -Wall

OBJS            = comp.o end.o extern.o init.o mille.o misc.o move.o print.o \
                  roll.o save.o types.o varpush.o
MAN             = mille.0
MANSRC          = mille.6
LIBS            = -lcurses -ltermcap -lc

all:            mille $(MAN)

mille:          $(OBJS)
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		$(OBJDUMP) -S $@.elf > $@.dis
		$(SIZE) $@.elf
		$(ELF2AOUT) $@.elf $@ && rm $@.elf

${MAN}:		${MANSRC}
		${MANROFF} ${MANSRC} > $@

clean:
		rm -f *.o core mille *.0 *.dis

install:        all
		${INSTALL} mille ${DESTDIR}/usr/games/
		${INSTALL} -m 644 ${MAN} ${DESTDIR}/usr/share/man/cat6/
