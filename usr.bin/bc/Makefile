#
# bc -- the OpenBSD arbitrary-precision calculator. bc compiles its input to
# dc source, forks, and runs dc's evaluator (usr.bin/dc, linked into this
# program) in the child on the other end of a pipe. The grammar goes through
# bison in yacc mode and the scanner through flex, as usr.bin/awk's grammar
# does.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin

PROG=		bc

DCDIR=		${TOPSRC}/usr.bin/dc
LIBBN=		${TOPSRC}/lib/libbn

# %expect is a bison extension in -y mode, and -Wno-yacc needs bison 3.
# Homebrew keeps its bison out of PATH (keg-only), so that keg is looked for
# first.
BISON!=		for b in /opt/homebrew/opt/bison/bin/bison /usr/local/opt/bison/bin/bison ; do \
			if [ -x "$$b" ] ; then echo "$$b" ; exit 0 ; fi ; \
		done ; echo bison
# The OpenBSD grammar carries one shift/reduce and sixteen reduce/reduce
# conflicts that the generators' defaults resolve; an LALR bison cannot
# %expect a reduce/reduce conflict, so both classes are silenced here and
# tests/dcbc check-grammar holds the two counts.
YACC=		${BISON} -y -Wno-yacc -Wno-conflicts-sr -Wno-conflicts-rr
# macOS's own flex writes a scanner whose yy_size_t comparisons fail
# -Wsign-compare; Homebrew's flex, keg-only like its bison, writes the
# scanner the Linux flex does, so that keg is looked for first.
FLEX!=		for f in /opt/homebrew/opt/flex/bin/flex /usr/local/opt/flex/bin/flex ; do \
			if [ -x "$$f" ] ; then echo "$$f" ; exit 0 ; fi ; \
		done ; echo flex
LEX=		${FLEX}

DCSRCS=		dc.c bcode.c inout.c mem.c stack.c
DCOBJS=		${DCSRCS:.c=.o}
OBJS=		bc.o scan.o ${DCOBJS}
CFLAGS		+= -std=c17 -pedantic-errors -I. -I${LIBBN}
LIBS=		-lbn -lc

MAN=		${PROG}.0
MANSRC=		${PROG}.1

all:		${PROG} ${MAN}

${PROG}:	${OBJS} ${TOPSRC}/lib/libbn.a
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

# bc.h carries the token numbers scan.l needs, and bison writes it with bc.c.
# bc.c is the only target with the bison recipe, so a parallel make runs one
# writer; bc.h waits for it and regenerates only when the header alone is gone.
BCGEN=		${YACC} -o bc.c -d ${.CURDIR}/bc.y

bc.c:		bc.y
		${BCGEN}

bc.h:		bc.c
		@test -f $@ || ${BCGEN}

# flex includes <inttypes.h> in a C99 scanner and this tree has only
# <stdint.h>, which declares the fixed-width types the scanner uses.
scan.c:		scan.l
		${LEX} -o $@ ${.CURDIR}/scan.l
		sed 's/^#include <inttypes.h>/#include <stdint.h>/' $@ > $@.new
		mv $@.new $@

# The generated parser and scanner compile under the same warnings as the
# hand-written objects; the scanner needs the parser's token header first.
bc.o scan.o:	bc.h

${DCOBJS} bc.o scan.o: extern.h pathnames.h ${DCDIR}/compat.h ${LIBBN}/bn.h

# dc's sources compile into this directory against bc's include path; a
# search path would pick up the objects usr.bin/dc leaves beside them.
.for src in ${DCSRCS}
${src:.c=.o}:	${DCDIR}/${src} ${DCDIR}/extern.h ${DCDIR}/bcode.h
		${CC} ${CFLAGS} -c -o $@ ${DCDIR}/${src}
.endfor

${TOPSRC}/lib/libbn.a:
		${MAKE} -C ${LIBBN} all

${MAN}:		${MANSRC}
		${MANROFF} ${MANSRC} > $@

install:	all bc.library
		${INSTALL} -c -m 444 bc.library ${DESTDIR}/usr/share/misc
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/

clean:
		rm -f *.o *.elf ${MAN} ${PROG} *.dis tags *~ bc.c bc.h scan.c scan.c.new y.tab.[ch]

.PHONY:		all install clean
