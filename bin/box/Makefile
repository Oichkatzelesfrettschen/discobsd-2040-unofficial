#
# A multicall binary of the small utilities. Each tool's own objects,
# already built in its directory, are combined with ld -r, every global
# except a renamed main is made local, and the results link once against
# libc. bin/Makefile builds this directory last.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy

# Tools and where their objects live. A tool listed here still builds
# on its own; the manifest decides which form ships.
TOOLS=		cat chgrp chmod cp date dd df echo hostname kill ln ls \
		mkdir mv pwd rm rmdir sleep stty sync test \
		chown mknod mount umount

# Other names a tool answers to, as name:tool.
ALIASES=	[:test

OBJS=		${TOOLS:S/$/.tool.o/}
PROG=		box

all:		${PROG}

# Localize a tool: relocatable link of its objects, main renamed to
# <tool>_main and kept as the only global definition.
.for t in ${TOOLS}
$t.tool.o:
	d=""; for c in bin sbin usr.bin usr.sbin; do \
		for n in $t $t2; do [ -d ${TOPSRC}/$$c/$$n ] && d=${TOPSRC}/$$c/$$n; done; done; \
	${LD} -r -o $@.r $$(ls $$d/*.o | grep -v '\.tool\.o$$'); \
	${OBJCOPY} --redefine-sym main=$t_main --keep-global-symbol=$t_main $@.r $@; \
	rm -f $@.r
.endfor

box.c:		box.c.in Makefile
	decls=""; table=""; for t in ${TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	for a in ${ALIASES}; do \
		table="$$table\n\t{ \"$${a%%:*}\", $${a#*:}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" box.c.in > $@

${PROG}:	box.o ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf box.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}/bin/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} box.c
