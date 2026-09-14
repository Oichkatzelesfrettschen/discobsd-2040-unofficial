# Host test for the packed a.out container: sys/sys/exec_hsaout.h, the
# kernel's hsx_stream.c and hsx_decoder.c, and the packer in tools/hsaout.
# IMAGES names a directory tree of board a.outs to round-trip as well; the
# default is the distribution tree a "bmake MACHINE=rp2040 build" leaves.
HOSTCC?=	cc
TOPSRC!=	cd ../..; pwd
IMAGES?=	${TOPSRC}/distrib/obj/destdir.rp2040
PROG=		hsaout_test
LIB=		${TOPSRC}/tools/hsaout/libhsaout.a

check:		${PROG}
		sh ./packed_root_configs_test.sh \
		    ${TOPSRC}/tools/verify_packed_root_configs.sh
		./${PROG} ${IMAGES}

${PROG}:	${PROG}.c ${LIB} ${TOPSRC}/sys/sys/exec_hsaout.h
		${HOSTCC} -std=gnu11 -Wall -Wextra -Werror -I${TOPSRC}/tools/hsaout \
		    -I${TOPSRC}/tools/hsaout -idirafter ${TOPSRC}/sys \
		    -DHEATSHRINK_STATIC_WINDOW_BITS=9 -DHEATSHRINK_STATIC_LOOKAHEAD_BITS=3 \
		    -o $@ ${PROG}.c ${LIB}

${LIB}:
		${MAKE} -C ${TOPSRC}/tools/hsaout libhsaout.a

all:		check

clean:
		rm -f ${PROG}

.PHONY:		all check clean
