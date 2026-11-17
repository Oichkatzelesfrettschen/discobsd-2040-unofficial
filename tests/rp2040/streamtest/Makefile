# streamtest: on-device check that stdio works past NSTATIC static FILE
# slots (lib/libc/stdio/findiop.c). Holds twelve concurrent FILE objects over
# one file (one inode, one file-table entry) so the streams past the static
# slots exercise the dynamic _f_morefiles path, and reads each back.
# Regression tool, not shipped in the root manifest.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		streamtest
OBJS=		streamtest.o
all:		${PROG}
${PROG}:	${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
