TOPSRC!=cd ../../..; pwd
# -Wall -Wextra raises 4 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

LIBDIR=		${TOPSRC}/lib/libtermlib

COPTS!=if [ x"${MACHINE_ARCH}" = x"mips" ] ; then \
		echo "-Wa,-x -B${TOOLBINDIR}/" ; \
	else \
		echo "" ; \
	fi

CFLAGS		= -Os ${DEFS} -Wall ${COPTS}

OBJS		= termcap.o tgoto.o tputs.o tcattr.o

all:            ../libtermlib.a

%.o:		${LIBDIR}/%.c
		${CC} ${DEFS} ${CFLAGS} -c $< -o $@

../libtermlib.a: ${OBJS}
		rm -f $@
		${AOUT_AR} rc $@ ${OBJS}
		${AOUT_RANLIB} $@

install:	../libtermlib.a
		${INSTALL} -d ${DESTDIR}/usr/lib
		${INSTALL} ../libtermlib.a ${DESTDIR}/usr/lib/libtermlib.a
		${AOUT_RANLIB} -t ${DESTDIR}/usr/lib/libtermlib.a

clean:
		rm -f *~ *.o a.out *.a ../libtermlib.a


termcap.o:	${LIBDIR}/termcap.c
tgoto.o:	${LIBDIR}/tgoto.c
tputs.o:	${LIBDIR}/tputs.c
tcattr.o:	${LIBDIR}/tcattr.c
