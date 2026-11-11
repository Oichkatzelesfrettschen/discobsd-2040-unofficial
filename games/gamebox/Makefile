#
# A multicall binary of the games in games/fifteen, games/keen,
# games/bubble, games/fourfall, games/coil and games/ridge, dispatched on the name it
# was invoked by. See sbin/textbox/Makefile, whose pattern this copies: each tool's own
# objects, already built in its directory, are combined with ld -r,
# main is renamed to <tool>_main and every other global is made
# local, and the results link once against libc. Each tool still
# builds and installs on its own, as a full a.out with a real main,
# for the HOST test in its own tests/play.py.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${C17_CAPTURE_PREFIX}${GCCPREFIX}-objcopy
BINDIR=		/usr/games

TOOLS=		fifteen keen bubble fourfall coil ridge
MULTICALL_TOOL_ROOTS=	games

# The members linked into this build, in the order the dispatch table lists
# them. GAMEBOX_TOOLS (set by distrib/rp2040/Makefile.inc from the image
# selection, see sys/arch/rp2040/doc/PROFILES.md) narrows the build to the
# members an image keeps; a build that leaves it unset links every tool, and
# a selection that keeps none builds nothing. TOOLS stays the full set the
# manifest links, which tools/check_multicall_composition.py reads.
.if defined(GAMEBOX_TOOLS)
MEMBERS=	${GAMEBOX_TOOLS}
.else
MEMBERS=	${TOOLS}
.endif
.for t in ${MEMBERS}
.if empty(TOOLS:M${t})
.error GAMEBOX_TOOLS names ${t}, which gamebox does not carry; it carries ${TOOLS}
.endif
.endfor

# Dispatch names the manifest deliberately leaves unlinked; the multicall
# composition gate (tools/check_multicall_composition.py) rejects any
# other dispatch name that ships no link.
UNSHIPPED_TOOLS=

OBJS=		${MEMBERS:S/$/.tool.o/}
PROG=		gamebox

.if empty(MEMBERS)
all:
	@echo "gamebox: no member selected, nothing to build"
.else
all:		${PROG}
.endif
include ${TOPSRC}/share/mk/multicall-bss-overlay.mk

.for t in ${MEMBERS}
$t.tool.o:
	${MAKE} -C ${TOPSRC}/games/$t MACHINE=${MACHINE} clean >/dev/null
	${MAKE} -C ${TOPSRC}/games/$t MACHINE=${MACHINE} \
	    COPTS="${MULTICALL_APPLET_COPTS}" all >/dev/null
	${LD} -r -o $@.r ${TOPSRC}/games/$t/$t.o
	${OBJCOPY} --redefine-sym main=$t_main --keep-global-symbol=$t_main $@.r $@
	${OBJCOPY} --rename-section .bss=.app_bss_$t \
	    --rename-section .sbss=.app_bss_$t $@
	rm -f $@.r
.endfor

# The table lists the selected members, which no file's age records, so the
# source is regenerated on every build like the objects it names.
.PHONY:		gamebox.c
gamebox.c:	gamebox.c.in Makefile
	decls=""; table=""; for t in ${MEMBERS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" gamebox.c.in > $@

${PROG}:	gamebox.o ${OBJS} ${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} ${LINK_MAP_FLAG} \
	    -o $@.elf gamebox.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
.if !empty(MEMBERS)
	${INSTALL} -m 755 ${PROG} ${DESTDIR}${BINDIR}/${PROG}
.endif

clean:
	rm -f *.o *.r *.elf ${PROG} gamebox.c

.PHONY:	all install clean
