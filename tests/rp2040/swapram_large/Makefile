# bigtest and hugetest: on-device checks of the LARGE window epoch
# (sys/arch/rp2040/rp2040/swapram.c, sys/kern/exec_subr.c). bigtest's
# bss exceeds the 144 KB window and fits the 160 KB of window plus pool;
# hugetest's exceeds both and is refused at exec. Regression tools, not
# shipped in the root manifest; run as root; expect "BIGTEST OK" and
# sh's "too big".
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROGS=		bigtest hugetest

all:		${PROGS}

bigtest:	bigtest.c
		${CC} ${CFLAGS} ${LDFLAGS} -DBSSBYTES=150000 -o $@.elf bigtest.c ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

hugetest:	bigtest.c
		${CC} ${CFLAGS} ${LDFLAGS} -DBSSBYTES=170000 -o $@.elf bigtest.c ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f ${PROGS} *.elf *.o
