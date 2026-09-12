TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -I../textbox
PROG	=  expand
OBJS	=  expand.o eprintf.o ealloc.o fshut.o reallocarray.o strtonum.o rune.o

all:		${PROG}

eprintf.o:		../textbox/eprintf.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/eprintf.c

ealloc.o:		../textbox/ealloc.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/ealloc.c

fshut.o:		../textbox/fshut.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/fshut.c

reallocarray.o:		../textbox/reallocarray.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/reallocarray.c

strtonum.o:		../textbox/strtonum.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/strtonum.c

rune.o:			../textbox/rune.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/rune.c

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
