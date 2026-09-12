TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -I../textbox
PROG	=  nl
OBJS	=  nl.o eprintf.o ealloc.o fshut.o strtonum.o unescape.o rune.o \
	   getline.o

all:		${PROG}

eprintf.o:		../textbox/eprintf.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/eprintf.c

ealloc.o:		../textbox/ealloc.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/ealloc.c

fshut.o:		../textbox/fshut.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/fshut.c

strtonum.o:		../textbox/strtonum.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/strtonum.c

unescape.o:		../textbox/unescape.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/unescape.c

rune.o:			../textbox/rune.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/rune.c

getline.o:		../textbox/getline.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/getline.c

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
