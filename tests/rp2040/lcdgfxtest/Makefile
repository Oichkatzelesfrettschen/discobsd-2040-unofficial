# lcdgfxtest: on-device speed and restore check of /dev/lcdgfx on the PICOCALC
# kernels (sys/arch/rp2040/dev/lcdgfx.c). It times the shapes the research
# note predicts and, with the argument "crash", kills itself while the device
# is open to check that the console text comes back. Regression tool, not
# shipped in the root manifest; a board run is opt-in.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		lcdgfxtest
OBJS=		lcdgfxtest.o
all:		${PROG}
${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
