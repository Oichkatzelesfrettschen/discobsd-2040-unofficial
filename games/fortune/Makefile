TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 2 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -O -Wall
LIBS            = -lc

OBJS            = fortune.o
MAN		= fortune.0
MANSRC		= fortune.6

all:            fortune $(MAN) fortunes.dat

fortune:        $(OBJS)
		${CC} ${CFLAGS} ${LDFLAGS} -o fortune.elf ${OBJS} ${LIBS}
		$(OBJDUMP) -S fortune.elf > fortune.dis
		$(SIZE) fortune.elf
		$(ELF2AOUT) fortune.elf $@ && rm fortune.elf

strfile:	strfile.c
		${HOST_CC} ${WARNERR} -o strfile strfile.c

unstr:		unstr.c
		${HOST_CC} ${WARNERR} -ansi -o unstr unstr.c

fortunes.dat:   #fortunes strfile
		./strfile -r fortunes

fortunes:       scene obscene
		cp scene fortunes
		echo "%-" >> fortunes
		cat obscene >> fortunes

$(MAN):         $(MANSRC)
		$(MANROFF) $(MANSRC) > $@

clean:
		rm -f *.o *.0 *.elf $(MAN) fortune strfile unstr fortunes *.elf *.dis tags *~

install:        all
		${INSTALL} fortune ${DESTDIR}/usr/games/
		cp $(MAN) $(DESTDIR)/usr/share/man/cat6/
		cp fortunes.dat $(DESTDIR)/usr/games/lib/

#$(OBJS):        fortune.h
