TOPSRC!=cd ../../..; pwd

-include ${TOPSRC}/tools/Makefile.inc

CFLAGS=	-Os -Wall -std=gnu89 -fno-builtin -DCROSS \
	-I. -I../include \
	-idirafter ${TOPSRC}/include \
	-idirafter ../ar

# libbsd-dev package on Linux in overlay mode.
CFLAGS+=${_LIBBSD_CFLAGS}
LIBS+=	${_LIBBSD_LIBS}

all:	${PROG}

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ ${OBJS} ${LIBS}

# Several objects compile a shim that includes the target program's own
# source (as/as-thumb.c includes usr.bin/as/as-thumb.c), so the shim's date
# says nothing about the code. The compiler records every file it read in
# a .d file beside the object, and each one rebuilds the object it names;
# the .d files load after "all" so none of their targets becomes the
# default. The flags here and in each Makefile bind every object too,
# which also rebuilds an object compiled before it had a .d file.
CFLAGS+=-MMD -MP
${OBJS}: Makefile ../Makefile.inc
.for _dep in ${OBJS:.o=.d}
.-include "${_dep}"
.endfor

install: all
	${INSTALL} ${PROG} ${TOOLBINDIR}/${PROG}

clean:
	rm -f *.o *.d ${PROG} *~

.PHONY:	all install clean
