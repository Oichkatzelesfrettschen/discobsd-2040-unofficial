TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		pdp11

SRCS=		main.c cpu.c mmu.c unibus.c rk.c cons.c
OBJS=		${SRCS:.c=.o}
HOSTCC?=	cc
HOSTBIN=	${PROG}-host
PYTHON?=	python3

# Guest core in bytes; 64 KB boots V6 and leaves the 144 KB window room.
MEMSIZE?=	65536
IMAGE=		/usr/v6/root.rk
CFLAGS+=	-DMEMSIZE=${MEMSIZE} -DDEFAULT_IMAGE=\"${IMAGE}\" -Wall -Wextra -Werror

all:		${PROG}

${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	pdp11.h

# Same sources, host compiler: tests/v6boot.sh boots a V6 pack through it.
host:		${SRCS} pdp11.h
		${HOSTCC} -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE \
		    -DMEMSIZE=${MEMSIZE} -o ${HOSTBIN} ${SRCS}

test:		host
		sh tests/v6boot.sh ./${HOSTBIN}

clean:
		rm -f *.o *.elf ${PROG} ${HOSTBIN} *.dis tags *~

# v6.rk.gz is the pack tests/mkv6pack.py builds from TUHS's Dennis_v6
# v6root with tests/v6.list (see README.md); it lands beside the emulator
# as the pack it boots by default.
install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
		mkdir -p ${DESTDIR}/usr/v6
		gunzip -c v6.rk.gz > ${DESTDIR}${IMAGE}
