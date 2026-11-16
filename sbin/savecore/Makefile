TOPSRC!=cd ../..; pwd
WARNLEVEL=	full
include ${TOPSRC}/share/mk/sys.mk
CFLAGS+=	-std=c17 -Wmissing-prototypes -Wold-style-definition

BIN		= savecore
SRCS            = savecore.c sysdep.c
OBJS            = savecore.o sysdep.o
MAN             = $(BIN).0

all:            $(BIN) ${MAN}

$(BIN):    ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@

${OBJS}:	savecore.h ${TOPSRC}/sys/arch/rp2040/include/dump.h

.SUFFIXES:      .0 .8

.8.0:
		${MANROFF} $*.8 > $@

clean:
		rm -f *.o *.elf ${MAN} *.elf *.dis tags *~ $(BIN)

install:        all
		${INSTALL} -m 755 ${BIN} ${DESTDIR}/sbin
		cp ${MAN} ${DESTDIR}/usr/share/man/cat8
