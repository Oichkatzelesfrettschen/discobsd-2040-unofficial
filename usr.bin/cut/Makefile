TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -I../textbox
PROG	=  cut
OBJS	=  cut.o eprintf.o fshut.o reallocarray.o unescape.o memmem.o \
	   rune.o getline.o

all:		${PROG}

eprintf.o:		../textbox/eprintf.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/eprintf.c

fshut.o:		../textbox/fshut.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/fshut.c

reallocarray.o:		../textbox/reallocarray.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/reallocarray.c

unescape.o:		../textbox/unescape.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/unescape.c

memmem.o:		../textbox/memmem.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/memmem.c

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
