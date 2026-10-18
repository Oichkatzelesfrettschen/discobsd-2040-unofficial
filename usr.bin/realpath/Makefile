TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall
PROG	=  realpath
OBJS	=  realpath.o

.if defined(LINK_MAP) && !empty(LINK_MAP)
LINK_MAP_FLAG=	-Wl,-Map,${LINK_MAP}
.endif

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} ${LINK_MAP_FLAG} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
