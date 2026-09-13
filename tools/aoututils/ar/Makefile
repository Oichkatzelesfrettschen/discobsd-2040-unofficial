#
# ar - create and maintain library archives
#

PROG=	ar
OBJS=	append.o ar.o archive.o contents.o delete.o extract.o \
	misc.o move.o print.o replace.o

include	../Makefile.inc

${OBJS}: ${TOPSRC}/usr.bin/ar/archive.h ${TOPSRC}/usr.bin/ar/extern.h
append.o: append.c ${TOPSRC}/usr.bin/ar/append.c
ar.o: ar.c ${TOPSRC}/usr.bin/ar/ar.c
archive.o: archive.c ${TOPSRC}/usr.bin/ar/archive.c
contents.o: contents.c ${TOPSRC}/usr.bin/ar/contents.c
delete.o: delete.c ${TOPSRC}/usr.bin/ar/delete.c
extract.o: extract.c ${TOPSRC}/usr.bin/ar/extract.c
misc.o: misc.c ${TOPSRC}/usr.bin/ar/misc.c
move.o: move.c ${TOPSRC}/usr.bin/ar/move.c
print.o: print.c ${TOPSRC}/usr.bin/ar/print.c
replace.o: replace.c ${TOPSRC}/usr.bin/ar/replace.c
