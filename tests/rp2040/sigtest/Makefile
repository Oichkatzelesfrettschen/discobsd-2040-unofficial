# sigtest: on-device check of signal delivery and sigreturn on the RP2040
# port (sys/arch/rp2040/rp2040/sig_machdep.c). Regression tool, not shipped
# in the root manifest; expects "SIGTEST OK".
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		sigtest
OBJS=		sigtest.o
all:		${PROG}
${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
