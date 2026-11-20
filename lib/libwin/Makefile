#
# libwin: a strip compositor for the PicoCalc panel. See win.h.
#
# The library is strict C17 so the host gate in tests/libwin and the target
# build accept the same dialect. lcdfont.o is the console's 6x9 font
# (sys/arch/rp2040/dev/lcdfont.c) built into the archive, the way
# usr.bin/matezinho builds it.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -std=c17 -pedantic-errors

OBJS		= win.o win_input.o win_input_dev.o win_widget.o win_lcdgfx.o lcdfont.o

all:		../libwin.a

../libwin.a:	${OBJS}
		${AR} cr $@ ${OBJS}
		$(RANLIB) $@

win.o win_lcdgfx.o win_input.o win_input_dev.o win_widget.o: win.h
win_input.o win_input_dev.o win_widget.o:	win_input.h
win_widget.o:	win_widget.h
win.o:		${TOPSRC}/sys/arch/rp2040/dev/lcdfont.h

lcdfont.o:	${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c
		${CC} ${CFLAGS} -c -o $@ ${TOPSRC}/sys/arch/rp2040/dev/lcdfont.c

install:	all
#		${INSTALL} ../libwin.a ${DESTDIR}/usr/lib/libwin.a

clean:
		rm -f ../libwin.a *.o *~ tags

cleandir:	clean
		rm -f .depend
