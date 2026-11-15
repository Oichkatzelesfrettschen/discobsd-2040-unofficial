# gpioprobe: on-device check of /dev/porta on the bare Pico
# (sys/arch/rp2040/dev/gpio.c). Regression tool, not shipped in the root
# manifest; prints one gpio.<step>=pass|fail line per request.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		gpioprobe
OBJS=		gpioprobe.o
all:		${PROG}
${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
