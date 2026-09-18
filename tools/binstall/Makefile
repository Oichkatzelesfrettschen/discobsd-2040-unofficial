TOPSRC!=cd ../..; pwd

# -Wall -Wextra raises 6 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
-include ../Makefile.inc

PROG=	xinstall
OBJS=	xinstall.o

# Use uninstalled copy of the install program.
INSTALL=./xinstall

# libbsd-dev package on Linux in overlay mode.
CFLAGS+=${_LIBBSD_CFLAGS}
LIBS+=	${_LIBBSD_LIBS}

all:	${PROG}

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o $@ ${OBJS} ${LIBS}

install: all
	${INSTALL} ${PROG} ${TOOLBINDIR}/binstall

clean:
	rm -f *~ *.o ${PROG}

.PHONY: all install clean
