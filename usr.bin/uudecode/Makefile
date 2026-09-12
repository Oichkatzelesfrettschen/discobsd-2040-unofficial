TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -I../textbox
PROG	=  uudecode
OBJS	=  uudecode.o eprintf.o fshut.o mode.o getline.o

all:		${PROG}

eprintf.o:	../textbox/eprintf.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/eprintf.c

fshut.o:	../textbox/fshut.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/fshut.c

mode.o:		../textbox/mode.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/mode.c

getline.o:	../textbox/getline.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/getline.c

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
