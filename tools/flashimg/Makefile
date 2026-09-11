TOPSRC!=cd ../..; pwd

-include ../Makefile.inc

# Dhara is compiled from the kernel's vendored copy so the on-flash format
# comes from one set of sources.
DHARA		= ${TOPSRC}/sys/arch/rp2040/dhara
CFLAGS		= -O -Wall -I${DHARA} -I${TOPSRC}/sys/arch

OBJS		= flashimg.o error.o journal.o map.o
PROG		= flashimg

all:		${PROG}

install:	all
		${INSTALL} ${PROG} ${TOOLBINDIR}/${PROG}

clean:
		rm -f *~ *.o ${PROG}

$(PROG):	$(OBJS)
		$(CC) $(LDFLAGS) -o $@ $(OBJS)

error.o:	${DHARA}/error.c
		${CC} ${CFLAGS} -c -o $@ ${DHARA}/error.c

journal.o:	${DHARA}/journal.c
		${CC} ${CFLAGS} -c -o $@ ${DHARA}/journal.c

map.o:		${DHARA}/map.c
		${CC} ${CFLAGS} -c -o $@ ${DHARA}/map.c
