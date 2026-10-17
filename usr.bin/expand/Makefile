TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -I../textbox
PROG	=  expand
OBJS	=  expand.o eprintf.o ealloc.o fshut.o ereallocarray.o strtonum.o rune.o

.if defined(LINK_MAP) && !empty(LINK_MAP)
LINK_MAP_FLAG=	-Wl,-Map,${LINK_MAP}
.endif

all:		${PROG}

eprintf.o:		../textbox/eprintf.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/eprintf.c

ealloc.o:		../textbox/ealloc.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/ealloc.c

fshut.o:		../textbox/fshut.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/fshut.c

ereallocarray.o:		../textbox/ereallocarray.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/ereallocarray.c

strtonum.o:		../textbox/strtonum.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/strtonum.c

rune.o:			../textbox/rune.c
			${CC} ${CFLAGS} -c -o $@ ../textbox/rune.c

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} ${LINK_MAP_FLAG} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
