#
# A multicall binary of the text utilities sbase (suckless.org, MIT/X)
# supplies that this port's tree does not: cut, paste, seq, dirname,
# nl, cksum, expand, unexpand, uuencode and uudecode. sbase's mkfifo
# and the tree's own comm, rev and fold compile for this target too
# but did not fit the 30 KB box budget; see research/posix-tools.md.
# See sbin/box/Makefile: each tool's own objects, already built in
# its directory, are combined with ld -r, every global except a
# renamed main is made local, and the results link once against
# libc. usr.bin/Makefile builds usr.bin before sbin, so this
# directory sees built objects when it runs.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy

# Tools and where their objects live. A tool listed here still builds
# on its own; the manifest decides which form ships.
TOOLS=		cut paste seq dirname nl cksum expand unexpand \
		uuencode uudecode

# Other names a tool answers to, as name:tool.
ALIASES=

OBJS=		${TOOLS:S/$/.tool.o/}
PROG=		textbox

all:		${PROG}

# Localize a tool: relocatable link of its objects, main renamed to
# <tool>_main and kept as the only global definition.
.for t in ${TOOLS}
$t.tool.o:
	d=""; for c in bin sbin usr.bin usr.sbin; do \
		for n in $t $t2; do [ -d ${TOPSRC}/$$c/$$n ] && d=${TOPSRC}/$$c/$$n; done; done; \
	${MAKE} -C $$d MACHINE=${MACHINE} all >/dev/null; \
	${LD} -r -o $@.r $$(ls $$d/*.o | grep -v '\.tool\.o$$'); \
	${OBJCOPY} --redefine-sym main=$t_main --keep-global-symbol=$t_main $@.r $@; \
	rm -f $@.r
.endfor

textbox.c:	textbox.c.in Makefile
	decls=""; table=""; for t in ${TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	for a in ${ALIASES}; do \
		table="$$table\n\t{ \"$${a%%:*}\", $${a#*:}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" textbox.c.in > $@

${PROG}:	textbox.o ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf textbox.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}/usr/bin/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} textbox.c
