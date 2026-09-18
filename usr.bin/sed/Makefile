TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 41 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

SRCS		= sed0.c sed1.c
OBJS		= sed0.o sed1.o

all:            sed

sed:            ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o sed.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S sed.elf > sed.dis
		${SIZE} sed.elf
		${ELF2AOUT} sed.elf $@ && rm sed.elf

clean:
		rm -f *.o *.elf ${MAN} sed *.elf *.dis tags *~

install:        all
		${INSTALL} sed ${DESTDIR}/usr/bin/

sed0.o: sed0.c sed.h
sed1.o: sed1.c sed.h
