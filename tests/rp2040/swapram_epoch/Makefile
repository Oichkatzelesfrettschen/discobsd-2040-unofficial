# epochtest: on-device check of the SMALL/LARGE window epoch and the pool
# admission it governs (sys/arch/rp2040/rp2040/swapram.c). Regression
# tool, not shipped in the root manifest; run as root; expects
# "EPOCHTEST OK".
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=		epochtest
OBJS=		epochtest.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
