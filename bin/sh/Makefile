TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

.if defined(LINK_MAP) && !empty(LINK_MAP)
LINK_MAP_FLAG=	-Wl,-Map,${LINK_MAP}
.endif

OBJS            = setbrk.o blok.o stak.o cmd.o fault.o main.o word.o \
                  string.o name.o args.o xec.o service.o error.o io.o \
                  print.o macro.o expand.o ctype.o msg.o test.o defs.o \
                  echo.o hash.o hashserv.o pwd.o func.o edit.o pwname.o

all:		sh

check-gnu17:	tests/gnu17.c
		${CC} ${CFLAGS} -fsyntax-only tests/gnu17.c

check-strict-c17:
		@set -eu; \
		for source in ${OBJS:S/.o$/.c/}; do \
			${CC} ${CFLAGS} -std=c17 -pedantic-errors \
			    -Wall -Wextra -Werror -Wstrict-prototypes \
			    -Wold-style-definition -Wmissing-prototypes \
			    -fsyntax-only $$source; \
		done

.PHONY: check-gnu17 check-strict-c17

sh:		${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} ${LINK_MAP_FLAG} -o sh.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S sh.elf > sh.dis
		${SIZE} sh.elf
		${ELF2AOUT} sh.elf $@ && rm sh.elf

install:	all
		${INSTALL} sh ${DESTDIR}/bin/

clean:
		rm -f sh *.o *~ *.elf *.dis
		${MAKE} -C tests clean

args.o: args.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
blok.o: blok.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
builtin.o: builtin.c
cmd.o: cmd.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h sym.h
ctype.o: ctype.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
edit.o: edit.c edit.h
error.o: error.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
expand.o: expand.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
fault.o: fault.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
io.o: io.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
macro.o: macro.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h sym.h
main.o: main.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h sym.h timeout.h edit.h pwname.h
msg.o: msg.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h sym.h
name.o: name.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
pwname.o: pwname.c pwname.h
print.o: print.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
service.o: service.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
setbrk.o: setbrk.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
stak.o: stak.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
string.o: string.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h
word.o: word.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h sym.h
xec.o: xec.c defs.h mac.h mode.h name.h stak.h brkincr.h ctype.h sym.h
