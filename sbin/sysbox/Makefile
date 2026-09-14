#
# The system tools as one binary; see bin/box/Makefile. Each tool's own objects,
# already built in its directory, are combined with ld -r, every global
# except a renamed main is made local, and the results link once against
# libc. bin/Makefile builds this directory last. bin/sysbox holds the
# system tools the same way; two images of 40 kbytes swap where one of
# 70 could not find a contiguous run in the swap map.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy

# Tools and where their objects live. A tool listed here still builds
# on its own; the manifest decides which form ships.
TOOLS=		date dd df mknod mount umount stty

# Other names a tool answers to, as name:tool.
ALIASES=

OBJS=		${TOOLS:S/$/.tool.o/}
PROG=		sysbox

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
	${OBJCOPY} --rename-section .bss=.app_bss_$t $@; \
	rm -f $@.r
.endfor

sysbox.c:	../box/box.c.in Makefile
	decls=""; table=""; for t in ${TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	for a in ${ALIASES}; do \
		table="$$table\n\t{ \"$${a%%:*}\", $${a#*:}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" ../box/box.c.in > $@

${PROG}:	sysbox.o ${OBJS} ${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} -o $@.elf sysbox.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}/bin/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} sysbox.c
