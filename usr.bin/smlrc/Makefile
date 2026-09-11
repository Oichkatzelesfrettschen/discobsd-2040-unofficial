TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The back end is selected by the machine: rp2040 is the Cortex-M0+ port and
# takes cgthumb.c, every other machine keeps the MIPS back end it had.
#
# The MIPS build declares itself to be Smaller C and supplies its own minimal
# library in lb.c, whose syscall stubs are MIPS assembly. The rp2040 build
# takes the tree's headers and libc instead, which it can because cgthumb.c
# names the frame size with a forward .equ rather than seeking back over the
# output: that is what removes the need for fgetpos and fsetpos, and the
# tree's libc has neither.
_TARGET_DEFS!=	if [ x"${MACHINE}" = x"rp2040" ] ; then \
			echo "-DTHUMB" ; \
		else \
			echo "-DMIPS -D__SMALLER_C_SCHAR__ -D__SMALLER_C__ -D__SMALLER_C_32__" ; \
		fi

_TARGET_OBJS!=	if [ x"${MACHINE}" = x"rp2040" ] ; then \
			echo "smlrc.o" ; \
		else \
			echo "smlrc.o lb.o" ; \
		fi

CFLAGS          = -Os -Wall ${_TARGET_DEFS} -DNO_ANNOTATIONS -DNO_PREPROCESSOR \
                  -DNO_PPACK -D_RETROBSD -DSTATIC \
                  -DNO_EXTRA_WARNS -DSYNTAX_STACK_MAX=3200

LIBS		= -lc

ARCH            = mips

OBJS            = ${_TARGET_OBJS}

all:		smlrc

smlrc:          $(OBJS)
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${OBJDUMP} -S $@.elf > $@.dis
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@

install:        smlrc
		${INSTALL} smlrc ${DESTDIR}/usr/libexec/


# The harness builds its own copy of the compiler with the host cc, so it
# needs the tree's location but none of the cross-build results above.
test:
		TOPSRC=${TOPSRC} MACHINE=${MACHINE} HOST_CC="${HOST_CC}" \
		    ${.CURDIR}/tests/run.sh

# Differential test against the host compiler, off the default path because
# it needs qemu-arm and a host cc that can target 32 bits.
fuzz:		test
		TOPSRC=${TOPSRC} HOST_CC="${HOST_CC}" ${.CURDIR}/tests/fuzz.sh

clean:
		rm -f *.o smlrc smlrc.dis smlrc.elf
		rm -rf tests/out
###
# mips16 halves the size of the MIPS build; the Cortex-M0+ executes only
# Thumb, so there is no corresponding option for it.
_SMLRC_COPTS!=	if [ x"${MACHINE}" = x"rp2040" ] ; then \
			echo "" ; \
		else \
			echo "-mips16" ; \
		fi

smlrc.o: smlrc.c fp.c cgmips.c cgthumb.c
		${CC} ${CFLAGS} ${_SMLRC_COPTS} smlrc.c -c -o $@
