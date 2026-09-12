# fptest: on-device bit-exact check of the RP2040 bootrom float path
# (lib/libc/arm/gen/rom_float.c). Built with the tree's arm-none-eabi cross
# toolchain and --wrap seam, then converted to an a.out to run as a DiscoBSD
# userland program. It is a regression tool, not part of the shipped root
# (distrib/rp2040/mi.rp2040 does not list it); copy fptest to the board over
# the console or a rebuilt image to run it, and expect "FPTEST OK".
#
# The expected-value corpus in corpus_single.h and corpus_double.h is the
# host's IEEE-754 round-to-nearest-even result for each case, regenerated
# with the standalone gencorpus helper preserved in the audit ledger
# (sys/arch/rp2040/doc/research/audit-response.md). It is an oracle
# independent of the code under test: the bootrom reproduces these bits
# exactly for the finite normal and signed-zero operands here.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=		fptest
OBJS=		fptest.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

fptest.o:	fptest.c corpus_single.h corpus_double.h

clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
