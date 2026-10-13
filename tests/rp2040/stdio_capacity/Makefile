# Board-only sampled-stack probe; the shipped manifest excludes stdiocap.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=           stdiocap
OBJS=           stdiocap.o
CFLAGS+=        -fstack-usage -Wextra
all:            ${PROG}
${PROG}:        ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS} stdiocap.su
.PHONY: all clean
