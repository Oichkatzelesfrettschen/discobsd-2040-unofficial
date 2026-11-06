# Host tests for the packed a.out container: sys/sys/exec_hsaout.h, the
# kernel's hsx_stream.c and hsx_decoder.c, and the packer in tools/hsaout.
# IMAGES names a directory tree of board a.outs to round-trip as well; the
# default is the distribution tree a "bmake MACHINE=rp2040 build" leaves.
#
#   check     the container against the loader's checks (hsaout_test)
#   decoder   the port's heatshrink decoder against the vendored decoder
#             (hsx_decoder_diff), then the mutant runner that breaks the
#             decoder and its stream loop 35 ways and requires a failure
#             from each; the caller selects PYTHON for the runner
HOSTCC?=	cc
TOPSRC!=	cd ../..; pwd
IMAGES?=	${TOPSRC}/distrib/obj/destdir.rp2040
PROG=		hsaout_test
LIB=		${TOPSRC}/tools/hsaout/libhsaout.a
HS=		${TOPSRC}/sys/arch/rp2040/heatshrink
KRP=		${TOPSRC}/sys/arch/rp2040/rp2040
INC=		${TOPSRC}/sys/arch/rp2040/include
DIFF=		hsx_decoder_diff
# The reference decoder and encoder are the vendored sources with dynamic
# allocation, so one build serves every lookahead; the kernel's decoder
# and stream loop come from the kernel tree unchanged.
DIFFFLAGS=	-std=gnu11 -O2 -Wall -Wextra -Werror \
		    -idirafter ${TOPSRC}/tools/hsaout -idirafter ${TOPSRC}/sys \
		    -I${HS} -DHEATSHRINK_DYNAMIC_ALLOC=1
DIFFOBJS=	${DIFF}.o diff_dec.o diff_stream.o diff_crc.o diff_refdec.o \
		diff_refenc.o

check:		${PROG}
		sh ./packed_root_configs_test.sh \
		    ${TOPSRC}/tools/verify_packed_root_configs.sh
		./${PROG} ${IMAGES}

${PROG}:	${PROG}.c ${LIB} ${TOPSRC}/sys/sys/exec_hsaout.h
		${HOSTCC} -std=gnu11 -Wall -Wextra -Werror \
		    -idirafter ${TOPSRC}/tools/hsaout -idirafter ${TOPSRC}/sys \
		    -DHEATSHRINK_STATIC_WINDOW_BITS=9 -DHEATSHRINK_STATIC_LOOKAHEAD_BITS=3 \
		    -o $@ ${PROG}.c ${LIB}

${LIB}:
		${MAKE} -C ${TOPSRC}/tools/hsaout libhsaout.a

decoder:	${DIFF}
		./${DIFF} ${IMAGES}
		@test -n "${PYTHON}" || { \
		    echo "PYTHON must name the intended interpreter" >&2; \
		    exit 2; }
		${PYTHON} -B hsx_decoder_mutants.py --cc ${HOSTCC} \
		    --topsrc ${TOPSRC} --images ${IMAGES}

${DIFF}:	${DIFFOBJS}
		${HOSTCC} -o $@ ${DIFFOBJS}

# tools/hsaout/machine links the port's include directory as machine/.
${DIFF}.o:	${DIFF}.c ${INC}/hsx_decoder.h
		${MAKE} -C ${TOPSRC}/tools/hsaout machine
		${HOSTCC} ${DIFFFLAGS} -c -o $@ ${DIFF}.c

diff_dec.o:	${KRP}/hsx_decoder.c ${INC}/hsx_decoder.h
		${HOSTCC} ${DIFFFLAGS} -c -o $@ ${KRP}/hsx_decoder.c

diff_stream.o:	${KRP}/hsx_stream.c ${INC}/hsx_decoder.h
		${HOSTCC} ${DIFFFLAGS} -c -o $@ ${KRP}/hsx_stream.c

diff_crc.o:	${TOPSRC}/sys/kern/subr_crc32.c
		${HOSTCC} ${DIFFFLAGS} -c -o $@ ${TOPSRC}/sys/kern/subr_crc32.c

diff_refdec.o:	${HS}/heatshrink_decoder.c
		${HOSTCC} ${DIFFFLAGS} -Wno-implicit-fallthrough -c -o $@ ${HS}/heatshrink_decoder.c

diff_refenc.o:	${HS}/heatshrink_encoder.c
		${HOSTCC} ${DIFFFLAGS} -Wno-implicit-fallthrough -c -o $@ ${HS}/heatshrink_encoder.c

all:		check

clean:
		rm -f ${PROG} ${DIFF} ${DIFFOBJS}

.PHONY:		all check clean decoder
