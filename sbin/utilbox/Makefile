#
# A multicall binary of the usr.bin tools that share the most libc:
# expr, id, more, uniq, head, tail, tr, wc, cmp, basename, env, printf,
# tee, touch, xargs, du, tty, uname and md5. See sbin/box/Makefile:
# each tool's own objects, already built in its directory, are
# combined with ld -r, every global except a renamed main is made
# local, and the results link once against libc. usr.bin/Makefile
# builds usr.bin before sbin, so this directory sees built objects
# when it runs.
#
# Four tools stay out after nm found their globals collide once every
# *.o in a directory is swept up and combined with the rest: sed and
# expr both carry the old regexp.c BRE engine's loc1, loc2, locs,
# braslist and braelist; sed and sort share eargc, eargv and ibuf;
# sort, sed and more each define their own nfiles; sort and uniq both
# define fields. Dropping sort and sed clears every one of those
# names, so both stay standalone rather than risk two tools sharing
# one variable through -fcommon merging at final link. find shares its
# directory with bigram and code, three programs each defining main
# and colliding globals (prefix_length, oldpath) once ld -r sweeps up
# every *.o in usr.bin/find; it stays a standalone a.out too. nohup is
# a shell script (usr.bin/nohup/nohup.sh) with no a.out to fold in.
# passwd, shutdown, reboot and sysctl stay out; see
# sys/arch/rp2040/doc/research/utilbox.md for the measurement that
# excluded them.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

OBJCOPY=	${GCCPREFIX}-objcopy

# Tools and where their objects live. A tool listed here still builds
# on its own; the manifest decides which form ships.
TOOLS=		expr id more uniq head tr wc cmp \
		basename env printf touch xargs tty uname md5

# Other names a tool answers to, as name:tool.
ALIASES=

OBJS=		${TOOLS:S/$/.tool.o/}
PROG=		utilbox

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

utilbox.c:	utilbox.c.in Makefile
	decls=""; table=""; for t in ${TOOLS}; do \
		decls="$$decls\nint $${t}_main(int, char **);"; \
		table="$$table\n\t{ \"$$t\", $${t}_main },"; done; \
	for a in ${ALIASES}; do \
		table="$$table\n\t{ \"$${a%%:*}\", $${a#*:}_main },"; done; \
	sed -e "s|@DECLS@|$$decls|" -e "s|@TABLE@|$$table|" utilbox.c.in > $@

${PROG}:	utilbox.o ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf utilbox.o ${OBJS} ${LIBS}
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@ && rm $@.elf

install:	all
	${INSTALL} -m 755 ${PROG} ${DESTDIR}/usr/bin/${PROG}

clean:
	rm -f *.o *.r *.elf ${PROG} utilbox.c
