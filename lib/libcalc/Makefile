#
# libcalc: the numerical core of the calculator. See mathematics.h.
#
# `bmake` builds ../libcalc.a with the tree's cross compiler for
# MACHINE_ARCH. `bmake host` builds libcalc_host.a with the host compiler
# from the same sources under the same dialect and warning set; the host
# test in tests/libcalc links it.
#
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The library is strict C17 so the host test in tests/libcalc and the target
# build accept the same dialect.
CFLAGS		+= -std=c17 -pedantic-errors -Wshadow -Wconversion \
		   -Wold-style-definition

# One object per area, so a program that calls only the statistics pulls in
# only that object from the archive.
SRCS		= calc_util.c calc_scalar.c calc_stats.c calc_vector.c \
		  calc_matrix.c calc_complex.c calc_quat.c calc_fft.c \
		  calc_interp.c calc_elem.c calc_crc.c calc_rng.c \
		  calc_base64.c calc_pid.c
OBJS		= ${SRCS:.c=.o}
HEADERS		= mathematics.h calc_internal.h

HOSTCC?=	cc
HOSTAR?=	ar
HOSTRANLIB?=	ranlib
HOST_CFLAGS	= -std=c17 -pedantic-errors -Wall -Wextra -Wshadow \
		  -Wconversion -Wold-style-definition -O2
HOST_OBJS	= ${SRCS:.c=.host.o}

all:		../libcalc.a

../libcalc.a:	${OBJS}
		${AR} cr $@ ${OBJS}
		$(RANLIB) $@

${OBJS}:	${HEADERS}

host:		libcalc_host.a

libcalc_host.a:	${HOST_OBJS}
		${HOSTAR} cr $@ ${HOST_OBJS}
		${HOSTRANLIB} $@

.for src in ${SRCS}
${src:.c=.host.o}: ${src} ${HEADERS}
		${HOSTCC} ${HOST_CFLAGS} -c ${src} -o $@
.endfor

# Per-object text, data and bss, then the total, for the target archive.
size:		all
		${SIZE} ${OBJS}

install:	all
#		${INSTALL} ../libcalc.a ${DESTDIR}/usr/lib/libcalc.a

clean:
		rm -f ../libcalc.a libcalc_host.a *.o *~ tags

cleandir:	clean
		rm -f .depend

.PHONY:		all host size install clean cleandir
