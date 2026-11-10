#
# This is the Makefile for 'stdlib'.  New routines ported from 4.4BSD's
# libc/stdlib directory go here but existing libc/gen files are being left
# where they are.
#
TOPSRC!=cd ../../..; pwd
# -Wall -Wextra raises 4 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

DEFS		=
CFLAGS		+= ${DEFS} -Os

SRCS		= _Exit.c atoll.c bsearch.c getopt.c getsubopt.c heapsort.c \
		  reallocarray.c strtol.c strtoul.c strtoll.c strtoull.c strtod.c
OBJS		= _Exit.o atoll.o bsearch.o getopt.o getsubopt.o heapsort.o \
		  reallocarray.o strtol.o strtoul.o strtoll.o strtoull.o strtod.o

all:		stdlib.a

stdlib.a:	${OBJS}
		@echo "building stdlib.a"
		@${AR} cr stdlib.a ${OBJS}

clean:
		rm -f *.o *~ profiled/*.o tags Makefile.bak stdlib.a stdlib_p.s

depend:		${SRCS}
		mkdep ${CFLAGS} ${SRCS}

tags:
		cwd=`pwd`; \
		for i in ${SRCS}; do \
			ctags -a -f ${TAGSFILE} $$cwd/$$i; \
		done

# DO NOT DELETE THIS LINE -- mkdep uses it.
# DO NOT PUT ANYTHING AFTER THIS LINE, IT WILL GO AWAY.
