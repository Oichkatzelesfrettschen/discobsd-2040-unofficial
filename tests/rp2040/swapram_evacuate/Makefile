# evactest: on-device check of the compressed swap pool's evacuation to
# flash (sys/arch/rp2040/rp2040/swapram.c, swapram_evacuate). Regression
# tool, not shipped in the root manifest; expects "EVACTEST OK".
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=		evactest
OBJS=		evactest.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
