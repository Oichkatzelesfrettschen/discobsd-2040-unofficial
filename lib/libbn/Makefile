#
# libbn: the BN_* subset OpenBSD dc(1) and bc(1) use. See bn.h.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The library is strict C17 so the host test in tests/libbn and the target
# build accept the same dialect.
CFLAGS		+= -std=c17 -pedantic-errors

SRCS		= bn.c
OBJS		= bn.o

all:		../libbn.a

../libbn.a:	${OBJS}
		${AR} cr $@ ${OBJS}
		$(RANLIB) $@

bn.o:		bn.c bn.h

install:	all
#		${INSTALL} ../libbn.a ${DESTDIR}/usr/lib/libbn.a

clean:
		rm -f ../libbn.a *.o *~ tags

cleandir:	clean
		rm -f .depend
