TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 3 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall

SRCS		= disktool.c
OBJS		= disktool.o

all:            disktool

disktool:       ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o disktool.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S disktool.elf > disktool.dis
		${SIZE} disktool.elf
		${ELF2AOUT} disktool.elf $@ && rm disktool.elf

clean:
		rm -f *.o *.elf disktool *.elf *.dis *~

install:        all
		${INSTALL} disktool ${DESTDIR}/usr/sbin/
