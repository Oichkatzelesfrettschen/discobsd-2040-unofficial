TOPSRC!=cd ../../..; pwd
# -Wall -Wextra raises 8 distinct sites in this directory, which assembles other
# programs' sources as fixtures, so the fatal set stays at the groups CFLAGS names.
WARNLEVEL=	legacy
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall -Os

# The tests run on the build host against its cross toolchain and the host
# build of the tree's own assembler and linker, which tools/aoututils puts
# in ${TOOLBINDIR}.
HOST_CC?=	cc

all:		test

test:
		@AR=${AOUT_AR} RANLIB=${AOUT_RANLIB} WORK=. \
		    sh ./archive-extended-name-padding.sh
		@AR=${AOUT_AR} HOST_CC="${HOST_CC}" \
		    RANLIB=${AOUT_RANLIB} SOURCE_ROOT=${.CURDIR} WORK=. \
		    sh ./archive-transactional-rewrite.sh
		@AR=${AOUT_AR} RANLIB=${AOUT_RANLIB} WORK=. \
		    sh ./archive-extract-all.sh
		@AS=${AOUT_AS} CC="${CC}" TOPSRC=${TOPSRC} \
		    CPU=${MACHINE_CPU} PYTHON="${PYTHON}" WORK=. \
		    sh ./thumb-encoding.sh
		@AS=${AOUT_AS} CC="${CC}" TOPSRC=${TOPSRC} \
		    CPU=${MACHINE_CPU} PYTHON="${PYTHON}" WORK=. \
		    sh ./thumb-compile.sh
		@TOPSRC=${TOPSRC} MACHINE=${MACHINE} TOOLBINDIR=${TOOLBINDIR} \
		    CC="${CC}" CPU=${MACHINE_CPU} MAKE=${MAKE} PYTHON="${PYTHON}" WORK=. \
		    sh ./thumb-link.sh

clean:
		rm -f *.o *.dis *.gcc-dis *.aout *.aout.sec _diff.txt
		rm -f cat.s echo.s wc.s
		rm -rf thumb-toolroot thumb-saved
		rm -rf archive-extended-name-padding
		rm -rf archive-transactional-rewrite
		rm -rf archive-extract-all
