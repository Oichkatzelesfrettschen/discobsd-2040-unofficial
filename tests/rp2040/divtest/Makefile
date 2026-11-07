# divtest: on-device check of the SIO-divider integer division helpers
# (lib/libc/arm/gen/aeabi_div.S) on the RP2040 port. Regression tool, not
# shipped in the root manifest; expects "DIVTEST OK". Board execution needs
# the owner's explicit opt-in.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		divtest
OBJS=		divtest.o
all:		${PROG}
${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
