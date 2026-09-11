TOPSRC!=cd ../..; pwd

-include ../Makefile.inc

CFLAGS		= -O -Wall

OBJS		= boot2sum.o
PROG		= boot2sum

all:		${PROG}

install:	all
		${INSTALL} ${PROG} ${TOOLBINDIR}/${PROG}

clean:
		rm -f *~ *.o ${PROG}

$(PROG):	$(OBJS)
		$(CC) $(LDFLAGS) -o $@ $(OBJS)
