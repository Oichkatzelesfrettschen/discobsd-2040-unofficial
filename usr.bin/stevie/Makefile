#
# STevie -- a modal vi, vendored from github.com/udo-munk/stevie (public
# domain, see LICENSE). window.c and hexchars.c are this port's; the
# other files carry upstream's text with the fixes named in each one.
#
# The vendored sources are K&R throughout, so this directory builds
# without -Wall -Wextra -Werror: old-style definitions and missing
# prototypes are the source's shape, not defects to fix.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# GCC 14 and later diagnose implicit int, unprototyped calls and a bare
# return from a defaulted-int function as errors by default. All three are the K&R
# shape of the vendored source rather than defects, and rewriting 51
# kbytes of 1987 C into prototypes is the scope cut taken here; every
# other warning stays an error.
KRFLAGS=	-Wno-error=implicit-int \
		-Wno-error=implicit-function-declaration \
		-Wno-error=return-mismatch

CFLAGS+=	${KRFLAGS}

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
# uses; nothing else differs. -std=gnu17 is required because GCC 15 and
# later default to C23, where old-style definitions are gone.
HOST_CFLAGS=	-std=gnu17 -O0 -g -DHOSTBUILD ${KRFLAGS}

${PROG}.host:	${SRCS} stevie.h
		${HOST_CC} ${HOST_CFLAGS} -o $@ ${SRCS}

PYTHON?=	python3

test:		${PROG}.host
		${PYTHON} test-pty.py ./${PROG}.host

clean:
		rm -f *.o *.elf ${PROG} ${PROG}.host *.dis tags *~

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
