TOPSRC!=cd ../..; pwd
PRINTF_FLOAT=	yes
# -Wall -Wextra raises 9 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS          += -Wall -O0

virus:          virus.c
		${CC} ${CFLAGS} ${LDFLAGS} -o virus.elf virus.c ${LIBS} -ltermcap
		$(SIZE) virus.elf
		$(ELF2AOUT) virus.elf $@

install:        virus
		${INSTALL} virus ${DESTDIR}/usr/bin/vi

clean:
		rm -f *.o virus.elf virus
