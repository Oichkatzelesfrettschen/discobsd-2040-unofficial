TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS+=	-Wall

BINDIR=		/usr/bin

PROG=		fmt

OBJS=		${PROG}.o head.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f *.o *.elf ${PROG} *.dis tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
