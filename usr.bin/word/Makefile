#
# word -- clean-room Word 5.1 command-menu TUI editor for DiscoBSD/rp2040.
# Built into DESTDIR/usr/bin/word and swept into the PICOCALC_SD 32 MiB
# SD root image by distrib/rp2040/mksdmanifest.py (omitted from the 979 KB
# flash root manifest mi.rp2040).
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		word

OBJS=		word.o word_menus.o word_find.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

HOST_CFLAGS=	-std=c17 -Wall -Wextra -Werror -O0 -g -DTERMIOS

word.host:	word.c word_menus.c word_find.c word.h
		${HOST_CC} ${HOST_CFLAGS} -o $@ word.c word_menus.c word_find.c

PYTHON?=	python3

test:		word.host
		${PYTHON} test-word.py ./word.host

clean:
		rm -f *.o *.elf ${PROG} word.host *.dis tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
