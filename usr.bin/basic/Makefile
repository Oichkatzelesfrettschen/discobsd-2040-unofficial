TOPSRC!=cd ../..; pwd
PRINTF_FLOAT=	yes
# -Wall -Wextra raises 22 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall

OBJS            = basic.o

LIBS		= -lcurses -ltermcap -lm -lc

all:            basic renumber

basic:          ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o basic.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S basic.elf > basic.dis
		${SIZE} basic.elf
		${ELF2AOUT} basic.elf $@ && rm basic.elf

renumber:       renumber.o
		${CC} ${CFLAGS} ${LDFLAGS} -o renumber.elf renumber.o ${LIBS}
		${OBJDUMP} -S renumber.elf > renumber.dis
		${SIZE} renumber.elf
		${ELF2AOUT} renumber.elf $@ && rm renumber.elf

clean:
		rm -f *.o basic renumber *.elf *.dis *~

install:        all
		${INSTALL} basic ${DESTDIR}/usr/bin/
		${INSTALL} renumber ${DESTDIR}/usr/bin/
