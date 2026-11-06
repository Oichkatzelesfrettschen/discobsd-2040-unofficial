# The puzzle as its own a.out, for the standalone size measurement that
# games/puzzlebox is compared against and for the host test. The rules are
# Simon Tatham's unruly.c (games/pzcommon/tatham), included by unruly.c as one
# unit with -fwhole-program so the collection's presentation functions, which
# need its graphics layer, are unreachable and the link discards them.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -std=c17 -pedantic-errors -fwhole-program

BINDIR=		/usr/games
PROG=		unruly
OBJS=		${PROG}.o
COMMON=		../pzcommon/pzcommon.o
HOSTCC?=	cc
HOSTBIN=	${PROG}-host

all:		${PROG}

# The shared object is rebuilt through its own Makefile on every make.
.PHONY:		common
common:
		${MAKE} -C ../pzcommon MACHINE=${MACHINE} all

${PROG}:	${OBJS} common
		${CC} ${CFLAGS} ${LDFLAGS} -Wl,--gc-sections -o $@.elf ${OBJS} ${COMMON} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	${PROG}.c ../pzcommon/pzfront.h ../pzcommon/tatham/${PROG}.c \
		../pzcommon/tatham/puzzles.h

# Host build for the pty test: the puzzle unit with -fwhole-program, the
# front end and the support code as ordinary units, linked with section
# collection.
host:		${HOSTBIN}

${HOSTBIN}:	${PROG}.c ../pzcommon/pzfront.c ../pzcommon/support.c \
		../pzcommon/pzfront.h ../gameterm.h ../gametty.h
		${HOSTCC} -std=c17 -pedantic-errors -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
		    -fwhole-program -O1 -c -o ${PROG}-host.o ${PROG}.c
		${HOSTCC} -std=c17 -pedantic-errors -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
		    -DHOSTBUILD -O1 -ffunction-sections -c -o pzfront-host.o ../pzcommon/pzfront.c
		${HOSTCC} -std=c17 -pedantic-errors -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
		    -O1 -ffunction-sections -c -o support-host.o ../pzcommon/support.c
		${HOSTCC} -Wl,--gc-sections -o $@ ${PROG}-host.o pzfront-host.o support-host.o -lm

clean:
		rm -f *.o *.elf ${PROG} ${HOSTBIN}

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		all host clean install
