TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR= /usr/bin
PROG= strings
OBJS= strings.o
MAN= strings.0

all: ${PROG} ${MAN}

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
	${OBJDUMP} -S $@.elf > $@.dis
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
	rm -f *.o *.elf ${PROG} ${MAN} *.dis tags *~

.SUFFIXES: .0 .1
.1.0:
	${MANROFF} $*.1 > $@

install: all
	${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	${INSTALL} ${MAN} ${DESTDIR}/usr/share/man/cat1/
