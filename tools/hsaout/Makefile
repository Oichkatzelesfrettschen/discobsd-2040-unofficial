# The packed a.out tool and the library fsutil links for its "pack" manifest
# command. The encoder is the vendored heatshrink built with the container's
# parameters; the check pass compiles the kernel's own hsx_stream.c and
# hsx_decoder.c, so what the packer accepts is what the loader reproduces.

TOPSRC!=cd ../..; pwd

-include ../Makefile.inc

HS=		${TOPSRC}/sys/arch/rp2040/heatshrink
KRP=		${TOPSRC}/sys/arch/rp2040/rp2040

# -idirafter keeps the host's own <sys/...> and <machine/...> headers ahead
# of the kernel's (macOS's <sys/signal.h> includes <machine/signal.h>);
# only exec_hsaout.h, exec_aout.h and machine/hsx_decoder.h resolve into
# the kernel tree, the last through the machine link made below.
CFLAGS		= -O -Wall -Wextra -Werror -idirafter . -I${HS} -idirafter ${TOPSRC}/sys \
		  -DHEATSHRINK_STATIC_WINDOW_BITS=9 -DHEATSHRINK_STATIC_LOOKAHEAD_BITS=3
HSFLAGS		= ${CFLAGS} -Wno-implicit-fallthrough
LIBOBJS		= libhsaout.o subr_crc32.o hsx_stream.o hsx_decoder.o \
		  heatshrink_encoder.o
LIB		= libhsaout.a
PROG		= hsaout

CFLAGS+=	${_LIBBSD_CFLAGS}
LIBS+=		${_LIBBSD_LIBS}

all:		$(PROG)

install:	all
		${INSTALL} ${PROG} ${TOOLBINDIR}/${PROG}

clean:
		rm -f *~ *.o ${PROG} ${LIB} machine

machine:
		ln -sf ${TOPSRC}/sys/arch/rp2040/include machine

$(LIB):		$(LIBOBJS)
		rm -f $@
		ar rc $@ $(LIBOBJS)

$(PROG):	hsaout.o $(LIB)
		$(CC) $(LDFLAGS) -o $@ hsaout.o $(LIB) $(LIBS)

libhsaout.o:	libhsaout.c libhsaout.h machine
		$(CC) $(CFLAGS) -c -o $@ libhsaout.c

subr_crc32.o:	${TOPSRC}/sys/kern/subr_crc32.c
		$(CC) $(CFLAGS) -c -o $@ ${TOPSRC}/sys/kern/subr_crc32.c

hsx_stream.o:	${KRP}/hsx_stream.c machine
		$(CC) $(CFLAGS) -c -o $@ ${KRP}/hsx_stream.c

hsx_decoder.o:	${KRP}/hsx_decoder.c machine
		$(CC) $(HSFLAGS) -c -o $@ ${KRP}/hsx_decoder.c

heatshrink_encoder.o: ${HS}/heatshrink_encoder.c
		$(CC) $(HSFLAGS) -c -o $@ ${HS}/heatshrink_encoder.c

.PHONY:		all install clean
