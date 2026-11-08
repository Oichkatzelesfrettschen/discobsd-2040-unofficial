# tiledemo: on-device check of the tile atlas of /dev/lcdgfx
# (sys/arch/rp2040/dev/lcdgfx.c) and the raw key events of the console
# (picokbd_evq.c). It uploads a tile set, repaints the 40 by 40 tile grid,
# pans a map sideways and scrolls it vertically through the panel's hardware
# scroll, timing each and printing the research note's prediction beside it.
# Regression tool, not shipped in the root manifest; a board run is opt-in.
TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk
PROG=		tiledemo
OBJS=		tiledemo.o
CFLAGS+=	-Wall -Wextra -Werror -I${TOPSRC}/games
all:		${PROG}
${PROG}:	${OBJS} ${TOPSRC}/games/gametile.h
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf
${OBJS}:	${TOPSRC}/games/gametile.h
clean:
		rm -f ${PROG} ${PROG}.elf ${OBJS}
