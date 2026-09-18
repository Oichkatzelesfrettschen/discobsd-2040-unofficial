#
# Public Domain.  12/3/1994 - Steven Schultz
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 5 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -O
SRCS		= ls.c stat_flags.c
OBJS		= ls.o stat_flags.o

all:		ls

ls:		${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o ls.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S ls.elf > ls.dis
		${SIZE} ls.elf
		${ELF2AOUT} ls.elf $@ && rm ls.elf

clean:
		rm -f *.o *.elf *.dis ls tags *~

depend:		${SRCS}
		mkdep ${CFLAGS} ${SRCS}

install:	all
		${INSTALL} ls ${DESTDIR}/bin/ls

lint:		${SRCS}
		lint -hax ${SRCS}

tags:		${SRCS}
		ctags ${SRCS}
