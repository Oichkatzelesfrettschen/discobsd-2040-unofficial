#
# A multicall binary of the four puzzles in games/flood, games/samegame,
# games/unruly and games/lightup, dispatched on the name it was invoked by.
# See games/gamebox/Makefile, whose pattern this copies: each puzzle's own
# object, already built in its directory, is combined with ld -r, main is
# renamed to <puzzle>_main and every other global is made local, and the
# results link once against libc. The text front end and the imported
# support code (games/pzcommon) are one more object that keeps its globals,
# because every puzzle calls it; the link collects sections so the support
# functions no puzzle reaches are dropped.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${C17_CAPTURE_PREFIX}${GCCPREFIX}-objcopy
BINDIR=		/usr/games

TOOLS=		flood samegame unruly lightup

# The members linked into this build, in the order the dispatch table lists
# them. PUZZLEBOX_TOOLS (set by distrib/rp2040/Makefile.inc from the image
# selection, see sys/arch/rp2040/doc/PROFILES.md) narrows the build to the
# members an image keeps; a build that leaves it unset links every tool, and
# a selection that keeps none builds nothing. TOOLS stays the full set the
# manifest links, which tools/check_multicall_composition.py reads.
.if defined(PUZZLEBOX_TOOLS)
MEMBERS=	${PUZZLEBOX_TOOLS}
.else
MEMBERS=	${TOOLS}
.endif
.for t in ${MEMBERS}
.if empty(TOOLS:M${t})
.error PUZZLEBOX_TOOLS names ${t}, which puzzlebox does not carry; it carries ${TOOLS}
.endif
.endfor

# Dispatch names the manifest deliberately leaves unlinked; the multicall
# composition gate (tools/check_multicall_composition.py) rejects any
# other dispatch name that ships no link.
UNSHIPPED_TOOLS=

OBJS=		${MEMBERS:S/$/.tool.o/}
COMMON=		${TOPSRC}/games/pzcommon/pzcommon.o
PROG=		puzzlebox

.if empty(MEMBERS)
all:
	@echo "puzzlebox: no member selected, nothing to build"
.else
all:		${PROG}
.endif
include ${TOPSRC}/share/mk/multicall-bss-overlay.mk

.for t in ${MEMBERS}
$t.tool.o:
	${MAKE} -C ${TOPSRC}/games/$t MACHINE=${MACHINE} clean >/dev/null
	${MAKE} -C ${TOPSRC}/games/$t MACHINE=${MACHINE} \
	    COPTS="${MULTICALL_APPLET_COPTS}" ${t}.o >/dev/null
	${LD} -r -o $@.r ${TOPSRC}/games/$t/$t.o
	${OBJCOPY} --redefine-sym main=$t_main --keep-global-symbol=$t_main $@.r $@
	${OBJCOPY} --rename-section .bss=.app_bss_$t \
	    --rename-section .sbss=.app_bss_$t $@
	rm -f $@.r
.endfor

# The shared object is rebuilt with the box's flags on every box build.
.PHONY:		common
common:
	${MAKE} -C ${TOPSRC}/games/pzcommon MACHINE=${MACHINE} \
	    COPTS="${MULTICALL_APPLET_COPTS}" clean >/dev/null
	${MAKE} -C ${TOPSRC}/games/pzcommon MACHINE=${MACHINE} \
	    COPTS="${MULTICALL_APPLET_COPTS}" all >/dev/null

# The table lists the selected members, which no file's age records, so the
# source is regenerated on every build like the objects it names.
.PHONY:		puzzlebox.c
puzzlebox.c:	puzzlebox.c.in Makefile
	decls=""; table=""; for t in ${MEMBERS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" puzzlebox.c.in > $@

${PROG}:	puzzlebox.o ${OBJS} common ${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} -Wl,--gc-sections \
	    ${LINK_MAP_FLAG} -o $@.elf puzzlebox.o ${OBJS} ${COMMON} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
.if !empty(MEMBERS)
	${INSTALL} -m 755 ${PROG} ${DESTDIR}${BINDIR}/${PROG}
.endif

clean:
	rm -f *.o *.r *.elf ${PROG} puzzlebox.c

.PHONY:	all install clean
