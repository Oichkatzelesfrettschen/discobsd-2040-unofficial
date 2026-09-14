#
# A multicall binary of the pattern matchers grep and fgrep, which each
# link their own copy of libc. See sbin/utilbox/Makefile for the pattern:
# each tool's objects, built in its own directory, are combined with
# ld -r, every global except a renamed main is made local, and the
# results link once against libc. usr.bin builds before sbin, so this
# directory sees built objects when it runs. TOOLS is the member list.
#
# grep and fgrep are different engines (basic REs and Aho-Corasick), so
# the overlay does not deduplicate their code; the win is one libc copy
# and, more, fgrep's trie bounded by the pattern source so the shared
# extent is grep's small bss rather than fgrep's former fixed 64 kbytes.
# egrep stays standalone: it is not in the root manifest, and its DFA
# tables are algorithmic, so folding it would add text and set the extent.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy

# Tools and where their objects live. A tool listed here still builds
# on its own; the manifest decides which form ships.
TOOLS=		grep fgrep

# Other names a tool answers to, as name:tool.
ALIASES=

OBJS=		${TOOLS:S/$/.tool.o/}
PROG=		grepbox

all:		${PROG}
include ${TOPSRC}/share/mk/multicall-bss-overlay.mk

# Localize a tool: relocatable link of its objects, main renamed to
# <tool>_main and kept as the only global definition, and its .bss
# renamed for the overlay.
.for t in ${TOOLS}
$t.tool.o:
	set -e; d=""; for c in bin sbin usr.bin usr.sbin; do \
		[ -d ${TOPSRC}/$$c/$t ] && d=${TOPSRC}/$$c/$t; done; \
	${MAKE} -C $$d MACHINE=${MACHINE} clean >/dev/null; \
	${MAKE} -C $$d MACHINE=${MACHINE} COPTS="${MULTICALL_APPLET_COPTS}" all >/dev/null; \
	${LD} -r -o $@.r $$(ls $$d/*.o | grep -v '\.tool\.o$$'); \
	${OBJCOPY} --redefine-sym main=$t_main --keep-global-symbol=$t_main $@.r $@; \
	${OBJCOPY} --rename-section .bss=.app_bss_$t \
	    --rename-section .sbss=.app_bss_$t $@; \
	rm -f $@.r
.endfor

grepbox.c:	grepbox.c.in Makefile
	decls=""; table=""; for t in ${TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	for a in ${ALIASES}; do \
		table="$$table\n\t{ \"$${a%%:*}\", $${a#*:}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" grepbox.c.in > $@

${PROG}:	grepbox.o ${OBJS} ${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} -o $@.elf grepbox.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}/usr/bin/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} grepbox.c
