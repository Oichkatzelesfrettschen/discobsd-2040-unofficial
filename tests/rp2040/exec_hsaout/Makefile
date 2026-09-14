# textcrc: on-device check of the packed executable loader's clean-text
# restoration (sys/arch/rp2040/rp2040/exec_hsaout.c, sys/kern/vm_swap.c).
# The test manifest installs it raw and packed; board_exec_hsaout.py runs
# both across a swap and expects "TEXTCRC OK" from each. Regression tool,
# not shipped in the root manifest.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROGS=		textcrc

all:		${PROGS}

textcrc:	textcrc.c
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf textcrc.c ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f ${PROGS} *.elf *.o
