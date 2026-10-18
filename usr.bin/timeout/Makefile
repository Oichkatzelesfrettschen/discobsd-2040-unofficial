TOPSRC!=cd ../..; pwd
WARNLEVEL=	full
include ${TOPSRC}/share/mk/sys.mk

CFLAGS	+= -Wall -Wextra -Werror -Wformat=2
PROG	=  timeout
OBJS	=  timeout.o

.if defined(LINK_MAP) && !empty(LINK_MAP)
LINK_MAP_FLAG=	-Wl,-Map,${LINK_MAP}
.endif

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} ${LINK_MAP_FLAG} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} ${LINK_MAP} tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}/usr/bin/${PROG}
