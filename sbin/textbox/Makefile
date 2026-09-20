#
# A multicall binary shares one libc image across small text utilities.
# The original sbase set, the tree's fold, rev and comm, the V7 look and
# deroff ports and the bounded BSD colrm filter use this executable.
# sbase's mkfifo remains unshipped because the kernel has no FIFO support.
# See research/posix-tools.md for the original admission measurements.
# See sbin/box/Makefile: each tool's own objects, already built in
# its directory, are combined with ld -r, every global except a
# renamed main is made local, and the results link once against
# libc. usr.bin/Makefile builds usr.bin before sbin, so this
# directory sees built objects when it runs.
#
# look and deroff are Seventh Edition Unix utilities (Caldera
# ancient-Unix grant, BSD-style; see usr.bin/look/LICENSE and
# usr.bin/deroff/LICENSE) rewritten with ANSI prototypes for Smaller
# C; see sys/arch/rp2040/doc/research/v7-tools.md. Standing alone
# each would cost about 8 KB of duplicated libc, so they join this
# box instead of shipping as their own a.out.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy

# Tools and where their objects live. A tool listed here still builds
# on its own; the manifest decides which form ships.
TOOLS=		cut paste seq dirname nl cksum expand unexpand \
		uuencode uudecode fold rev comm look deroff colrm

# Other names a tool answers to, as name:tool.
ALIASES=

OBJS=		${TOOLS:S/$/.tool.o/}
PROG=		textbox

all:		${PROG}
include ${TOPSRC}/share/mk/multicall-bss-overlay.mk

# Localize a tool: relocatable link of its objects, main renamed to
# <tool>_main and kept as the only global definition.
.for t in ${TOOLS}
$t.tool.o:
	set -e; d=""; for c in bin sbin usr.bin usr.sbin; do \
		for n in $t $t2; do [ -d ${TOPSRC}/$$c/$$n ] && d=${TOPSRC}/$$c/$$n; done; done; \
	${MAKE} -C $$d MACHINE=${MACHINE} clean >/dev/null; \
	${MAKE} -C $$d MACHINE=${MACHINE} COPTS="${MULTICALL_APPLET_COPTS}" all >/dev/null; \
	${LD} -r -o $@.r $$(ls $$d/*.o | grep -v '\.tool\.o$$'); \
	${OBJCOPY} --redefine-sym main=$t_main --keep-global-symbol=$t_main $@.r $@; \
	${OBJCOPY} --rename-section .bss=.app_bss_$t \
	    --rename-section .sbss=.app_bss_$t $@; \
	rm -f $@.r
.endfor

textbox.c:	textbox.c.in Makefile
	decls=""; table=""; for t in ${TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	for a in ${ALIASES}; do \
		table="$$table\n\t{ \"$${a%%:*}\", $${a#*:}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" textbox.c.in > $@

${PROG}:	textbox.o ${OBJS} ${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} -o $@.elf textbox.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}/usr/bin/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} textbox.c
