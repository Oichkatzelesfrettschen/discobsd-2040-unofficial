# windowtest: on-device check described in windowtest.c. Regression tool, not shipped in
# the root manifest; expects a "WINDOWTEST" result line.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=		windowtest
OBJS=		windowtest.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
