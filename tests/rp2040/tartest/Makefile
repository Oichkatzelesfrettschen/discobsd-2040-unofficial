# tartest: on-device check that tar's -Z filter carries an archive through a
# pipe between two 96-kbyte processes (bin/tar/tar.c zfilter, zreap). The
# header formats are settled on the host by bin/tar/tests/tartest.sh; only
# the board answers for the fork, pipe and execl of /usr/bin/compress.
# Regression tool, not shipped in the root manifest.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		tartest
OBJS=		tartest.o
CFLAGS+=	-Wall -Wextra -Werror
all:		${PROG}
${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
