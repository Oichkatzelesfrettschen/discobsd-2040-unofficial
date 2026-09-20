# mputest: user-mode oracle for the RP2040 MPU map that
# sys/arch/rp2040/rp2040/mpu.c programs. Shipped in the root so the Renode
# gate (tools/renode/boot.robot) and a person at the board console can run
# the deliberate fault test the protection claim rests on.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall

SRCS		= mputest.c
OBJS		= mputest.o
MAN		= mputest.0
MANSRC		= mputest.1

all:            mputest mputest.0

mputest:        ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o mputest.elf ${OBJS} ${LIBS}
		${SIZE} mputest.elf
		${ELF2AOUT} mputest.elf $@ && rm mputest.elf

mputest.0:      ${MANSRC}
		${MANROFF} ${MANSRC} > ${MAN}

clean:
		rm -f *.o *.elf ${MAN} mputest tags *~

install:        all
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/
		${INSTALL} mputest ${DESTDIR}/usr/bin/mputest
