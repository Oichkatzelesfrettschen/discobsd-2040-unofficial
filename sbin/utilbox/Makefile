#
# A multicall binary of the usr.bin tools that share the most libc.
# See sbin/box/Makefile: each tool's own objects, already built in its
# directory, are combined with ld -r, every global except a renamed main
# is made local, and the results link once against libc. usr.bin/Makefile
# builds usr.bin before sbin, so this directory sees built objects when it
# runs. TOOLS below is the authoritative member list.
#
# The relocatable link and symbol localization isolate applet globals,
# and the BSS overlay gives their zero-initialized private storage one
# shared address range. sort joins after replacing its resident byte
# tables and oversized merge-pointer array. find stays standalone until
# its user and group caches have bounded ownership. sed stays standalone
# because its compiled program and three work buffers overlap in time.
# Adminbox supplies nohup without importing a shell script. passwd,
# shutdown, reboot and sysctl stay out; see
# sys/arch/rp2040/doc/research/utilbox.md for the measurement that
# excluded them.
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 83 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy

# Tools and where their objects live. A tool listed here still builds
# on its own; the manifest decides which form ships.
TOOLS=		expr id more uniq head tr wc cmp sort \
		basename env printf touch xargs tty uname md5 resize md

# Other names a tool answers to, as name:tool.
ALIASES=	whoami:id groups:id logname:id

OBJS=		${TOOLS:S/$/.tool.o/}
PROG=		utilbox

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

utilbox.c:	utilbox.c.in Makefile
	decls=""; table=""; for t in ${TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	for a in ${ALIASES}; do \
		table="$$table\n\t{ \"$${a%%:*}\", $${a#*:}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" utilbox.c.in > $@

${PROG}:	utilbox.o ${OBJS} ${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} -o $@.elf utilbox.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

resize-symbol-contract: utilbox.o ${OBJS} ${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} \
	    -o utilbox.contract.elf utilbox.o ${OBJS} ${LIBS}
	sh ${TOPSRC}/tests/resize_contracts/symbol_closure.sh ${TOPSRC} \
	    utilbox.contract.elf
	rm -f utilbox.contract.elf

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}/usr/bin/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} utilbox.c

.PHONY: resize-symbol-contract
