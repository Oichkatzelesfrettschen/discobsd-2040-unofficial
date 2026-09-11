#
# as      - Assembler
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall -Wextra -Os
LDFLAGS         +=

# The RP2040 assembles Thumb-1; every other machine keeps the MIPS target.
ASSRC!=	if [ x"${MACHINE}" = x"rp2040" ] ; then \
		echo "as-thumb.c" ; \
	else \
		echo "as.c" ; \
	fi
ASOBJ=	${ASSRC:.c=.o}

all:            as

as:             ${ASOBJ}
		${CC} ${CFLAGS} ${LDFLAGS} -o as.elf ${ASOBJ} ${LIBS}
		${OBJDUMP} -S as.elf > as.dis
		${SIZE} as.elf
		${ELF2AOUT} as.elf $@ && rm as.elf

# The encoding and link tests run on the build host against the GNU
# toolchain; see tests/Makefile and README.md.
test:
		${MAKE} -C tests MACHINE=${MACHINE} test

clean:
		rm -f *.o *.0 *.elf as tags *~ *.dis
		${MAKE} -C tests clean

install:        all
		${INSTALL} as ${DESTDIR}/usr/bin/
