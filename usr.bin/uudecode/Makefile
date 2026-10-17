TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -I../textbox
PROG	=  uudecode
OBJS	=  uudecode.o eprintf.o fshut.o mode.o

.if defined(LINK_MAP) && !empty(LINK_MAP)
LINK_MAP_FLAG=	-Wl,-Map,${LINK_MAP}
.endif

all:		${PROG}

eprintf.o:	../textbox/eprintf.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/eprintf.c

fshut.o:	../textbox/fshut.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/fshut.c

mode.o:		../textbox/mode.c
		${CC} ${CFLAGS} -c -o $@ ../textbox/mode.c

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} ${LINK_MAP_FLAG} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
