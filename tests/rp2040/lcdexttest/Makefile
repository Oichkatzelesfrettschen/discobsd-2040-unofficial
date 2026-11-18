# lcdexttest: on-device test of the LCD_GFX_EXT commands of /dev/lcdgfx on
# the PICOCALC_SD kernel: memory read framing, write and read-back of all 480
# lines, a sprite, the scan-line wait, the power modes and a console
# screenshot. Regression tool, not shipped in the root manifest; a board run
# is opt-in.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		lcdexttest
OBJS=		lcdexttest.o
all:		${PROG}
${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
