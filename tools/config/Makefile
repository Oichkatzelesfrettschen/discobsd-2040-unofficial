TOPSRC!=cd ../..; pwd

-include ../Makefile.inc

CFLAGS		= -O -Wall

# libbsd-dev package on Linux in overlay mode.
CFLAGS+=	${_LIBBSD_CFLAGS}
LIBS+=		${_LIBBSD_LIBS}

OBJS            = config.o main.o lang.o mkioconf.o mkmakefile.o mkswapconf.o
PROG		= config
MAN		= config.0
MANSRC		= config.8

all:		${PROG} ${MAN}

check:		${PROG}
		sh ./mkmakefile_test.sh ${.CURDIR}/${PROG} ${TOPSRC}

install:	all
		${INSTALL} ${PROG} ${TOOLBINDIR}/${PROG}

${MAN}:		${MANSRC}
		${MANROFF} ${MANSRC} > $@

clean:
		rm -f *~ *.o ${PROG} ${MAN} ${PROG}.dSYM y.tab.[ch] lang.c config.c

$(PROG):	$(OBJS)
		$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

y.tab.c:	config.y
		${YACC} -d config.y

y.tab.h:	y.tab.c
		test -f $@

config.o:	y.tab.c y.tab.h
		$(CC) $(CFLAGS) -c -o $@ y.tab.c

main.o: main.c y.tab.h config.h
mkheaders.o: mkheaders.c config.h y.tab.h
mkioconf.o: mkioconf.c y.tab.h config.h
mkmakefile.o: mkmakefile.c y.tab.h config.h
mkswapconf.o: mkswapconf.c config.h

.PHONY:		all check clean install
