TOPSRC!=cd ../..; pwd

-include ../Makefile.inc

CFLAGS		= -O -Wall

OBJS		= kernsum.o
PROG		= kernsum

all:		${PROG}

install:	all
		${INSTALL} ${PROG} ${TOOLBINDIR}/${PROG}

clean:
		rm -f *~ *.o ${PROG}

$(PROG):	$(OBJS)
		$(CC) $(LDFLAGS) -o $@ $(OBJS)
