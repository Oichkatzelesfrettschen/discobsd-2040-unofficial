TOPSRC!=cd ../..; pwd
# -Wall -Wextra raises 2 distinct sites in this directory, so the fatal set
# stays at the groups CFLAGS names until they are repaired.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS          += ${DEFS} -Wall

SRCS            = login.c logout.c logwtmp.c
# The C17 7.12 and 7.6 members: one function per member, so a program
# links only what it calls.  fpbits.h is their shared view of the encodings.
C17OBJS         = acosf.o acosh.o acoshf.o asinf.o asinh.o asinhf.o atan2f.o \
                  atanf.o atanh.o atanhf.o cbrt.o cbrtf.o ceilf.o copysign.o \
                  copysignf.o cosf.o coshf.o erfcf.o erff.o exp2.o exp2f.o \
                  expf.o expm1.o expm1f.o fabs.o fabsf.o fdim.o fdimf.o \
                  fenv.o floorf.o fma.o fmaf.o fmax.o fmaxf.o fmin.o fminf.o \
                  fmodf.o frexpf.o hypotf.o ilogb.o ilogbf.o ldexpf.o \
                  lgamma.o lgammaf.o llrint.o llrintf.o llround.o llroundf.o \
                  log10f.o log1p.o log1pf.o log2.o log2f.o logb.o logbf.o \
                  logf.o lrint.o lrintf.o lround.o lroundf.o nan.o nanf.o \
                  nearbyint.o nearbyintf.o nextafter.o nextafterf.o \
                  nexttowardf.o powf.o remainder.o remainderf.o remquo.o \
                  remquof.o rint.o rintf.o round.o roundf.o scalbln.o \
                  scalblnf.o scalbn.o scalbnf.o sinf.o sinhf.o sqrtf.o \
                  tanf.o tanhf.o tgamma.o tgammaf.o trunc.o truncf.o

OBJS            = asin.o atan.o exp.o erf.o floor.o fmod.o hypot.o j0.o j1.o \
                  jn.o log.o pow.o sin.o sinh.o sqrt.o tan.o tanh.o \
                  ${C17OBJS}

all:            ../libm.a

../libm.a:	${OBJS}
		${AR} cr $@ ${OBJS}
		$(RANLIB) $@

install:	all
#		${INSTALL} -d ${DESTDIR}/usr/lib
#		${INSTALL} ../libm.a ${DESTDIR}/usr/lib/libm.a
#		$(RANLIB) ${DESTDIR}/usr/lib/libm.a

clean:
		rm -f ../libm.a *.o *~ tags

cleandir:       clean
		rm -f .depend

depend:
		mkdep ${CFLAGS} ${SRCS}

tags:
		ctags ${SRCS}
