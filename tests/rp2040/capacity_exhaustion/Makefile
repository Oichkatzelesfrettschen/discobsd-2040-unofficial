# proctab, filetab and inotab: on-device exhaustion checks described in
# proctab.c, filetab.c and inotab.c. Regression tools, not shipped in the
# root manifest; they print a "PROCTABTEST", "FILETABTEST" or "INOTABTEST"
# verdict line. board_capacity_exhaustion.py stages and runs all three.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROGS=		proctab filetab inotab
HELPER=		captest.o

all:		${PROGS}

.for program in ${PROGS}
${program}:	${program}.o ${HELPER}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${program}.o ${HELPER} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
.endfor

clean:
		rm -f ${PROGS} *.elf *.o

.PHONY:		all clean
