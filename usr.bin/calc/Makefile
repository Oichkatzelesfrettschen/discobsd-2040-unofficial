#
# calc -- scientific calculator for the PicoCalc console. See calc.c.
#
#   all           the target program, linked against lib/libcalc.a and libm
#   host          calc-host, the same source built for the host
#   test          every host test, then the mutants
#   check-qemu    the line interface as a Thumb-1 program under qemu-arm
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
#
TOPSRC!=cd ../..; pwd
PRINTF_FLOAT=	yes
include ${TOPSRC}/share/mk/sys.mk

BINDIR=		/usr/bin
PROG=		calc
OBJS=		${PROG}.o
LIBDIR=		${TOPSRC}/lib/libcalc
LIBS=		-lcalc -lm -lc
CFLAGS		+= -std=c17 -pedantic-errors -I${LIBDIR}

all:		${PROG}

${PROG}:	${OBJS} ${TOPSRC}/lib/libcalc.a
		${CC} ${CFLAGS} ${LDFLAGS} -o $@.elf ${OBJS} ${LIBS}
		${SIZE} $@.elf
		${ELF2AOUT} $@.elf $@ && rm $@.elf

${OBJS}:	${LIBDIR}/mathematics.h

${TOPSRC}/lib/libcalc.a:
		${MAKE} -C ${LIBDIR} all

HOSTCC?=	cc
HOSTBIN=	${PROG}-host
HOSTSAN=	${PROG}-host-san
LIBSRCS!=	ls ${LIBDIR}/calc_*.c
HOST_CFLAGS=	-std=c17 -pedantic-errors -D_DEFAULT_SOURCE -DHOSTBUILD \
		-Wall -Wextra -Werror -Wshadow -Wconversion \
		-Wold-style-definition -O1 -I${LIBDIR}
# tools/host-test.mk supplies the sanitizer probe, the tree libm objects and
# the qemu-arm newlib link shared with tests/libcalc and tests/libbn.
include ${TOPSRC}/tools/host-test.mk

host:		${HOSTBIN}

${HOSTBIN}:	${PROG}.c ${LIBSRCS} ${LIBDIR}/mathematics.h ${LIBDIR}/calc_internal.h
		${HOSTCC} ${HOST_CFLAGS} -o $@ ${PROG}.c ${LIBSRCS} -lm

${HOSTSAN}:	${PROG}.c ${LIBSRCS} ${LIBDIR}/mathematics.h ${LIBDIR}/calc_internal.h
		${HOSTCC} ${HOST_CFLAGS} ${SAN_FLAGS} -o $@ ${PROG}.c ${LIBSRCS} -lm

require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

# The line-mode table against Python's float and math, the units against
# NIST SP 811, the statistics against Python's statistics module.
test-line:	require-python host
		${PYTHON} -I tests/line.py ./${HOSTBIN}

# The same table under the address and undefined-behavior sanitizers.
test-line-san:	require-python
.if ${SAN_OK} == "yes"
		@${MAKE} ${HOSTSAN}
		ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
		    UBSAN_OPTIONS=halt_on_error=1 \
		    ${PYTHON} -I tests/line.py ./${HOSTSAN}
.else
		@echo "SKIP calc sanitizers: sanitizer runtime unavailable; no sanitized run occurred"
.endif

# The same table with the tree's lib/libm sources compiled for the host and
# linked ahead of the host libm (TREELIBM in tools/host-test.mk), so the
# trigonometric, exponential, logarithmic, power, root and rounding routines
# come from the code the target links. It runs on host IEEE hardware, so it
# measures that libm's algorithms and not the ROM float provider.
${HOSTBIN}-treelibm:	${PROG}.c ${LIBSRCS} ${TREELIBM_OBJS} ${LIBDIR}/mathematics.h
		${HOSTCC} ${HOST_CFLAGS} -fno-builtin -o $@ ${PROG}.c ${LIBSRCS} \
		    ${TREELIBM_OBJS} -lm

test-line-treelibm:	require-python
.if ${OBJCOPY_OK} == "yes"
		@${MAKE} ${HOSTBIN}-treelibm
		${PYTHON} -I tests/line.py ./${HOSTBIN}-treelibm
.else
		@echo "SKIP calc tree libm: objcopy unavailable; no tree libm run occurred"
.endif

# The terminal interface through a pty: item bar, F keys, signals, restore.
test-play:	require-python host
		${PYTHON} -I tests/play.py ./${HOSTBIN}

# Each deliberately wrong copy of calc.c must be rejected by the line table
# or the pty test. The unmutated tests pass first.
test-mutants:	require-python host
		HOSTCC=${HOSTCC} ${PYTHON} -I tests/mutants.py .

# The mutants run after every unmutated test has passed.
test:		test-line test-line-san test-line-treelibm test-play .WAIT test-mutants

# The line interface compiled for ARMv6-M against newlib, with the Linux
# EABI system calls of tests/libbn/qemu_support.c, and run under qemu-arm.
# The terminal code is compiled out (CALC_NO_TTY). The run covers the
# generated code of calc.c and libcalc on Thumb-1; it does not run the
# DiscoBSD a.out, whose syscalls and ROM float provider qemu-user lacks.
${PROG}-arm:	${PROG}.c ${LIBSRCS} ${QEMU_SUPPORT} ${LIBDIR}/mathematics.h
		${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS} -DCALC_NO_TTY -I${LIBDIR} \
		    -o $@ ${PROG}.c ${QEMU_SUPPORT} ${LIBSRCS} -lm ${QEMU_ARM_LIBS}

check-qemu:	require-python
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
		@echo "calc qemu: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
		@echo "SKIP calc qemu: qemu-arm unavailable; no ARM execution occurred"
.else
		@${MAKE} ${PROG}-arm
		${PYTHON} -I tests/line.py "${QEMU_ARM} ./${PROG}-arm"
.endif

clean:
		rm -f *.o *.elf *.dis ${PROG} ${HOSTBIN} ${HOSTSAN} ${HOSTBIN}-treelibm ${PROG}-arm
		rm -rf tests/__pycache__

install:	all
		${INSTALL} ${PROG} ${DESTDIR}${BINDIR}/${PROG}

.PHONY:		all host test test-line test-line-san test-line-treelibm test-play \
		test-mutants \
		check-qemu require-python clean install
