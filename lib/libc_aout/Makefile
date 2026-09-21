#
# libc_aout - a.out libraries, built with a.out utilities
#

SUBDIR=	startup libc libm libcurses libtermlib \
	libreadline

all install:
	set -e; for dir in ${SUBDIR}; do \
		${MAKE} -C $$dir ${MFLAGS} $@; \
	done

clean:
	status=0; rm -f *~ *.o *.a || status=1; \
	for dir in ${SUBDIR}; do \
		${MAKE} -C $$dir ${MFLAGS} clean || status=1; \
	done; exit $$status
