TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=matezinho
BINDIR=/usr/bin
OBJS=main.o core.o font.o
CFLAGS+=-DMZ_BOARD

all: ${PROG}

font.o: ${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c
	${CC} ${CFLAGS} -c -o $@ ${.ALLSRC}

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@

matezinho.host: main.c core.c matezinho.h
	${HOST_CC} -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Os \
	    -o $@ main.c core.c ${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c

install: all
	${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

clean:
	rm -f ${PROG} ${PROG}.elf ${PROG}.host *.o *.dis

.PHONY: all install clean
