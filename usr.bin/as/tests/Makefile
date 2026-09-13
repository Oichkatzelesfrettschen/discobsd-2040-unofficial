TOPSRC!=cd ../../..; pwd
include ${TOPSRC}/share/mk/sys.mk

CFLAGS		+= -Wall -Os
TESTS           = test1 test2 test3 test4 test5

# The tests run on the build host against its cross toolchain and the host
# build of the tree's own assembler and linker, which tools/aoututils puts
# in ${TOOLBINDIR}. The RP2040 has the three described in ../README.md;
# every other machine keeps the MIPS disassembly comparison.
_THUMB!=if [ x"${MACHINE}" = x"rp2040" ] ; then echo yes ; else echo no ; fi
HOST_CC?=	cc

all:
		for i in $(TESTS); do \
                    $(AS) $$i.s -o $$i.o; \
                    $(OBJDUMP) -D $$i.o > $$i.gcc-dis; \
                    ${AOUT_AS} $$i.s -o $$i.o; \
                    ${AOUT_AOUT} -r $$i.o > $$i.dis; \
                done

.if ${_THUMB} == "yes"
test:
			@AR=${AOUT_AR} RANLIB=${AOUT_RANLIB} WORK=. \
			    sh ./archive-extended-name-padding.sh
			@AR=${AOUT_AR} HOST_CC="${HOST_CC}" \
			    RANLIB=${AOUT_RANLIB} SOURCE_ROOT=${.CURDIR} WORK=. \
			    sh ./archive-transactional-rewrite.sh
			@AS=${AOUT_AS} CC="${CC}" TOPSRC=${TOPSRC} \
			    CPU=${MACHINE_CPU} WORK=. sh ./thumb-encoding.sh
		@AS=${AOUT_AS} CC="${CC}" TOPSRC=${TOPSRC} \
		    CPU=${MACHINE_CPU} WORK=. sh ./thumb-compile.sh
		@TOPSRC=${TOPSRC} MACHINE=${MACHINE} TOOLBINDIR=${TOOLBINDIR} \
		    CC="${CC}" CPU=${MACHINE_CPU} MAKE=${MAKE} WORK=. \
		    sh ./thumb-link.sh
.else
test:		all
.endif

clean:
		rm -f *.o *.dis *.gcc-dis *.aout *.aout.sec _diff.txt
			rm -f cat.s echo.s wc.s
			rm -rf thumb-wrap thumb-saved
			rm -rf archive-extended-name-padding
			rm -rf archive-transactional-rewrite

hello.s:        hello.c
		${CC} ${CFLAGS} -S hello.c
