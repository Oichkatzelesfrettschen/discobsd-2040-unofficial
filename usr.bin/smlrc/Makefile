TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# The maintained machines use cgthumb.c and the tree's headers and libc. The
# back end names the frame size with a forward .equ rather than seeking back
# over the output; that removes the need for fgetpos and fsetpos, which the
# tree's libc does not provide.
CFLAGS          = -Os -Wall -DNO_ANNOTATIONS -DNO_PREPROCESSOR \
                  -DNO_PPACK -D_RETROBSD -DSTATIC \
                  -DNO_EXTRA_WARNS -DSYNTAX_STACK_MAX=3200

LIBS		= -lc

ARCH            = arm

OBJS            = smlrc.o

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
		TOPSRC=${TOPSRC} MACHINE=${MACHINE} CPU=${MACHINE_CPU} \
		    TOOLBINDIR=${TOOLBINDIR} PYTHON="${PYTHON}" HOST_CC="${HOST_CC}" \
		    ${.CURDIR}/tests/run.sh

# Differential test against the host compiler, off the default path because
# it needs qemu-arm and a host cc that can target 32 bits.
fuzz:		test
		TOPSRC=${TOPSRC} CPU=${MACHINE_CPU} PYTHON="${PYTHON}" \
		    HOST_CC="${HOST_CC}" ${.CURDIR}/tests/fuzz.sh

clean:
		rm -f *.o smlrc smlrc.dis smlrc.elf
		rm -rf tests/out
smlrc.o: smlrc.c fp.c cgthumb.c
		${CC} ${CFLAGS} smlrc.c -c -o $@
