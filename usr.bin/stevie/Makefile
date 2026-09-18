#
# STevie -- a modal vi, vendored from github.com/udo-munk/stevie (public
# domain, see LICENSE). window.c and hexchars.c are this port's; the
# other files carry upstream's text with the fixes named in each one.
#
# stevie.h carries an ANSI prototype for every function defined in the
# nine sources below, and every definition itself is ANSI style with an
# explicit return type, so this directory builds warning-free under
# -Wall -Wextra -Werror.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS+=	-Wall -Wextra -Werror

BINDIR=		/usr/bin

PROG=		stevie

SRCS=		cmdline.c edit.c help.c hexchars.c linefunc.c \
		main.c misccmds.c normal.c window.c

OBJS=		cmdline.o edit.o help.o hexchars.o linefunc.o \
		main.o misccmds.o normal.o window.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	stevie.h

# Host build for the pty-driven test: the same sources, compiled with the
# host cc against the host's <termios.h>. HOSTBUILD selects that half of
# window.c's terminal code instead of the sgtty ioctls the target build
# uses; nothing else differs. -std=gnu17 pins the dialect so the host
# build tracks the same language rules as the target's arm-none-eabi-gcc
# rather than drifting to whatever C standard the host compiler defaults
# to next.
HOST_CFLAGS=	-std=gnu17 -O0 -g -DHOSTBUILD -Wall -Wextra -Werror

${PROG}.host:	${SRCS} stevie.h
		${HOST_CC} ${WARNERR} ${HOST_CFLAGS} -o $@ ${SRCS}

PYTHON?=	python3

test:		${PROG}.host
		${PYTHON} test-pty.py ./${PROG}.host

clean:
		rm -f *.o *.elf ${PROG} ${PROG}.host *.dis tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
