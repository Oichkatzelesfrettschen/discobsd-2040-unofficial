TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -I../textbox
PROG	=  cksum
OBJS	=  cksum.o eprintf.o fshut.o

all:		${PROG}

eprintf.o:	../textbox/eprintf.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/eprintf.c

fshut.o:	../textbox/fshut.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/fshut.c

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
