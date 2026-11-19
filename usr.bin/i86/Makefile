# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# i86: an 8086, 80186 and 80286 real-mode PC for DOS (i86.1).
#
#   all       the target program and its manual page
#   host      i86-host: the same sources against the host's terminal
#   test      the host boot test of tests/check.py
#   check-qemu  cpu.c compiled for ARMv6-M against the sampled vectors,
#             under qemu-arm
#   test-sst  cpu.c against the SingleStepTests 8088 vectors in SST_DIR,
#             which this tree does not carry
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk
include ${TOPSRC}/tools/qemu-arm.mk

CFLAGS		+= -std=c17 -pedantic-errors -Wall -Wextra -Werror

OBJS		= main.o pc.o cpu.o mem.o
HDRS		= i86.h mem.h pc.h
MAN		= i86.0
MANSRC		= i86.1

all:		i86 ${MAN}

${OBJS}:	${HDRS}

# The interpreter is the program's inner loop: at -O2 it executes 161
# Thumb instructions for each guest instruction of the measured DOS session,
# at -Os 173, for 2.8 KB more text (docs/research/i86-dos-emulator.md).
cpu.o:		cpu.c
		${CC} ${CFLAGS:N-Os} -O2 -c cpu.c

i86:		${OBJS}
		${CC} ${CFLAGS} ${LDFLAGS} -o i86.elf ${OBJS} ${LIBS}
		${SIZE} i86.elf
		${ELF2AOUT} i86.elf $@ && rm i86.elf

${MAN}:	${MANSRC}
		${MANROFF} ${MANSRC} > ${MAN}

HOSTCC?=	cc
HOST_CFLAGS=	-std=c17 -pedantic-errors -D_DEFAULT_SOURCE -DHOSTBUILD \
		-Wall -Wextra -Werror -Wshadow -O2 -I.
HOSTSRCS=	main.c pc.c cpu.c mem.c

host:		i86-host

i86-host:	${HOSTSRCS} ${HDRS}
		${HOSTCC} ${HOST_CFLAGS} -o $@ ${HOSTSRCS}

tests/sstrun:	tests/sstrun.c cpu.c mem.c ${HDRS}
		${HOSTCC} ${HOST_CFLAGS} -o $@ tests/sstrun.c cpu.c mem.c

require-python:
		@if [ -z "${PYTHON}" ]; then \
			echo "PYTHON must name the intended interpreter" >&2; \
			exit 2; \
		fi

test:		require-python
		HOSTCC=${HOSTCC} ${PYTHON} -I -u tests/check.py .

test-sst:	require-python tests/sstrun
		@if [ -z "${SST_DIR}" ]; then \
			echo "SST_DIR must name the 8088 v2 vector directory" >&2; \
			exit 2; \
		fi
		${PYTHON} -I -u tests/sst.py tests/sstrun ${SST_DIR} ${SST_ARGS}

# cpu.c and mem.c as Thumb-1 code against newlib, with the Linux EABI system
# calls of tests/libbn/qemu_support.c, over the sampled vectors. The run
# covers the generated code of the interpreter; it does not run the DiscoBSD
# a.out, whose system calls qemu-user lacks. All pages are resident because
# that support file's lseek() moves nothing.
tests/sstrun-arm: tests/sstrun.c cpu.c mem.c ${HDRS} ${QEMU_SUPPORT}
		${QEMU_ARM_CC} ${QEMU_ARM_CFLAGS:N-Os} -O2 -DSSTRUN_NEWLIB -I. -o $@ \
		    tests/sstrun.c cpu.c mem.c ${QEMU_SUPPORT} ${QEMU_ARM_LIBS}

check-qemu:	require-python
.if empty(QEMU_ARM) && !empty(REQUIRE_QEMU)
		@echo "i86 qemu: qemu-arm unavailable and REQUIRE_QEMU is set" >&2; exit 1
.elif empty(QEMU_ARM)
		@echo "SKIP i86 qemu: qemu-arm unavailable; no ARM execution occurred"
.else
		@${MAKE} tests/sstrun-arm
		${PYTHON} -I -u tests/check.py . --vectors "${QEMU_ARM_RUN} tests/sstrun-arm"
.endif

clean:
		rm -f *.o *.elf ${MAN} i86 i86-host tests/sstrun tests/sstrun-arm tags *~
		rm -rf tests/__pycache__

install:	all
		cp ${MAN} ${DESTDIR}/usr/share/man/cat1/
		${INSTALL} i86 ${DESTDIR}/usr/bin/i86

.PHONY:		all host test test-sst check-qemu require-python clean install
