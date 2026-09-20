# swapmaptest: on-device read of CTL_VM.VM_SWAPMAP and CTL_VM.VM_NSWAP
# through sysctl(3), holding every free run the kernel returns inside the
# swap device. It is a regression tool, not part of the shipped root
# (distrib/rp2040/mi.rp2040 does not list it); stage it with a
# "file /usr/bin/swapmaptest" line for a test image or copy it to the board
# over the console, and expect "SWAPMAPTEST OK".
#
# tests/kernel/sysctl_test is the host half, linking sys/kern/kern_sysctl.c
# itself in both descriptor shapes; this one adds the syscall path, the
# port's own SMAPSIZ and the map a running kernel actually holds.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

PROG=		swapmaptest
OBJS=		swapmaptest.o

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
