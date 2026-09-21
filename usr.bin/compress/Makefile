#
#	@(#)Makefile	5.5 (Berkeley) 9/18/85
#
TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 3 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

OBJS 		= compress.o

#COMFLAGS       = -DBSD4_2 -O -DSACREDMEM=256000

CFLAGS          += -DUSERMEM=1024 -Wall

compress: $(OBJS)
	${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
	${OBJDUMP} -S $@.elf > $@.dis
	${SIZE} $@.elf
	${ELF2AOUT} $@.elf $@

install: compress
	${INSTALL} compress ${DESTDIR}/usr/bin/compress
	${INSTALL} compress ${DESTDIR}/usr/bin/uncompress
	${INSTALL} compress ${DESTDIR}/usr/bin/zcat

clean:
	rm -f compress core errs compress.o *.elf *.dis
