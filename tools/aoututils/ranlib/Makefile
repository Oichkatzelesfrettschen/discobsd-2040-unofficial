#
# ranlib - generate index to archive
#

PROG=	ranlib
OBJS=	archive.o ranlib.o

include	../Makefile.inc

ranlib.o: ranlib.c ${TOPSRC}/usr.bin/ranlib/ranlib.c \
	${TOPSRC}/usr.bin/ar/archive.h

archive.o: ../ar/archive.c ${TOPSRC}/usr.bin/ar/archive.c \
	${TOPSRC}/usr.bin/ar/archive.h ${TOPSRC}/usr.bin/ar/extern.h
	${CC} ${CFLAGS} -c -o $@ ../ar/archive.c
