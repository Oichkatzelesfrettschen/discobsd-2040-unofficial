# windemo: on-device demonstration of lib/libwin, three overlapping windows
# with one moved across the others. Regression tool, not shipped in the root
# manifest; a board run is opt-in.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		windemo
OBJS=		windemo.o
LIBDIR=		${TOPSRC}/lib/libwin
LIBS=		-lwin -lc
CFLAGS+=	-std=c17 -pedantic-errors -Wall -Wextra -Werror -I${LIBDIR}
all:		${PROG}
${PROG}:	${OBJS} ${TOPSRC}/lib/libwin.a
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
${OBJS}:	${LIBDIR}/win.h ${LIBDIR}/win_input.h ${LIBDIR}/win_widget.h
${TOPSRC}/lib/libwin.a:
		${MAKE} -C ${LIBDIR} all
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
