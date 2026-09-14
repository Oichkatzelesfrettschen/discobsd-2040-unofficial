#
# A multicall binary of the three games in games/fifteen, games/keen
# and games/bubble, dispatched on the name it was invoked by. See
# sbin/textbox/Makefile, whose pattern this copies: each tool's own
# objects, already built in its directory, are combined with ld -r,
# main is renamed to <tool>_main and every other global is made
# local, and the results link once against libc. Each tool still
# builds and installs on its own, as a full a.out with a real main,
# for the HOST test in its own tests/play.py.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy
BINDIR=		/usr/games

TOOLS=		fifteen keen bubble

OBJS=		${TOOLS:S/$/.tool.o/}
PROG=		gamebox

all:		${PROG}
include ${TOPSRC}/share/mk/multicall-bss-overlay.mk

.for t in ${TOOLS}
$t.tool.o:
	${MAKE} -C ${TOPSRC}/games/$t MACHINE=${MACHINE} $t.o >/dev/null
	${LD} -r -o $@.r ${TOPSRC}/games/$t/$t.o
	${OBJCOPY} --redefine-sym main=$t_main --keep-global-symbol=$t_main $@.r $@
	${OBJCOPY} --rename-section .bss=.app_bss_$t $@
	rm -f $@.r
.endfor

gamebox.c:	gamebox.c.in Makefile
	decls=""; table=""; for t in ${TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" gamebox.c.in > $@

${PROG}:	gamebox.o ${OBJS} ${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} -o $@.elf gamebox.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}${BINDIR}/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} gamebox.c

.PHONY:	all install clean
