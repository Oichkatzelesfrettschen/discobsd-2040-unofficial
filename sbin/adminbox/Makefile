#
# A multicall binary of the small system-administration tools:
# shutdown, reboot and sysctl, plus the zero-BSS true, false and nohup
# applets. sysctl (17332), shutdown (18284) and
# reboot (15788) each carry their own libc, and their globals do not
# collide once localized. passwd measures in the same size class
# (15340) but installs 04755 (setuid root); a multicall binary would
# hand every linked name root, so passwd stays its own a.out. See
# sbin/box/Makefile: each tool's own objects, already built in its
# directory, are combined with ld -r, every global except a renamed
# main is made local, and the results link once against libc.
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 9 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy

# Tools and where their objects live. A tool listed here still builds
# on its own; the manifest decides which form ships.
TOOLS=		shutdown reboot sysctl

# The built-in applets replace one-block scripts without adding private BSS.
BUILTIN_TOOLS=	true false nohup

# Other names a tool answers to, as name:tool.
ALIASES=	halt:reboot bootloader:reboot

OBJS=		${TOOLS:S/$/.tool.o/}
BUILTIN_OBJS=	tiny_applets.o
PROG=		adminbox
HSAOUT=		${TOPSRC}/tools/bin/hsaout
# Root budget for the packed image, a regression watermark rather than a
# hardware limit: hsaout -s reports 23 blocks from arm-none-eabi-gcc 16
# (Arch) for this tree, against 22 for the same toolchain before the C17
# _doprnt landed. The growth is in doprnt.c itself -- the hh, h, ll, z, t
# and j length modifiers, a standard %n, the space flag and a negative *
# precision -- and it is 472 raw bytes, 372 packed. It cannot be opted out
# of the way the 64-bit and float digit paths can: doprnt_llong.o and
# doprnt_float.o stay behind PRINTF_LLONG and PRINTF_FLOAT with weak
# __doprnt_ll and __doprnt_fp, so adminbox links neither.
MAX_PACKED_ROOT_BLOCKS= 23

CFLAGS+=	-Wall -Wextra -Werror -Wformat=2

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

adminbox.c:	adminbox.c.in Makefile
	decls=""; table=""; for t in ${TOOLS} ${BUILTIN_TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	for a in ${ALIASES}; do \
		table="$$table\n\t{ \"$${a%%:*}\", $${a#*:}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" adminbox.c.in > $@

verify-builtin-bss: ${BUILTIN_OBJS}
	@bss_symbols=`${GCCPREFIX}-nm -S --format=posix ${BUILTIN_OBJS} | \
	    awk '$$2 ~ /^[BbC]$$/ { print $$1 }'`; \
	if [ -n "$$bss_symbols" ]; then \
		echo "adminbox: built-in applets define mutable BSS:" >&2; \
		printf '%s\n' "$$bss_symbols" >&2; \
		exit 1; \
	fi

${PROG}:	adminbox.o ${OBJS} ${BUILTIN_OBJS} verify-builtin-bss \
		${MULTICALL_BSS_LDSCRIPT}
	${CC} ${CFLAGS} ${MULTICALL_BSS_LDFLAGS} ${LDFLAGS} -o $@.elf \
	    adminbox.o ${OBJS} ${BUILTIN_OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf
.if ${MACHINE} == "rp2040"
	@packed_blocks=`${HSAOUT} -s $@ | awk 'NR == 2 { print $$5 }'`; \
	if [ -z "$$packed_blocks" ] || \
	    [ "$$packed_blocks" -gt ${MAX_PACKED_ROOT_BLOCKS} ]; then \
		echo "adminbox: packed root image exceeds ${MAX_PACKED_ROOT_BLOCKS} blocks" >&2; \
		exit 1; \
	fi
.endif

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}/sbin/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} adminbox.c

.PHONY:		verify-builtin-bss
