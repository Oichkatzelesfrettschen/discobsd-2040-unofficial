TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin
PROG=		unifdef
OBJS=		unifdef.o
MANSRC=		${TOPSRC}/share/man/man1/unifdef.1
MAN=		unifdef.0
CFLAGS+=	-std=c17 -Wall -Wextra -Werror -Wstrict-prototypes \
		-Wmissing-prototypes -Wold-style-definition

all:		${PROG} ${MAN}

${PROG}:	${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
	${OBJDUMP} -S $@.elf > $@.dis
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

${MAN}:	${MANSRC}
	${MANROFF} ${MANSRC} > $@

install:	all
	${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	cp ${MAN} ${DESTDIR}/usr/share/man/cat1/

clean:
	rm -f *.o *.elf *.dis ${PROG} ${MAN} tags *~
