#
# as      - Assembler
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall -Wextra -Os
LDFLAGS         +=

# Cortex-M0+ and Cortex-M4 both accept the maintained Thumb instruction set.
ASSRC=	as-thumb.c
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
