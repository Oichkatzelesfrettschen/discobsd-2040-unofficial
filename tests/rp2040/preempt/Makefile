# preempt: on-device oracle for the asynchronous system trap, the return to
# a process through userret from a clock tick (preempt.c states each case).
# Regression tool, not shipped in the root manifest; it prints
# "PREEMPT <case> OK". tools/renode/check-preempt.py runs it under Renode.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		preempt
OBJS=		preempt.o preempt_regs.o
all:		${PROG}
${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
