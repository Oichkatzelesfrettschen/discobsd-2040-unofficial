TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The image build accepts the dialect of the host build: strict C17, the
# later flag winning over the tree default of gnu17.
CFLAGS		+= -std=c17 -pedantic-errors

BINDIR=		/usr/games
PROG=		ridge
OBJS=		${PROG}.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	${TOPSRC}/games/gametile.h

clean:
		rm -f *.o *.elf ${PROG}

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

