# forktest: on-device check described in forktest.c. Regression tool, not shipped in
# the root manifest; expects a "FORKTEST" result line.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=		forktest
OBJS=		forktest.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
