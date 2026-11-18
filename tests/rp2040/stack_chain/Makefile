# Board-only driver of the kernel stack chains; the shipped manifest excludes stackchain.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=           stackchain
OBJS=           stackchain.o
CFLAGS+=        -fstack-usage -Wextra
all:            ${PROG}
${PROG}:        ${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS} stackchain.su
.PHONY: all clean
