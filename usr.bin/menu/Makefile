#
# menu -- a full-screen menu launcher for the USB console, modeled on
# Rockbox's action-table and cursor-list design (see menu.c's top comment
# and sys/arch/rp2040/doc/research/rockbox-ui.md; no Rockbox code is used).
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		menu

OBJS=		${PROG}.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# Host build for the pty-driven test: the same menu.c, compiled with the
# host cc against the host's <termios.h> (TERMIOS selects that half of
# menu.c's terminal code instead of the sgtty ioctls the target build
# uses), the same split usr.bin/kilo/kilo.c uses.
HOST_CFLAGS=	-Wall -Wextra -Werror -O0 -g -DTERMIOS

menu.host:	menu.c
		${HOST_CC} ${WARNERR} ${HOST_CFLAGS} -o $@ menu.c

PYTHON?=	python3

test:		menu.host
		${PYTHON} test-pty.py ./menu.host

clean:
		rm -f *.o *.elf ${PROG} menu.host *.dis tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
