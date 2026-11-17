# paneltest: the PicoCalc panel register test (doc/PANELTEST.md); it
# needs a kernel built with -DLCD_PANELTEST and draws through /dev/lcdgfx.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall

SRCS		= paneltest.c
OBJS		= paneltest.o lcdfont.o
MAN		= paneltest.0
MANSRC		= paneltest.1

all:            paneltest paneltest.0

lcdfont.o:	${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c
		${CC} ${CFLAGS} -c -o $@ ${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c

paneltest:        ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o paneltest.elf ${OBJS} ${LIBS}
		${SIZE} paneltest.elf
		${ELF2AOUT} paneltest.elf $@ && rm paneltest.elf

paneltest.0:      ${MANSRC}
		${MANROFF} ${MANSRC} > ${MAN}

clean:
		rm -f *.o *.elf ${MAN} paneltest tags *~

install:        all
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/
		${INSTALL} paneltest ${DESTDIR}/usr/bin/paneltest
