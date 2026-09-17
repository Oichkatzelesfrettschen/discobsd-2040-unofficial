#
# Kilo -- a small screen editor, ported from antirez/kilo (see kilo.c for
# the upstream commit and what was cut to fit rp2040's a.out budget).
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		kilo

OBJS=		${PROG}.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# Host build for the pty-driven test: the same kilo.c, compiled with the
# host cc against the host's <termios.h> (TERMIOS selects that half of
# kilo.c's terminal code instead of the sgtty ioctls the target build uses).
HOST_CFLAGS=	-Wall -Wextra -Werror -O0 -g -DTERMIOS

kilo.host:	kilo.c
		${HOST_CC} ${HOST_CFLAGS} -o $@ kilo.c

PYTHON?=	python3

test:		kilo.host
		${PYTHON} test-pty.py ./kilo.host

clean:
		rm -f *.o *.elf ${PROG} kilo.host *.dis tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
