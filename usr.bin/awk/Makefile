TOPSRC!=cd ../..; pwd
PRINTF_FLOAT=	yes
# -Wall -Wextra raises 211 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Os -ansi

# %expect is a bison extension in -y mode, and -Wno-yacc needs bison 3.
# Apple's command line tools carry bison 2.3, and Homebrew keeps its
# bison out of PATH (keg-only), so that keg is looked for first.
BISON!=		for b in /opt/homebrew/opt/bison/bin/bison /usr/local/opt/bison/bin/bison ; do \
			if [ -x "$$b" ] ; then echo "$$b" ; exit 0 ; fi ; \
		done ; echo bison
YACC            = ${BISON} -y -Wno-yacc
LIBS            = -lm -lc
FILES           = awk.lx.o b.o main.o tran.o lib.o run.o parse.o proctab.o freeze.o
SOURCE          = awk.def.h awk.g.y awk.lx.l b.c lib.c main.c parse.c \
                  proc.c freeze.c run.c tran.c

awk:            $(FILES) awk.g.o
		${CC} ${CFLAGS} ${LDFLAGS} -o awk.elf ${FILES} awk.g.o ${LIBS}
		${OBJDUMP} -S awk.elf > awk.dis
		${SIZE} awk.elf
		${ELF2AOUT} awk.elf $@ && rm awk.elf

y.tab.h:	awk.g.o

awk.h:          y.tab.h
		-cmp -s y.tab.h awk.h || cp y.tab.h awk.h

$(FILES):	awk.h awk.def.h

token.h:	awk.h tokenscript
		${EX} < tokenscript

install:        awk
		${INSTALL} awk ${DESTDIR}/usr/bin/

#profile:	awk.g.o $(FILES) mon.o
#		${HOST_CC} -p -i awk.g.o ${FILES} mon.o -lm

lint:
		lint -spu b.c main.c tran.c run.c lib.c parse.c -lm |\
			egrep -v '^(error|free|malloc)'

proctab.c:	proc.c token.h awk.h
		${HOST_CC} ${WARNERR} -std=gnu89 -Wall -o proc proc.c
		./proc > proctab.c

clean:
		-rm -f *.o *.tmp awk.lx.c *.out *.elf *.dis y.tab.* awk.h proc proctab.c token.h awk
