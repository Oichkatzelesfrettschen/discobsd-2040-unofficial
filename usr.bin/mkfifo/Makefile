TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -I../textbox
PROG	=  mkfifo
OBJS	=  mkfifo.o eprintf.o mode.o

all:		${PROG}

eprintf.o:	../textbox/eprintf.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/eprintf.c

mode.o:		../textbox/mode.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/mode.c

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
