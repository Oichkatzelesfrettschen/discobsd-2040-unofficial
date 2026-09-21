TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

LIBDIR=		${TOPSRC}/lib/libreadline

CFLAGS		= -Os ${DEFS} -Wall

OBJS		= readline.o

all:            ../libreadline.a

%.o:		${LIBDIR}/%.c
		${CC} ${DEFS} ${CFLAGS} -c $< -o $@

../libreadline.a: ${OBJS}
		rm -f $@
		${AOUT_AR} rc $@ ${OBJS}
		${AOUT_RANLIB} $@

install:	../libreadline.a
		${INSTALL} -d ${DESTDIR}/usr/lib
		${INSTALL} ../libreadline.a ${DESTDIR}/usr/lib/libreadline.a
		${AOUT_RANLIB} -t ${DESTDIR}/usr/lib/libreadline.a

clean:
		rm -f *~ *.o a.out *.a ../libreadline.a


readline.o:	${LIBDIR}/readline.c
